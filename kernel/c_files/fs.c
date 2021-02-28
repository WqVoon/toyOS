#include "super_block.h"
#include "string.h"
#include "memory.h"
#include "debug.h"
#include "stdio.h"
#include "inode.h"
#include "list.h"
#include "ide.h"
#include "dir.h"
#include "fs.h"

/*
格式化分区，也就是初始化分区的元信息，创建文件系统
分区格式如下:
	引导块，超级块，空闲块位图，inode 位图，inode 数组，根目录，空闲块
*/
static void partition_format(partition* part) {
	printk("%s format start\n", part->name);
	uint32_t boot_sector_sects = 1;
	uint32_t super_block_sects = 1;
	// inode 位图占用的扇区数，最多支持 4096 个文件
	uint32_t inode_bitmap_sects = DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_SECTOR);
	// inode 数组占用的扇区数
	uint32_t inode_table_sects = DIV_ROUND_UP(\
		(sizeof(inode) * MAX_FILES_PER_PART),
		SECTOR_SIZE
	);
	uint32_t used_sects = boot_sector_sects + super_block_sects +\
	inode_bitmap_sects + inode_bitmap_sects;
	uint32_t free_sects = part->sec_cnt - used_sects;
	ASSERT(free_sects >= 0);

	uint32_t block_bitmap_sects;
	block_bitmap_sects = DIV_ROUND_UP(free_sects, BITS_PER_SECTOR);
	uint32_t block_bitmap_bit_len = free_sects - block_bitmap_sects;
	block_bitmap_sects = DIV_ROUND_UP(block_bitmap_bit_len, BITS_PER_SECTOR);

	struct super_block sb;
	sb.magic = *((uint32_t*) "iLym");
	sb.sec_cnt = part->sec_cnt;
	sb.inode_cnt = MAX_FILES_PER_PART;
	sb.part_lba_base = part->start_lba;

	// 第 0 块是引导块，第 1 块是超级块
	sb.block_bitmap_lba = sb.part_lba_base + 2;
	sb.block_bitmap_sects = block_bitmap_sects;

	sb.inode_bitmap_lba = sb.block_bitmap_lba + sb.block_bitmap_sects;
	sb.inode_bitmap_sects = inode_bitmap_sects;

	sb.inode_table_lba = sb.inode_bitmap_lba + sb.inode_bitmap_sects;
	sb.inode_table_sects = inode_table_sects;

	sb.data_start_lba = sb.inode_table_lba + sb.inode_table_sects;
	sb.root_inode_no = 0;
	sb.dir_entry_size = sizeof(dir_entry);

	printk(
		"%s info:\n"
		"   magic:                0x%x\n"
		"   part_lba_base:        0x%x\n"
		"   all_sectors:          0x%x\n"
		"   inode_cnt:            0x%x\n"
		"   block_bitmap_lba:     0x%x\n"
		"   block_bitmap_sectors: 0x%x\n"
		"   inode_bitmap_lba:     0x%x\n"
		"   inode_bitmap_sectors: 0x%x\n"
		"   inode_table_lba:      0x%x\n"
		"   inode_table_sectors:  0x%x\n"
		"   data_start_lba:       0x%x\n",
		part->name, sb.magic, sb.part_lba_base,
		sb.sec_cnt, sb.inode_cnt, sb.block_bitmap_lba,
		sb.block_bitmap_sects, sb.inode_bitmap_lba,
		sb.inode_bitmap_sects, sb.inode_table_lba,
		sb.inode_table_sects, sb.data_start_lba
	);

	disk* hd = part->my_disk;
/* 先把超级块写入本分区的 1 扇区 */
	ide_write(hd, part->start_lba + 1, &sb, 1);
	printk("   super_block_lba:      0x%x\n", part->start_lba + 1);

	// 找出数据量最大的元信息，用其尺寸做存储缓冲区
	uint32_t buf_size = (
		sb.block_bitmap_sects >= sb.inode_bitmap_sects\
		? sb.block_bitmap_sects
		: sb.inode_bitmap_sects
	);
	buf_size = (
		buf_size >= sb.inode_table_sects\
		? buf_size
		: sb.inode_table_sects
	);

	uint8_t* buf = (uint8_t*) sys_malloc(buf_size * SECTOR_SIZE);

