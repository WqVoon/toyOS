#include "thread.h"
#include "string.h"
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
		if (file_table[idx].fd_inode == NULL) {
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

extern partition* cur_part;
/* 创建文件，若成功则返回文件描述符，否则返回 -1 */
int32_t file_create(dir* parent_dir, char* filename, uint8_t flag) {
	void* io_buf = sys_malloc(1024);
	if (io_buf == NULL) {
		printk("in file_create: sys_malloc for io_buf failed\n");
		return -1;
	}

	// 用于操作失败时回滚各资源状态
	uint8_t rollback_step = 0;

	int32_t inode_no = inode_bitmap_alloc(cur_part);
	if (inode_no == -1) {
		printk("in file_create: allocate inode failed\n");
		return -1;
	}

	inode* new_file_inode = sys_malloc(sizeof(inode));
	if (new_file_inode == NULL) {
		printk("file_create: sys_malloc for inode failed\n");
		rollback_step = 1;
		goto rollback;
	}
	// 初始化这个新的 inode
	inode_init(inode_no, new_file_inode);

	// 获取 file_table 中空闲的下标
	int fd_idx = get_free_slot_in_global();
	if (fd_idx == -1) {
		rollback_step = 2;
		goto rollback;
	}

	file_table[fd_idx].fd_inode = new_file_inode;
	file_table[fd_idx].fd_pos = 0;
	file_table[fd_idx].fd_flag = flag;
	file_table[fd_idx].fd_inode->write_deny = 0;

	dir_entry new_dir_entry;
	memset(&new_dir_entry, 0, sizeof(dir_entry));
	create_dir_entry(filename, inode_no, FT_REGULAR, &new_dir_entry);

	// 在目录 parent_dir 下安装目录项
	if (! sync_dir_entry(parent_dir, &new_dir_entry, io_buf)) {
		printk("sync dir_entry to disk failed\n");
		rollback_step = 3;
		goto rollback;
	}

	// 将父目录 inode 的内容同步到硬盘
	memset(io_buf, 0, 1024);
	inode_sync(cur_part, parent_dir->inode, io_buf);

	// 将新创建的 inode 结点同步到硬盘
	memset(io_buf, 0, 1024);
	inode_sync(cur_part, new_file_inode, io_buf);

	// 将 inode_bitmap 同步到硬盘
	bitmap_sync(cur_part, inode_no, INODE_BITMAP);

	// 将创建的文件 inode 添加到 open_inodes 链表
	list_push(&cur_part->open_inodes, &new_file_inode->inode_tag);

	sys_free(io_buf);
	// TODO: 这里不会只安装在内核线程中吗
	return pcb_fd_install(fd_idx);

rollback:
	switch (rollback_step) {
	case 3:
		memset(&file_table[fd_idx], 0, sizeof(file));
	case 2:
		sys_free(new_file_inode);
	case 1:
		bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
		break;
	}
	sys_free(io_buf);
	return -1;
}

/* 打开编号为 inode_no 的 inode 对应的文件，成功返回描述符，否则返回 -1 */
int32_t file_open(uint32_t inode_no, uint8_t flag) {
	int fd_idx = get_free_slot_in_global();
	if (fd_idx == -1) {
		printk("exceed max open files\n");
		return -1;
	}

	file_table[fd_idx].fd_inode = inode_open(cur_part, inode_no);
	file_table[fd_idx].fd_pos = 0;
	file_table[fd_idx].fd_flag = flag;
	bool* write_deny = &file_table[fd_idx].fd_inode->write_deny;

	if (flag & O_WRONLY || flag & O_RDWR) {
		// 只要有可能写文件，就考虑是否有其他进程也在写
		intr_status old_status = intr_disable();
		if (! (*write_deny)) {
			*write_deny = 1;
			intr_set_status(old_status);
		} else {
			intr_set_status(old_status);
			printk("file can not be write now, try again later\n");
			return -1;
		}
	}
	return pcb_fd_install(fd_idx);
}