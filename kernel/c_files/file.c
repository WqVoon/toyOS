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
	if (flag & O_WRONLY || flag & O_RDWR) {
		// 只要有可能写文件，就考虑是否有其他进程也在写
		intr_status old_status = intr_disable();
		file_table[fd_idx].fd_inode->write_deny = 1;
		intr_set_status(old_status);
	}

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

/* 关闭文件，成功返回 0，失败返回 -1 */
int32_t file_close(file* file) {
	if (file == NULL) {
		return -1;
	}
	/*
	由于 file_open 保证了同一时刻一个文件只可能出现一个对应的可写 file* 结构
	因此这里不需要关中断来保证原子操作
	*/
	if (file->fd_flag & O_WRONLY || file->fd_flag & O_RDWR) {
		file->fd_inode->write_deny = 0;
	}
	inode_close(file->fd_inode);
	// 这里使 file_table 对应的项目可用
	file->fd_inode = NULL;
	return 0;
}

/* 把 buf 中的 count 个字节写入 file，成功返回写入的字节数，失败返回 -1 */
int32_t file_write(file* file, const void*buf, uint32_t count) {
	// TODO: 如果 count 等于 uint32_t 的最大值不是也会返回 -1 吗

	if ((file->fd_inode->i_size + count) > (BLOCK_SIZE * 140)) {
		// 文件目前最大只支持 512*140=71680 字节
		printk("exceed max file_size 71680 bytes, write file failed\n");
		return -1;
	}

	uint8_t* io_buf = sys_malloc(512);
	if (io_buf == NULL) {
		printk("file_write: sys_malloc for io_buf failed\n");
		return -1;
	}

	// 用来记录文件所有的块地址
	uint32_t* all_blocks = (uint32_t*)sys_malloc(BLOCK_SIZE + 48);
	if (all_blocks == NULL) {
		printk("file_write: sys_malloc for all_blocks failed\n");
		return -1;
	}

	// 用 src 指向 buf 中待写入的数据
	const uint8_t* src = buf;
	// 用来记录已写入数据大小
	uint32_t bytes_written = 0;
	// 用来记录未写入数据大小
	uint32_t size_left = count;
	// 块地址
	int32_t block_lba = -1;
	// 用来记录 block 对应于 block_bitmap 中的索引，作为参数传给 bitmap_sync
	uint32_t block_bitmap_idx = 0;

	// 用来索引扇区
	uint32_t sec_idx;
	// 扇区地址
	uint32_t sec_lba;
	// 扇区内字节偏移量
	uint32_t sec_off_bytes;
	// 扇区内剩余字节量
	uint32_t sec_left_bytes;
	// 每次写入硬盘的数据块大小
	uint32_t chunk_size;
	// 用来获取一级间接表地址
	int32_t indirect_block_table;
	// 块索引
	uint32_t block_idx;

	/* 判断文件是否是第一次写，如果是，先为其分配一个块 */
	if (file->fd_inode->i_sectors[0] == 0) {
		block_lba = block_bitmap_alloc(cur_part);
		if (block_lba == -1) {
			printk("file_write: block_bitmap_alloc failed\n");
			return -1;
		}

		file->fd_inode->i_sectors[0] = block_lba;
		/* 每分配一个块就将位图同步到硬盘 */
		block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
		ASSERT(block_bitmap_idx >= 0);
		bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
	}

	/*
	写入 count 个字节前，该文件已经占用的块数
	TODO: 为什么不用向上取整除，这个 +1 是否有问题
	*/
	uint32_t file_has_used_blocks = file->fd_inode->i_size / BLOCK_SIZE + 1;

	/*
	存储 count 字节后该文件将占用的块数
	TODO: 同上
	*/
	uint32_t file_will_use_blocks =\
	(file->fd_inode->i_size + count) / BLOCK_SIZE + 1;
	ASSERT(file_will_use_blocks <= 140);

	/* 通过此增量判断是否需要分配扇区，如增量为 0，表示原扇区够用 */
	uint32_t add_blocks = file_will_use_blocks - file_has_used_blocks;
	/*
	将写文件所用到的块地址收集到 all_blocks，系统中块大小等于扇区大小，
	后面都统一在 all_blocks 中获取写入扇区地址
	*/
	if (add_blocks == 0) {
		/* 在同一扇区内写入数据，不涉及到分配新扇区 */
		if (file_will_use_blocks <= 12 ) { // 文件数据量将在 12 块之内
			// 指向最后一个已有数据的扇区
			block_idx = file_has_used_blocks - 1;
			all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
		} else {
			/* 未写入新数据之前已经占用了间接块，需要将间接块地址读进来 */
			ASSERT(file->fd_inode->i_sectors[12] != 0);
			indirect_block_table = file->fd_inode->i_sectors[12];
			ide_read(
				cur_part->my_disk, indirect_block_table,
				all_blocks + 12, 1
			);
		}
	}else{
/* 若有增量，便涉及到分配新扇区及是否分配一级间接块表， 下面要分三种情况处理 */
/* 第一种情况:12 个直接块够用*/
		if (file_will_use_blocks <= 12) {
			/* 先将有剩余空间的可继续用的扇区地址写入 all_blocks */
			block_idx = file_has_used_blocks - 1;
			ASSERT(file->fd_inode->i_sectors[block_idx] != 0);
			all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];

			/* 再将未来要用的扇区分配好后写入 all_blocks */
			block_idx = file_has_used_blocks; // 指向第一个要分配的新扇区
			while (block_idx < file_will_use_blocks) {
				block_lba = block_bitmap_alloc(cur_part);
				if (block_lba == -1) {
					printk("file_write: block_bitmap_alloc for situation 1 failed\n");
					return -1;
				}

				/* 写文件时，不应该存在块未使用，但已经分配扇区的情况， 当文件删除时，就会把块地址清0 */
				ASSERT(file->fd_inode->i_sectors[block_idx] == 0); // 确保尚未分配扇区地址
				file->fd_inode->i_sectors[block_idx] = \
				all_blocks[block_idx] = block_lba;

				/* 每分配一个块就将位图同步到硬盘 */
				block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
				bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
				block_idx++; // 下一个分配的新扇区
			}
		} else if (file_has_used_blocks <= 12 && file_will_use_blocks > 12) {
/* 第二种情况: 旧数据在 12 个直接块内，新数据将使用间接块*/

			/* 先将有剩余空间的可继续用的扇区地址收集到 all_blocks */
			block_idx = file_has_used_blocks - 1;

			// 指向旧数据所在的最后一个扇区
			all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];

			/* 创建一级间接块表 */
			block_lba = block_bitmap_alloc(cur_part);
			if (block_lba == -1) {
				printk("file_write: block_bitmap_alloc for situation 2 failed\n");
				return -1;
			}
			ASSERT(file->fd_inode->i_sectors[12] == 0); // 确保一级间接块表未分配

			/* 分配一级间接块索引表 */
			indirect_block_table = file->fd_inode->i_sectors[12] = block_lba;

			block_idx = file_has_used_blocks; // 第一个未使用的块，即本文件最后一个已经使用的直接块的下一块
			while (block_idx < file_will_use_blocks) {
				block_lba = block_bitmap_alloc(cur_part);
				if (block_lba == -1) {
					printk("file_write: block_bitmap_alloc for situation 2 failed\n");
					return -1;
				}

				if (block_idx < 12) { // 新创建的 0~11 块直接存入 all_blocks 数组
					ASSERT(file->fd_inode->i_sectors[block_idx] == 0); // 确保尚未分配扇区地址
					file->fd_inode->i_sectors[block_idx] = \
					all_blocks[block_idx] = block_lba;
				} else {
					// 间接块只写入到 all_block 数组中，待全部分配完成后一次性同步到硬盘
					all_blocks[block_idx] = block_lba;
				}

				/* 每分配一个块就将位图同步到硬盘 */
				block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
				bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
				block_idx++; // 下一个新扇区
			}
			ide_write(
				cur_part->my_disk, indirect_block_table,
				all_blocks + 12, 1
			); // 同步一级间接块表到硬盘
		} else if (file_has_used_blocks > 12) {
/* 第三种情况:新数据占据间接块*/

			ASSERT(file->fd_inode->i_sectors[12] != 0); // 已经具备了一级间接块表
			indirect_block_table = file->fd_inode->i_sectors[12]; // 获取一级间接表地址

			/* 已使用的间接块也将被读入 all_blocks，无需单独收录 */
			ide_read(
				cur_part->my_disk, indirect_block_table,
				all_blocks + 12, 1
			); // 获取所有间接块地址

			block_idx = file_has_used_blocks; // 第一个未使用的间接块，即已经使用的间接块的下一块
			while (block_idx < file_will_use_blocks) {
				block_lba = block_bitmap_alloc(cur_part);
				if (block_lba == -1) {
					printk("file_write: block_bitmap_alloc for situation 3 failed\n");
					return -1;
				}
				all_blocks[block_idx++] = block_lba;
				/* 每分配一个块就将位图同步到硬盘 */
				block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
				bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
			}

			ide_write(
				cur_part->my_disk, indirect_block_table,
				all_blocks + 12, 1
			); // 同步一级间接块表到硬盘
		}
	}