/* 将块位图初始化并写入 sb.block_bitmap_lba */
	buf[0] |= 0x01; // 第 0 个块预留给根目录，位图中占位
	uint32_t block_bitmap_last_byte = block_bitmap_bit_len / 8;
	uint8_t block_bitmap_last_bit   = block_bitmap_bit_len % 8;
	// last_size 是位图最后一个扇区中，不足一扇区的部分
	uint32_t last_size = SECTOR_SIZE - (block_bitmap_last_byte % SECTOR_SIZE);
	// 将位图最后一字节到其所在扇区结束全置为 1

	memset(&buf[block_bitmap_last_byte], 0xff, last_size);

	// 再将上一步中覆盖的最后一字节内有效位重新置 0
	uint8_t bit_idx = 0;

	while (bit_idx <= block_bitmap_last_bit) {
		buf[block_bitmap_last_byte] &= ~(1 << bit_idx++);
	}

	ide_write(hd, sb.block_bitmap_lba, buf, sb.block_bitmap_sects);

/* 将 inode 位图初始化并写入 sb.inode_bitmap_lba */
	memset(buf, 0, buf_size);
	buf[0] |= 0x01;
	// inode 位图刚好占用 1 扇区
	ide_write(hd, sb.inode_bitmap_lba, buf, sb.inode_bitmap_sects);

/* 将 inode 数组初始化并写入 sb.inode_table_lba */
	memset(buf, 0, buf_size);
	// 准备填写根目录的 inode
	inode* i = (inode*) buf;
	i->i_size = sb.dir_entry_size * 2; // . 和 ..
	i->i_no = 0;
	i->i_sectors[0] = sb.data_start_lba;
	ide_write(hd, sb.inode_table_lba, buf, sb.inode_table_sects);

/* 将根目录写入 sb.data_start_lba */
	memset(buf, 0, buf_size);
	// 准备填写 . 的目录项
	dir_entry* p_de = (dir_entry*)buf;
	memcpy(p_de->filename, ".", 1);
	p_de->i_no = 0;
	p_de->f_type = FT_DIRECTORY;
	p_de++;

	// 准备填写 .. 的目录项，但根目录的父目录依然是其自己
	memcpy(p_de->filename, "..", 2);
	p_de->i_no = 0;
	p_de->f_type = FT_DIRECTORY;
	ide_write(hd, sb.data_start_lba, buf, 1);

	printk(
		"   root_dir_lba: 0x%x\n"
		"%s format done\n",
		sb.data_start_lba, part->name
	);
	sys_free(buf);
}

// TODO: 默认情况下操作的是哪个分区（有啥用？）
partition* cur_part;
// sys_malloc 返回失败的错误信息
const char* malloc_error = "malloc memory failed!";
/* 在分区链表中找到名为 part_name 的分区，并将其指针赋值给 cur_part */
static bool mount_partition(struct list_elem* pelem, int arg) {
	char* part_name = (char*) arg;
	partition* part = elem2entry(partition, part_tag, pelem);
	if (strcmp(part->name, part_name)) {
		// 返回 0 来使 list_traversal 继续扫描
		return 0;
	}

	cur_part = part;
	disk* hd = part->my_disk;

/* 处理超级块 */
	struct super_block* sb_buf = cur_part->sb = sys_malloc(SECTOR_SIZE);
	if (sb_buf == NULL) {
		ASSERT(! malloc_error);
	}
	ide_read(hd, cur_part->start_lba + 1, sb_buf, 1);

/* 处理块位图 */
	cur_part->block_bitmap.bits =\
	sys_malloc(sb_buf->block_bitmap_sects * SECTOR_SIZE);
	if (cur_part->block_bitmap.bits == NULL) {
		ASSERT(! malloc_error);
	}

	cur_part->block_bitmap.btmp_bytes_len =\
	sb_buf->block_bitmap_sects * SECTOR_SIZE;
	ide_read(
		hd, sb_buf->block_bitmap_lba,
		cur_part->block_bitmap.bits,
		sb_buf->block_bitmap_sects
	);

/* 处理 inode 位图 */
	cur_part->inode_bitmap.bits =\
	sys_malloc(sb_buf->inode_bitmap_sects * SECTOR_SIZE);
	if (cur_part->inode_bitmap.bits == NULL) {
		ASSERT(! malloc_error);
	}

	cur_part->inode_bitmap.btmp_bytes_len =\
	sb_buf->inode_bitmap_sects * SECTOR_SIZE;
	ide_read(
		hd, sb_buf->inode_bitmap_lba,
		cur_part->inode_bitmap.bits,
		sb_buf->inode_bitmap_sects
	);

	list_init(&cur_part->open_inodes);
	printk("mount %s done!\n", part->name);

	// 返回 1 来使 list_traversal 停止
	return 1;
}

