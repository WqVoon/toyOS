#ifndef __KERNEL_MEMORY_H
#define __KERNEL_MEMORY_H

#include "stdint.h"
#include "bitmap.h"

/* 虚拟地址池 */
typedef struct {
	bitmap   vaddr_bitmap; // 虚拟地址使用的位图
	uint32_t vaddr_start;  // 虚拟地址的起始地址
} virtual_addr;

/* 物理内存池，有两个实例分别用于管理内核和用户内存 */
typedef struct {
	bitmap   pool_bitmap;
	uint32_t phy_addr_start;
	uint32_t pool_size;
} pool;

extern pool kernel_pool, user_pool;

void mem_init(void);

#endif