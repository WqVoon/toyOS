#include "thread.h"
#include "stdio.h"
#include "file.h"
#include "ide.h"
#include "fs.h"

/* 文件表，因同一文件可被打开多次，故里面有可能有相同的文件 */
file file_table[MAX_FILE_OPEN];

/* 从文件表 file_table 中获取一个空闲位，成功返回下标，失败返回 -1 */
int32_t get_free_slot_in_global(void) {
	// 前三个已经被占用了
	int idx = 3;
	for (; idx < MAX_FILE_OPEN; idx++) {
		if (file_table[idx].fd_inode = NULL) {
			break;
		}
	}

	if (idx == MAX_FILE_OPEN) {
		printk("exceed max open files\n");
		return -1;
	}
	return idx;
}

/* 将全局描述符下标安装到进程或线程自己的文件描述符数组 fd_table 中 */
int32_t pcb_fd_install(int32_t globa_fd_idx) {
	task_struct* cur = running_thread();
	int idx = 3;
	for (; idx < MAX_FILES_OPEN_PER_PROC; idx++) {
		if (cur->fd_table[idx] == -1) {
			cur->fd_table[idx] = globa_fd_idx;
			break;
		}
	}

	if (idx == MAX_FILES_OPEN_PER_PROC) {
		printk("exceed max open files_per_proc\n");
		return -1;
	}
	return idx;
}

/* 分配一个 i 结点，返回 i 结点号 */
int32_t inode_bitmap_alloc(partition* part) {
	int32_t bit_idx = bitmap_scan(&part->inode_bitmap, 1);
	if (bit_idx == -1) {
		return -1;
	}
	bitmap_set(&part->inode_bitmap, bit_idx, 1);
	return bit_idx;
}

/* 分配一个空闲块，返回其扇区地址 */
int32_t block_bitmap_alloc(partition* part) {
	int32_t bit_idx = bitmap_scan(&part->block_bitmap, 1);
	if (bit_idx == -1) {
		return -1;
	}
	bitmap_set(&part->block_bitmap, bit_idx, 1);
	return (part->sb->data_start_lba + bit_idx);
}

/* 将内存中 bitmap 第 bit_idx 位所在的 512 字节同步到硬盘 */
void bitmap_sync(partition* part, uint32_t bit_idx, bitmap_type btmp) {
	// 计算本结点索引所在扇区相对于位图的扇区偏移量
	uint32_t off_sec = bit_idx / BITS_PER_SECTOR;
	// 计算本结点索引所在扇区相对于位图的字节偏移量
	uint32_t off_size = off_sec * BLOCK_SIZE;
	uint32_t sec_lba;
	uint8_t* bitmap_off;

	switch (btmp) {
	case INODE_BITMAP:
		sec_lba = part->sb->inode_bitmap_lba + off_sec;
		bitmap_off = part->inode_bitmap.bits + off_size;
		break;
	case BLOCK_BITMAP:
		sec_lba = part->sb->block_bitmap_lba + off_sec;
		bitmap_off = part->block_bitmap.bits + off_size;
		break;
	}

	ide_write(part->my_disk, sec_lba, bitmap_off, 1);
}