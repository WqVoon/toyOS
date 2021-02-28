#ifndef __DIR_H
#define __DIR_H

#include "fs.h"
#include "inode.h"
#include "stdint.h"

// 最大文件名长度
#define MAX_FILE_NAME_LEN 16

/* 目录结构 */
typedef struct {
	inode* inode;
	// 记录在目录内的偏移
	uint32_t dir_pos;
	// 目录的数据缓存
	uint8_t dir_buf[512];
} dir;

/* 目录项结构 */
typedef struct {
	char filename[MAX_FILE_NAME_LEN];
	uint32_t i_no;
	file_types f_type;
} dir_entry;

void open_root_dir(partition* part);
dir* dir_open(partition* part, uint32_t inode_no);
bool search_dir_entry(partition* part, dir* pdir, const char* name, dir_entry* dir_e);
void dir_close(dir* dir);
void create_dir_entry(char* filename, uint32_t inode_no, uint8_t file_type, dir_entry* p_de);
bool sync_dir_entry(dir* parent_dir, dir_entry* p_de, void* io_buf);

#endif