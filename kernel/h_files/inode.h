#ifndef __INODE_H
#define __INODE_H

#include "ide.h"
#include "list.h"
#include "stdint.h"

/* inode 结构 */
typedef struct {
	// inode 编号
	uint32_t i_no;
	/*
	当此 inode 是文件时，i_size 指文件大小
	当此 inode 是目录时，i_size 指该目录下所有目录项大小之和
	均以字节为单位而不是数据块，为了方便编码，数据块大小等于扇区大小
	*/
	uint32_t i_size;
	// 记录此文件被打开的次数
	uint32_t i_open_cnts;
	// TODO: 写文件不能并行，此标记用于供准备写文件的进程检查（为什么不加锁？）
	bool write_deny;
	// 下标 0-11 为直接块，12 存储一级间接块指针，共支持 128 个间接块，因此单文件总大小限制在 70K
	uint32_t i_sectors[13];
	// 用于加入 "已打开的 inode 列表"，该列表做缓存用，避免多次读盘
	struct list_elem inode_tag;
} inode;

void inode_sync(partition* part, inode* in, void* io_buf);
inode* inode_open(partition* part, uint32_t inode_no);
void inode_close(inode* inode);
void inode_init(uint32_t inode_no, inode* new_inode);

#endif