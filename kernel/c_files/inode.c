#include "thread.h"
#include "string.h"
#include "stdint.h"
#include "inode.h"
#include "list.h"
#include "file.h"
#include "fs.h"

/* 用来存储 inode 位置 */
typedef struct {
	// inode 是否跨扇区
	uint8_t two_sec;
	// inode 所在的扇区号
	uint32_t sec_lba;
	// inode 在扇区内的字节偏移量
	uint32_t off_size;
} inode_position;

/* 获取 inode 所在的扇区和扇区内的偏移量 */
static void inode_locate(partition* part, uint32_t inode_no,
inode_position* inode_pos) {
	// 单硬盘最多支持 4096 个 inode
	ASSERT(inode_no < MAX_FILES_PER_PART);
	uint32_t inode_table_lba = part->sb->inode_table_lba;

	uint32_t inode_size = sizeof(inode);
	// 当前 inode 在 inode_table 中的偏移量
	uint32_t off_size = inode_no * inode_size;
	// 当前 inode 在相对 inode_table 的第几个扇区
	uint32_t off_sec = off_size / SECTOR_SIZE;
	// 当前 inode 在对应扇区内的偏移量
	uint32_t off_size_in_sec = off_size % SECTOR_SIZE;

	// 判断当前 inode 是否跨 2 个扇区
	uint32_t left_in_sec = SECTOR_SIZE - off_size_in_sec;
	if (left_in_sec < inode_size) {
		inode_pos->two_sec = 1;
	} else {
		inode_pos->two_sec = 0;
	}
	inode_pos->sec_lba = inode_table_lba + off_sec;
	inode_pos->off_size = off_size_in_sec;
}

/* 将 inode 写入到分区 part */
void inode_sync(partition* part, inode* in, void* io_buf) {
	uint8_t inode_no = in->i_no;
	inode_position inode_pos;
	inode_locate(part, inode_no, &inode_pos);
	ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt));

	// 用于清零 inode_tag 和 i_open_cnts
	inode pure_inode;
	memcpy(&pure_inode, in, sizeof(inode));
	pure_inode.i_open_cnts = 0;
	pure_inode.write_deny = 0;
	pure_inode.inode_tag.prev = pure_inode.inode_tag.next = NULL;

	uint8_t* inode_buf = (uint8_t*)io_buf;
	if (inode_pos.two_sec) {
		// 如果跨了两个扇区，就同时操作两个扇区
		ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
		// 更新这两个扇区中当前 inode 部分的数据
		memcpy(inode_buf + inode_pos.off_size, &pure_inode, sizeof(inode));
		// 写回更新后的扇区
		ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
	} else {
		ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
		memcpy(inode_buf + inode_pos.off_size, &pure_inode, sizeof(inode));
		ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
	}
}

/* 让 list_traversal 按 inode_no 进行查找 inode 的动作函数 */
static bool find_inode_by_inode_no(struct list_elem* elem, int inode_no) {
	return (elem2entry(inode, inode_tag, elem))->i_no == inode_no;
}
/* 根据 inode 结点号返回对应的 inode */
inode* inode_open(partition* part, uint32_t inode_no) {
	inode* inode_found = NULL;
	// 先试图在 inode 缓存中查找
	struct list_elem* elem = list_traversal(
		&part->open_inodes,
		find_inode_by_inode_no,
		inode_no
	);
	if (elem != NULL) {
		inode_found = elem2entry(inode, inode_tag, elem);
		inode_found->i_open_cnts++;
		return inode_found;
	}

	// 找不到就只能读盘
	inode_position inode_pos;
	inode_locate(part, inode_no, &inode_pos);

	// 为了让 inode 缓存被所有任务共享，需要将其置于内核空间
	// TODO: 但是文件操作需要走系统调用，所以这个函数不是本来就被内核调用吗
	task_struct* cur = running_thread();
	uint32_t* cur_pagedir_bak = cur->pgdir;
	cur->pgdir = NULL;
	inode_found = sys_malloc(sizeof(inode));
	cur->pgdir = cur_pagedir_bak;

	uint8_t* inode_buf;
	if (inode_pos.two_sec) {
		inode_buf = sys_malloc(SECTOR_SIZE * 2);
		ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
	} else {
		inode_buf = sys_malloc(SECTOR_SIZE);
		ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
	}
	memcpy(inode_found, inode_buf + inode_pos.off_size, sizeof(inode));

	// 将该 inode 放在队首方便检测
	list_push(&part->open_inodes, &inode_found->inode_tag);
	inode_found->i_open_cnts = 1;
	sys_free(inode_buf);

	return inode_found;
}

/* 关闭 inode 或者减少 inode 的打开次数 */
void inode_close(inode* inode) {
	intr_status old_status = intr_disable();
	if (--inode->i_open_cnts == 0) {
		list_remove(&inode->inode_tag);
		// TODO: 同理，为什么要修改下面的内容，按理说不需要修改
		task_struct* cur = running_thread();
		uint32_t* cur_pagedir_bak = cur->pgdir;
		cur->pgdir = NULL;
		sys_free(inode);
		cur->pgdir = cur_pagedir_bak;
	}
	intr_set_status(old_status);
}

/* 初始化一个 inode */
void inode_init(uint32_t inode_no, inode* new_inode) {
	new_inode->i_no = inode_no;
	new_inode->i_size = 0;
	new_inode->i_open_cnts = 0;
	new_inode->write_deny = 0;

	for (int i=0; i<13; i++) {
		new_inode->i_sectors[i] = 0;
	}
}

/* 回收 inode 的数据块和 inode 本身 */
void inode_release(partition* part, uint32_t inode_no) {
	inode* inode_to_del = inode_open(part, inode_no);
	ASSERT(inode_to_del->i_no == inode_no);

/* 回收 inode 的数据块 */
	uint8_t block_idx = 0, block_cnt = 12;
	uint32_t block_bitmap_idx;
	uint32_t all_blocks[140] = {0};

	while (block_idx < 12) {
		all_blocks[block_idx] = inode_to_del->i_sectors[block_idx];
		block_idx++;
	}
/* 如果一级间接表存在，读入其条目并清理其本身所占的空间 */
	if (inode_to_del->i_sectors[12] != 0) {
		ide_read(part->my_disk, inode_to_del->i_sectors[12], all_blocks+12, 1);
		block_cnt = 140;

		// 回收一级间接块表占用的扇区
		block_bitmap_idx =\
		inode_to_del->i_sectors[12] - part->sb->data_start_lba;
		ASSERT(block_bitmap_idx > 0);
		bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
		bitmap_sync(part, block_bitmap_idx, BLOCK_BITMAP);
	}
/* 逐个回收扇区们 */
	block_idx = 0;
	while (block_idx < block_cnt) {
		if (all_blocks[block_idx] != 0) {
			block_bitmap_idx = all_blocks[block_idx] - part->sb->data_start_lba;
			ASSERT(block_bitmap_idx > 0);
			bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
			bitmap_sync(part, block_bitmap_idx, BLOCK_BITMAP);
		}
		block_idx++;
	}

	bitmap_set(&part->inode_bitmap, inode_no, 0);
	bitmap_sync(part, inode_no, INODE_BITMAP);

	inode_close(inode_to_del);
}