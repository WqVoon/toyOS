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
}

extern struct list partition_list;

/* 在磁盘上搜索文件系统，若没有则格式化分区来创建之 */
void filesys_init() {
	printk("searching filesystem...\n");
	list_traversal(&partition_list, for_each_partition, 0);
}