#include "string.h"
#include "memory.h"
#include "stdio.h"
#include "file.h"
#include "ide.h"
#include "dir.h"

// 根目录
dir root_dir;

/* 打开根目录 */
void open_root_dir(partition* part) {
	root_dir.inode = inode_open(part, part->sb->root_inode_no);
	root_dir.dir_pos = 0;
}

/* 在分区 part 上打开 inode_no 对应的目录并返回其指针 */
dir* dir_open(partition* part, uint32_t inode_no) {
	dir* pdir = sys_malloc(sizeof(dir));
	pdir->inode = inode_open(part, inode_no);
	pdir->dir_pos = 0;
	return pdir;
}

/* 在分区 part 中的 pdir 目录内寻找名为 name 的目录项，找到会赋值给 dir_e 并返回 1，否则返回 0*/
bool search_dir_entry(
	partition* part, dir* pdir,
	const char* name, dir_entry* dir_e
) {
	// 12 个直接块 + 128 个一级间接块 = 140 块
	uint32_t block_cnt = 140;
	uint32_t* all_blocks = (uint32_t*)sys_malloc(block_cnt * 4);
	if (all_blocks == NULL) {
		printk("search_dir_entry: sys_malloc for all blocks failed");
		return 0;
	}

	uint32_t block_idx = 0;
	while (block_idx < 12) {
		all_blocks[block_idx] = pdir->inode->i_sectors[block_idx];
		block_idx++;
	}
	block_idx = 0;

	if (pdir->inode->i_sectors[12] != 0) {
		ide_read(
			part->my_disk,
			pdir->inode->i_sectors[12],
			all_blocks + 12, 1
		);
	}
/* 到此为止，all_blocks 储存了该文件或目录的所有扇区地址 */

	// TODO: 写目录项时保证了目录项不跨扇区
	uint8_t* buf = sys_malloc(SECTOR_SIZE);
	dir_entry* p_de = (dir_entry*)buf;
	uint32_t dir_entry_size = part->sb->dir_entry_size;
	uint32_t dir_entry_cnt = SECTOR_SIZE / dir_entry_size;

	while (block_idx < block_cnt) {
		// 如果块地址为 0 表示该块中无数据
		if (all_blocks[block_idx] == 0) {
			block_idx++; continue;
		}
		ide_read(part->my_disk, all_blocks[block_idx], buf, 1);

		// TODO: 如果一个扇区中没有写满目录项，那么剩下的部分会清零吗，否则为什么这里不做判断
		uint32_t dir_entry_idx = 0;
		while (dir_entry_idx < dir_entry_cnt) {
			if (! strcmp(p_de->filename, name)) {
				memcpy(dir_e, p_de, dir_entry_size);
				sys_free(buf);
				sys_free(all_blocks);
				return 1;
			}
			dir_entry_idx++; p_de++;
		}
		block_idx++;
		p_de = (dir_entry*)buf;
		memset(buf, 0, SECTOR_SIZE);
	}
	sys_free(buf);
	sys_free(all_blocks);
	return 0;
}

/* 关闭目录 */
void dir_close(dir* dir) {
	if (dir == &root_dir) {
		return;
	}
	inode_close(dir->inode);
	sys_free(dir);
}

/* 在内存中初始化目录项 p_de */
void create_dir_entry(
	char* filename, uint32_t inode_no,
	uint8_t file_type, dir_entry* p_de
) {
	ASSERT(strlen(filename) <= MAX_FILE_NAME_LEN);

	memcpy(p_de->filename, filename, strlen(filename));
	p_de->i_no = inode_no;
	p_de->f_type = file_type;
}

extern partition* cur_part;

