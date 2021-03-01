#ifndef __FS_H
#define __FS_H

#include "stdint.h"

// 前向引用
typedef struct __dir  dir;

// 每个分区所支持的最大创建文件的数量
#define MAX_FILES_PER_PART 4096
// 每扇区的位数
#define BITS_PER_SECTOR 4096
// 扇区字节大小
#define SECTOR_SIZE 512
// 块字节大小
#define BLOCK_SIZE SECTOR_SIZE
// 路径最大长度
#define MAX_PATH_LEN 512

typedef enum {
	// 不支持的文件类型
	FT_UNKNOWN,
	// 普通文件
	FT_REGULAR,
	// 目录
	FT_DIRECTORY
} file_types;

/* 打开文件的选项 */
typedef enum {
	// 只读
	O_RDONLY,
	// 只写
	O_WRONLY,
	// 读写
	O_RDWR,
	// 创建
	O_CREAT = 4
} oflags;

/* 用来记录查找文件过程中已找到的上级路径，也就是查找文件过程中“走过的地方” */
typedef struct {
	// 查找过程中的父路径，如果文件不存在的话可通过此值来判断是路径的哪个部分不存在
	char searched_path[MAX_PATH_LEN];
	// 文件或目录所在的直接父目录
	dir* parent_dir;
	// 找到的是普通文件，还是目录，找不到则置为 FT_UNKNOWN
	file_types file_type;
} path_search_record;

int32_t sys_open(const char* pathname, uint8_t flags);
uint32_t path_depth_cnt(char* pathname);
void filesys_init();
int32_t sys_close(int32_t fd);
int32_t sys_write(int32_t fd, const void* buf, uint32_t count);
int32_t sys_read(int32_t fd, void* buf, uint32_t count);

#endif