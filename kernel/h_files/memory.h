#ifndef __KERNEL_MEMORY_H
#define __KERNEL_MEMORY_H

#include "stdint.h"
#include "bitmap.h"

#define PG_P_1  1 // pte 或 pde 存在属性位
#define PG_P_0  0 // 同上
#define PG_RW_R 0 // R/W 属性位，此处表示读/执行
#define PG_RW_W 2 // R/W 属性位，此处表示读/写/执行
#define PG_US_S 0 // U/S 属性位，此处表示系统级，仅允许 0～2 特权级访问
#define PG_US_U 4 // U/S 属性位，此处表示用户级

/* 内存池标记，用于判断是哪个内存池 */
typedef enum {
	PF_KERNEL = 1,
	PF_USER   = 2
} pool_flags;

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

uint32_t* pte_ptr(uint32_t);

uint32_t* pde_ptr(uint32_t);

// void* malloc_page(pool_flags, uint32_t);

void* get_kernel_pages(uint32_t);

#endif