/* 将最上层的路径名称解析出来，把名字保存在 name_store，并返回名字后面的位置 */
static char* path_parse(char* pathname, char* name_store) {
	if (pathname[0] == 0) return NULL;

	// 根目录不需要单独解析，所以跳过所有的前缀 '/'
	if (pathname[0] == '/') {
		while (*(++pathname) == '/');
	}

	// 开始解析一般路径
	while (*pathname != '/' && *pathname != 0) {
		*name_store++ = *pathname++;
	}

	return pathname;
}

/* 返回路径的深度，比如 /a/b/c 的深度为 3 */
uint32_t path_depth_cnt(char* pathname) {
	ASSERT(pathname != NULL);
	char*p = pathname;
	// 用于 path_parse 的参数做路径解析
	char name[MAX_FILE_NAME_LEN];

	uint32_t depth = 0;

	// 解析路径，从中拆分出各级名称
	path_parse(p, name);
	while (name[0]) {
		depth++;
		if (p) { // 如果 p 不等于 NULL，那么继续分析路径
			p = path_parse(p, name);
		}
	}
	return depth;
}

extern dir root_dir;
/* 搜索文件 pathname，若找到则返回其 inode 号，否则返回 -1 */
static int search_file(const char* pathname, path_search_record* searched_record) {
	// 如果待查找的是根目录，那么直接返回已知的根目录信息
	if (!strcmp(pathname, "/") || !strcmp(pathname, "/.") || !strcmp(pathname, "/..")) {
		searched_record->parent_dir = &root_dir;
		searched_record->file_type = FT_DIRECTORY;
		searched_record->searched_path[0] = 0;
		return 0;
	}

	uint32_t path_len = strlen(pathname);
	// 保证 pathname 至少是这样的路径 /x，且小于最大长度
	ASSERT(pathname[0] == '/' && path_len > 1 && path_len < MAX_PATH_LEN);
	char* sub_path = (char*)pathname;
	dir* parent_dir = &root_dir;
	dir_entry dir_e;

	// 记录路径解析出来的各级名称，如 /a/b/c 那么每次拆分出的值分别是 a, b, c
	char name[MAX_FILE_NAME_LEN] = {0};

	searched_record->parent_dir = parent_dir;
	searched_record->file_type = FT_UNKNOWN;
	uint32_t parent_inode_no = 0;

	sub_path = path_parse(sub_path, name);
	while (name[0]) {
		ASSERT(strlen(searched_record->searched_path) < MAX_PATH_LEN);

		//记录已经存在的父目录
		strcat(searched_record->searched_path, "/");
		strcat(searched_record->searched_path, name);

		if (search_dir_entry(cur_part, parent_dir, name, &dir_e)) {
			memset(name, 0, MAX_FILE_NAME_LEN);

			// 如果 sub_path 不为 NULL，那么继续拆分路径
			if (sub_path) {
				sub_path = path_parse(sub_path, name);
			}

			if (FT_DIRECTORY == dir_e.f_type) {
				// 如果被打开的是目录
				parent_inode_no = parent_dir->inode->i_no;
				dir_close(parent_dir);
				searched_record->parent_dir = parent_dir = dir_open(cur_part, dir_e.i_no);
			} else if (FT_REGULAR == dir_e.f_type) {
				// 如果是普通文件
				searched_record->file_type = FT_REGULAR;
				return dir_e.i_no;
			}
		} else {
			// TODO: 如果没找到那么直接返回 -1，但先不关闭 parent_dir，方便创建文件
			return -1;
		}
	}

	/*
	如果能执行到这里，那么说明遍历完了全部路径，并且查找的文件或目录只有同名目录存在
	此时 searched_record->parent_dir 是最后一级目录，所以需要将其更新成倒数第二级的目录
	*/
	dir_close(searched_record->parent_dir);

	searched_record->parent_dir = dir_open(cur_part, parent_inode_no);
	searched_record->file_type = FT_DIRECTORY;
	return dir_e.i_no;
}

static bool for_each_partition(struct list_elem* tag, int unused) {
	partition* part = elem2entry(partition, part_tag, tag);
	struct super_block sb_buf[1] = {0};

	// 读出超级块
	ide_read(part->my_disk, part->start_lba + 1, sb_buf, 1);

	// 只支持自建的文件系统
	if (sb_buf->magic == *((uint32_t*) "iLym")) {
		printk("%s has filesystem\n", part->name);
	} else {
		partition_format(part);
	}

	return 0;
}

extern struct list partition_list;

/* 在磁盘上搜索文件系统，若没有则格式化分区来创建之 */
void filesys_init() {
	printk("searching filesystem...\n");
	list_traversal(&partition_list, for_each_partition, 0);
	list_traversal(&partition_list, mount_partition, (int)"sdb1");
}