/* 将目录项 p_de 写入父目录 parent_dir 中，io_buf 由主调函数提供 */
bool sync_dir_entry(dir* parent_dir, dir_entry* p_de, void* io_buf) {
	inode* dir_inode = parent_dir->inode;
	uint32_t dir_size = dir_inode->i_size;
	uint32_t dir_entry_size = cur_part->sb->dir_entry_size;

	// dir_size 应该是 dir_entry_size 的整数倍
	ASSERT(dir_size % dir_entry_size == 0);
	// 每个扇区最大的目录项数目，由于向下取整，因此保证了目录项不会跨扇区
	uint32_t dir_entrys_per_sec = (SECTOR_SIZE / dir_entry_size);
	int32_t block_lba = -1;
	// 用来存储所有的扇区地址
	uint8_t block_idx = 0;
	uint32_t all_blocks[140] = {0};

	// 将 12 个直接块存入 all_blocks
	// TODO: 是不是有 bug？如果下标 12 对应的一级间接表有值呢
	while (block_idx < 12) {
		all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
		block_idx++;
	}

	dir_entry* dir_e = (dir_entry*)io_buf;

	/*
	开始遍历所有块以寻找目录项的空位，若已有扇区中没有空闲位，
	在不超过文件大小的情况下申请新扇区来存储新目录项
	*/
	int32_t block_bitmap_idx = -1;
	block_idx = 0;
	while (block_idx < 140) {
		block_bitmap_idx = -1;
		// 如果为 0 说明该块当前为空，需要被分配
		if (all_blocks[block_idx] == 0) {
			// 先分配一个块，块的用途针对不同的 block_idx 而不同
			block_lba = block_bitmap_alloc(cur_part);
			if (block_lba == -1) {
				printk("alloc block bitmap for sync_dir_entry failed\n");
				return 0;
			}

			// 每分配一个块就同步一次 block_bitmap
			block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
			ASSERT(block_bitmap_idx >= 0);
			bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

			block_bitmap_idx = -1;
			if (block_idx < 12) {
				// 如果是直接块
				dir_inode->i_sectors[block_idx] =\
				all_blocks[block_idx] = block_lba;
			} else if (block_idx == 12) {
				// 如果是一级间接块表，那么由于上面的 all_blocks[block_idx] == 0，可知当前表空间未分配
				dir_inode->i_sectors[12] = block_lba;

				// 再分配一块用于作为一级间接块表的表项
				block_lba = -1;
				block_lba = block_bitmap_alloc(cur_part);
				if (block_lba == -1) {
					// 如果这个时候分配失败了，回收上面块表的分配的块
					block_bitmap_idx = dir_inode->i_sectors[12] -\
					cur_part->sb->data_start_lba;
					bitmap_set(&cur_part->block_bitmap, block_bitmap_idx, 0);
					dir_inode->i_sectors[12] = 0;
					printk("alloc block bitmap for sync_dir_entry failed\n");
					return 0;
				}

				// 每分配一个块就同步一次 block_bitmap
				block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
				ASSERT(block_bitmap_idx >= 0);
				bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

				all_blocks[12] = block_lba;
				// 把新分配的第 0 个间接块地址写入一级间接表
				ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
			} else {
				// 如果是间接块
				all_blocks[block_idx] = block_lba;
				// 把新分配的第 (block_idx - 12) 个间接块地址写入一级间接表
				ide_write(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks + 12, 1);
			}

			// 再将新目录项写入新分配的间接块
			memset(io_buf, 0, 512);
			memcpy(io_buf, p_de, dir_entry_size);
			ide_write(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
			dir_inode->i_size += dir_entry_size;
			return 1;
		}

		// 若第 block_idx 块已经存在，则将其读入内存，然后在该块中查找空目录项
		ide_read(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
		// 在扇区内查找空目录项
		uint8_t dir_entry_idx = 0;
		while (dir_entry_idx < dir_entrys_per_sec) {
			if ((dir_e + dir_entry_idx)->f_type == FT_UNKNOWN) {
				// 无论是初始化还是删除文件后，都会将 f_type 置为 FT_UNKNOWN
				memcpy(dir_e + dir_entry_idx, p_de, dir_entry_size);
				ide_write(cur_part->my_disk, all_blocks[block_idx], io_buf, 1);
				dir_inode->i_size += dir_entry_size;
				return 1;
			}
			dir_entry_idx++;
		}
		block_idx++;
	}
	printk("directory is full\n");
	return 0;
}

/* 读取目录，成功返回 1 个目录项，失败返回 NULL */
dir_entry* dir_read(dir* dir) {
	dir_entry* dir_e = (dir_entry*)dir->dir_buf;
	inode* dir_inode = dir->inode;
	uint32_t all_blocks[140] = {0}, block_cnt = 12;
	uint32_t block_idx = 0, dir_entry_idx = 0;
	// 根据 i_sectors 的状态读出目录所占的所有块
	while (block_idx < 12) {
		all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
		block_idx++;
	}
	if (dir_inode->i_sectors[12] != 0) {
		ide_read(cur_part->my_disk, dir_inode->i_sectors[12], all_blocks+12, 1);
		block_cnt = 140;
	}
	block_idx = 0;

	uint32_t cur_dir_entry_pos = 0;
	uint32_t dir_entry_size = cur_part->sb->dir_entry_size;
	uint32_t dir_entrys_per_sec = SECTOR_SIZE / dir_entry_size;
	while (dir->dir_pos < dir_inode->i_size) {
		if (dir->dir_pos >= dir_inode->i_size) {
			return NULL;
		}
		if (all_blocks[block_idx] == 0) {
			block_idx++;
			continue;
		}
		memset(dir_e, 0, SECTOR_SIZE);
		ide_read(cur_part->my_disk, all_blocks[block_idx], dir_e, 1);
		dir_entry_idx = 0;
		while (dir_entry_idx < dir_entrys_per_sec) {
			if ((dir_e + dir_entry_idx)->f_type) {
				// 跳过已经读取过的目录项
				if (cur_dir_entry_pos < dir->dir_pos) {
					cur_dir_entry_pos += dir_entry_size;
					dir_entry_idx++;
					continue;
				}
				ASSERT(cur_dir_entry_pos == dir->dir_pos);
				dir->dir_pos += dir_entry_size;
				return dir_e + dir_entry_idx;
			}
			dir_entry_idx++;
		}
		block_idx++;
	}
	return NULL;
}

/* dir_read 的封装 */
dir_entry* sys_readdir(dir* dir) {
	ASSERT(dir != NULL);
	return dir_read(dir);
}

/* 把目录 dir 的指针 dir_pos 置为 0 */
void sys_rewinddir(dir* dir) {
	dir->dir_pos = 0;
}

/* 把分区 part 目录 pdir 中编号为 inode_no 的目录项删除 */
bool delete_dir_entry(partition* part, dir* pdir, uint32_t inode_no, void* io_buf) {
	inode* dir_inode = pdir->inode;
	uint32_t block_idx = 0, all_blocks[140] = {0}, block_cnt = 12;
	while (block_idx < 12) {
		all_blocks[block_idx] = dir_inode->i_sectors[block_idx];
		block_idx++;
	}
	if (dir_inode->i_sectors[12]) {
		ide_read(part->my_disk, dir_inode->i_sectors[12], all_blocks+12, 1);
		block_cnt = 140;
	}

	// 目录项不会跨扇区
	uint32_t dir_entry_size = part->sb->dir_entry_size;
	uint32_t dir_entrys_per_sec = (SECTOR_SIZE / dir_entry_size);

	dir_entry* dir_e = (dir_entry*) io_buf;
	dir_entry* dir_entry_found = NULL;
	uint8_t dir_entry_idx, dir_entry_cnt;
	bool is_dir_first_block = 0; // 目录的第一个块

	block_idx = 0;
	while (block_idx < block_cnt) {
		is_dir_first_block = 0;
		if (all_blocks[block_idx] == 0) {
			block_idx++;
			continue;
		}

		dir_entry_idx = dir_entry_cnt = 0;
		memset(io_buf, 0, SECTOR_SIZE);
		ide_read(part->my_disk, all_blocks[block_idx], io_buf, 1);

		while (dir_entry_idx < dir_entrys_per_sec) {
			if ((dir_e + dir_entry_idx)->f_type != FT_UNKNOWN) {
				if (! strcmp((dir_e + dir_entry_idx)->filename, ".")) {
					is_dir_first_block = 1;
				} else if (
					strcmp((dir_e + dir_entry_idx)->filename, ".")
					&& strcmp((dir_e + dir_entry_idx)->filename, "..")
				) {
					dir_entry_cnt++;
					if ((dir_e + dir_entry_idx)->i_no == inode_no) {
						dir_entry_found = dir_e + dir_entry_idx;
					}
				}
			}
			dir_entry_idx++;
		}
		// 如果本扇区中没有找到，继续寻找下一个扇区
		if (dir_entry_found == NULL) {
			block_idx++;
			continue;
		}

		ASSERT(dir_entry_cnt >= 1);
		// 除目录第一个扇区外，若该扇区上只有待删除的目录项，那么将整个扇区回收
		if (dir_entry_cnt == 1 && !is_dir_first_block) {
			uint32_t block_bitmap_idx =\
			all_blocks[block_idx] - part->sb->data_start_lba;
			bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
			bitmap_sync(part, block_bitmap_idx, BLOCK_BITMAP);

			if (block_idx < 12) {
				dir_inode->i_sectors[block_idx] = 0;
			} else {
				// 如果间接索引表中只有这一个间接块，那么回收索引表
				uint32_t indirect_blocks = 0;
				uint32_t indirect_block_idx = 12;
				while (indirect_block_idx < 140) {
					if (all_blocks[indirect_block_idx] != 0) {
						indirect_blocks++;
					}
				}
				ASSERT(indirect_blocks >= 1);

				if (indirect_blocks > 1) {
					// 若索引表中还有其他间接块，那么仅在索引表中擦除这个地址
					all_blocks[block_idx] = 0;
					ide_write(part->my_disk, dir_inode->i_sectors[12], all_blocks+12, 1);
				} else {
					//TODO: 这里不先将索引块清零再回收吗？
					// 否则回收整个索引表
					block_bitmap_idx =\
					dir_inode->i_sectors[12] - part->sb->data_start_lba;
					bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
					bitmap_sync(part, block_bitmap_idx, BLOCK_BITMAP);
					dir_inode->i_sectors[12] = 0;
				}
			}
		} else { // 仅将该目录项清空
			memset(dir_entry_found, 0, dir_entry_size);
			ide_write(part->my_disk, all_blocks[block_idx], io_buf, 1);
		}

		ASSERT(dir_inode->i_size >= dir_entry_size);
		dir_inode->i_size -= dir_entry_size;
		memset(io_buf, 0, SECTOR_SIZE*2);
		inode_sync(part, dir_inode, io_buf);

		return 1;
	}
	return 0;
}