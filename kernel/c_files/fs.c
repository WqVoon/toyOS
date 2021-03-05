#include "super_block.h"
#include "keyboard.h"
#include "ioqueue.h"
#include "console.h"
#include "string.h"
#include "thread.h"
#include "memory.h"
#include "debug.h"
#include "stdio.h"
#include "inode.h"
#include "list.h"
#include "file.h"
#include "ide.h"
#include "dir.h"
#include "fs.h"

extern struct list partition_list;
extern file file_table[MAX_FILE_OPEN];

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
		"   magic:                %x\n"
		"   part_lba_base:        %x\n"
		"   all_sectors:          %x\n"
		"   inode_cnt:            %x\n"
		"   block_bitmap_lba:     %x\n"
		"   block_bitmap_sectors: %x\n"
		"   inode_bitmap_lba:     %x\n"
		"   inode_bitmap_sectors: %x\n"
		"   inode_table_lba:      %x\n"
		"   inode_table_sectors:  %x\n"
		"   data_start_lba:       %x\n",
		part->name, sb.magic, sb.part_lba_base,
		sb.sec_cnt, sb.inode_cnt, sb.block_bitmap_lba,
		sb.block_bitmap_sects, sb.inode_bitmap_lba,
		sb.inode_bitmap_sects, sb.inode_table_lba,
		sb.inode_table_sects, sb.data_start_lba
	);

	disk* hd = part->my_disk;
/* 先把超级块写入本分区的 1 扇区 */
	ide_write(hd, part->start_lba + 1, &sb, 1);
	printk("   super_block_lba:      %x\n", part->start_lba + 1);

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
		"   root_dir_lba: %x\n"
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
	ASSERT(pathname[0] == '/' && pathname != NULL);
	char*p = pathname;
	// 用于 path_parse 的参数做路径解析
	char name[MAX_FILE_NAME_LEN];

	uint32_t depth = 0;

	// 解析路径，从中拆分出各级名称
	p = path_parse(p, name);
	while (name[0]) {
		depth++;
		memset(name, 0, MAX_FILE_NAME_LEN);
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

/* 打开或创建文件成功后，返回文件描述符，否则返回 -1 */
int32_t sys_open(const char* pathname, uint8_t flags) {
	if (pathname[strlen(pathname)-1] == '/') {
		printk("can't open a directory %s\n", pathname);
		return -1;
	}
	ASSERT(flags <= 7);
	int32_t fd = -1;

	path_search_record searched_record;
	memset(&searched_record, 0, sizeof(path_search_record));

	uint32_t pathname_depth = path_depth_cnt((char*)pathname);

	int inode_no = search_file(pathname, &searched_record);
	bool found = (inode_no != -1);

	if (searched_record.file_type == FT_DIRECTORY) {
		printk("can't open a directory %s\n", pathname);
		dir_close(searched_record.parent_dir);
		return -1;
	}

	uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);

	if (pathname_depth != path_searched_depth) {
		printk(
			"can not access %s: Not a directory, subpath %s is't exist\n",
			pathname, searched_record.searched_path
		);
		dir_close(searched_record.parent_dir);
		return -1;
	}

	// 如果在最后一个路径没找到，并且不是要创建文件，那么直接返回 -1
	if (!found && !(flags & O_CREAT)) {
		printk(
			"in path %s, file %s is't exist\n",
			searched_record.searched_path,
			(strrchr(searched_record.searched_path, '/') + 1)
		);
		dir_close(searched_record.parent_dir);
		return -1;
	} else if (found && flags & O_CREAT) {
		printk("%s has already exist\n", pathname);
		dir_close(searched_record.parent_dir);
		return -1;
	}

	// TODO: 这个 switch 为什么不换成 if
	switch (flags & O_CREAT) {
	case O_CREAT:
		fd = file_create(searched_record.parent_dir, (strrchr(pathname, '/')+1), flags);
		dir_close(searched_record.parent_dir);
		break;
	// 其余为打开文件
	default:
		fd = file_open(inode_no, flags);
	}
	// 此 fd 是指任务 pcb->file_table 数组中的下标，而不是全局 file_table 的下标
	return fd;
}