/* 用到的块地址已经收集到 all_blocks 中，下面开始写数据 */
	bool first_write_block = 1; // 含有剩余空间的块标识
	file->fd_pos = file->fd_inode->i_size - 1; // 置 fd_pos 为文件大小-1，下面在写数据时随时更新
	while (bytes_written < count) { // 直到写完所有数据
		memset(io_buf, 0, BLOCK_SIZE);
		sec_idx = file->fd_inode->i_size / BLOCK_SIZE;
		sec_lba = all_blocks[sec_idx];
		sec_off_bytes = file->fd_inode->i_size % BLOCK_SIZE;
		sec_left_bytes = BLOCK_SIZE - sec_off_bytes;

		/* 判断此次写入硬盘的数据大小 */
		chunk_size = size_left < sec_left_bytes
		 ? size_left
		 : sec_left_bytes;

		if (first_write_block) {
			ide_read(cur_part->my_disk, sec_lba, io_buf, 1);
			first_write_block = 0;
		}

		memcpy(io_buf + sec_off_bytes, src, chunk_size);
		ide_write(cur_part->my_disk, sec_lba, io_buf, 1);
		printk("file write at lba 0x%x\n", sec_lba); // 调试，完成后去掉

		src += chunk_size; // 将指针推移到下个新数据
		file->fd_inode->i_size += chunk_size; // 更新文件大小
		file->fd_pos += chunk_size;
		bytes_written += chunk_size;
		size_left -= chunk_size;
	}

	inode_sync(cur_part, file->fd_inode, io_buf);
	sys_free(all_blocks);
	sys_free(io_buf);
	return bytes_written;
}