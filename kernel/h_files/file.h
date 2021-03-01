#ifndef __FILE_H
#define __FILE_H

#include "stdint.h"
#include "inode.h"
#include "dir.h"

/* 文件结构 */
typedef struct {
	// 记录当前文件操作的偏移地址，以 0 为起始，最大为 -1
	uint32_t fd_pos;
	uint32_t fd_flag;
	inode* fd_inode;
} file;

/* 标准输入输出描述符 */
typedef enum {
	stdin_no,
	stdout_no,
	stderr_no
} std_fd;

/* 位图类型 */
typedef enum {
	INODE_BITMAP,
	BLOCK_BITMAP
} bitmap_type;

// TODO: 为什么要把它限制在 32
#define MAX_FILE_OPEN 32

int32_t get_free_slot_in_global(void);
int32_t pcb_fd_install(int32_t globa_fd_idx);
int32_t inode_bitmap_alloc(partition* part);
int32_t block_bitmap_alloc(partition* part);
void bitmap_sync(partition* part, uint32_t bit_idx, bitmap_type btmp);
int32_t file_create(dir* parent_dir, char* filename, uint8_t flag);
int32_t file_open(uint32_t inode_no, uint8_t flag);

#endif