/* 将文件描述符转换为全局文件表 file_table 的下标 */
static uint32_t fd_local2global(uint32_t local_fd) {
	task_struct* cur = running_thread();
	int32_t global_fd = cur->fd_table[local_fd];
	ASSERT(global_fd >= 0 && global_fd < MAX_FILE_OPEN);
	return (uint32_t)global_fd;
}

/* 关闭文件，成功返回 0，失败返回 */
int32_t sys_close(int32_t fd) {
	int32_t ret = -1;
	if (fd > 2) {
		uint32_t _fd = fd_local2global(fd);
		ret = file_close(&file_table[_fd]);
		// 使该文件描述符可用
		running_thread()->fd_table[fd] = -1;
	}
	return ret;
}

/* 将 buf 中连续 count 个字节写入描述符 fd，成功则返回写入的字节数，失败返回 -1 */
int32_t sys_write(int32_t fd, const void* buf, uint32_t count) {
	// TODO: 原书实现有 bug，stdin_no 和 stderr_no 都不该被写入
	if (fd < stdout_no || fd == stderr_no) {
		printk("sys_write: fd error\n");
		return -1;
	} else if (fd == stdout_no) {
		// TODO: 原书实现有 bug，count 完全可以大于 1024，这里为实现简单直接用 ASSERT 避免
		ASSERT(count <= 1023);
		char tmp_buf[1024] = {0};
		memcpy(tmp_buf, buf, count);
		printk(tmp_buf);
		return count;
	}

	uint32_t _fd = fd_local2global(fd);
	file* wr_file = &file_table[_fd];
	if (wr_file->fd_flag & O_WRONLY || wr_file->fd_flag & O_RDWR) {
		return file_write(wr_file, buf, count);
	} else {
		printk("sys_write: not allowed to write file without O_RDWR or O_WRONLY\n");
		return -1;
	}
}

/* 从文件描述符 fd 指向的文件中读取 count 个字节到 buf，成功返回字节数，失败返回 -1 */
int32_t sys_read(int32_t fd, void* buf, uint32_t count) {
	ASSERT(buf != NULL);

	if (fd < 0 || fd == stdout_no || fd == stderr_no) {
		printk("sys_read: fd error\n");
		return -1;
	} else if (fd == stdin_no) {
		char* buffer = buf;
		uint32_t bytes_read = 0;
		while (bytes_read < count) {
			*buffer = ioq_getchar(&kbd_buf);
			bytes_read++, buffer++;
		}
		return bytes_read;
	}

	uint32_t _fd = fd_local2global(fd);
	file* rd_file = &file_table[_fd];
	if (rd_file->fd_flag & O_CREAT || rd_file->fd_flag & O_WRONLY) {
		printk("sys_read: not allowed to read file with O_CREAT or O_WRONLY\n");
		return -1;
	} else {
		return file_read(rd_file, buf, count);
	}
}

/* 重置用于文件读写操作的偏移指针，成功返回新的偏移量，失败返回 -1 */
int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence) {
	if (fd <= stderr_no) {
		printk("sys_lseek: fd error\n");
		return -1;
	}
	ASSERT(whence > 0 && whence < 4);

	uint32_t _fd = fd_local2global(fd);
	file* pf = &file_table[_fd];

	// 新的偏移量必须位于文件大小之内
	int32_t new_pos = 0;
	int32_t file_size = (int32_t)pf->fd_inode->i_size;

	switch (whence)
	{
	case SEEK_SET:
		new_pos = offset;
		break;
	case SEEK_CUR:
		new_pos = (int32_t)pf->fd_pos + offset;
		break;
	case SEEK_END:
		new_pos = file_size + offset;
		break;
	}

	// 下标从 0 开始，故最后一个有效位置的下标在文件大小 -1
	if (new_pos < 0 || new_pos > (file_size-1)) {
		return -1;
	}
	pf->fd_pos = new_pos;
	return pf->fd_pos;
}

