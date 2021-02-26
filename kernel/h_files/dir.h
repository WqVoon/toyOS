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

#endif