/* 打开一个目录，成功返回目录指针，失败返回 NULL */
dir* sys_opendir(const char* name) {
	ASSERT(strlen(name) < MAX_PATH_LEN);
	if (name[0] == '/' && name[1] == 0) {
		return &root_dir;
	}

	path_search_record searched_record;
	memset(&searched_record, 0, sizeof(path_search_record));
	int inode_no = search_file(name, &searched_record);
	dir* ret = NULL;
	if (inode_no == -1) {
		printk(
			"In %s, sub path %s not exist\n",
			name, searched_record.searched_path
		);
	} else {
		if (searched_record.file_type == FT_REGULAR) {
			printk("%s is a regular file\n", name);
		} else if (searched_record.file_type == FT_DIRECTORY) {
			ret = dir_open(cur_part, inode_no);
		}
	}
	dir_close(searched_record.parent_dir);
	return ret;
}

/* 尝试关闭一个目录，成功返回 0， 失败返回 -1 */
int32_t sys_closedir(dir* d) {
	int32_t ret = -1;
	if (d != NULL) {
		dir_close(d);
		ret = 0;
	}
	return ret;
}

/* 删除一个普通文件，成功返回 0，失败返回 -1 */
int32_t sys_unlink(const char* pathname) {
	ASSERT(strlen(pathname) < MAX_PATH_LEN);

	path_search_record searched_record;
	memset(&searched_record, 0, sizeof(path_search_record));
	int inode_no = search_file(pathname, &searched_record);
	ASSERT(inode_no != 0);
	if (inode_no == -1) {
		printk("file %s not found\n", pathname);
		dir_close(searched_record.parent_dir);
		return -1;
	}

	if (searched_record.file_type == FT_DIRECTORY) {
		printk("can't delete a direcotry now");
		dir_close(searched_record.parent_dir);
		return -1;
	}

	/* 检查是否在已经打开的文件列表中 */
	uint32_t file_idx = 0;
	while (file_idx < MAX_FILE_OPEN) {
		if (
			file_table[file_idx].fd_inode != NULL
			&& (uint32_t)inode_no == file_table[file_idx].fd_inode->i_no
		) {
			break;
		}
		file_idx++;
	}
	if (file_idx < MAX_FILE_OPEN) {
		dir_close(searched_record.parent_dir);
		printk("file %s is in use, not allow to delete\n", pathname);
		return -1;
	}
	ASSERT(file_idx == MAX_FILE_OPEN);

	void* io_buf = sys_malloc(SECTOR_SIZE * 2);
	if (io_buf == NULL) {
		dir_close(searched_record.parent_dir);
		printk("sys_unlink: malloc for io_buf failed\n");
		return -1;
	}

	dir* parent_dir = searched_record.parent_dir;
	delete_dir_entry(cur_part, parent_dir, inode_no, io_buf);
	inode_release(cur_part, inode_no);
	sys_free(io_buf);
	dir_close(searched_record.parent_dir);
	return 0;
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

/* 在磁盘上搜索文件系统，若没有则格式化分区来创建之 */
void filesys_init() {
	printk("searching filesystem...\n");
	// 格式化硬盘中的每个分区
	list_traversal(&partition_list, for_each_partition, 0);
	// 将 sdb1 挂载到内存
	list_traversal(&partition_list, mount_partition, (int)"sdb1");
	// 打开挂载的分区的根目录
	open_root_dir(cur_part);
	// 初始化文件表
	for(int i=0; i<MAX_FILE_OPEN; i++) {
		file_table[i].fd_inode = NULL;
	}
}