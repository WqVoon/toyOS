#include "memory.h"
#include "stdint.h"
#include "print.h"

// 每一页的大小
#define PG_SIZE 4096

// 内存位图的基址
#define MEM_BITMAP_BASE 0xc009a000

/**
 * 内核堆的起始地址，由于当前该地址在分页机制下是页目录表
 * 因此将来内核的虚拟地址 0xc0100000~0xc0101fff 并不映射到这个位置
 */
#define K_HEAP_START 0xc0100000

pool kernel_pool, user_pool;
virtual_addr kernel_vaddr;

/* 初始化内存池 */
static void mem_pool_init(uint32_t all_mem) {
	put_str("  mem_pool_init start\n");
	/**
	 * 256个页框 = 1 + 1 + 254
	 * 	页框指的是页表中页表项对应的物理内存区域（不重复）
	 *  这里页目录表本身占一个页框（1）
	 *  页目录表第 0 和第 768 项指向同一个页表，算同一个页框（1）
	 *  页目录表第 769～1022 项指向 254 个页表（254）
	 *  页目录表最后一项指向页目录表，所以不算一个页框（x)
	 */
	uint32_t page_table_size = PG_SIZE * 256;
	/**
	 * 加上低端的 1MB 就是当前内存的总使用量
	 * 由于位图被安放在 MEM_BITMAP_BASE 中，也在低端 1MB 内
	 * 因此其所占的空间不用额外计算
	 */
	uint32_t used_mem = page_table_size + 0x100000;
	uint32_t free_mem = all_mem - used_mem;
	uint16_t all_free_pages = free_mem / PG_SIZE;

	// 内核和用户各占用一半的物理内存页，但由于页总数可能是单数，故做如下处理
	uint16_t kernel_free_pages = all_free_pages / 2;
	uint16_t user_free_pages = all_free_pages - kernel_free_pages;

	// 内核和用户位图的长度，这里不处理余数，有可能会丢掉部分内存
	uint32_t kbm_length = kernel_free_pages / 8;
	uint32_t ubm_length = user_free_pages / 8;

	// 内核和用户内存池的起始地址
	uint32_t kp_start = used_mem;
	uint32_t up_start = kp_start + kernel_free_pages * PG_SIZE;

	kernel_pool.phy_addr_start = kp_start;
	user_pool.phy_addr_start = up_start;

	kernel_pool.pool_size = kernel_free_pages * PG_SIZE;
	user_pool.pool_size = user_free_pages * PG_SIZE;

	kernel_pool.pool_bitmap.btmp_bytes_len = kbm_length;
	user_pool.pool_bitmap.btmp_bytes_len = ubm_length;

	kernel_pool.pool_bitmap.bits = (void*)MEM_BITMAP_BASE;
	user_pool.pool_bitmap.bits = (void*)(MEM_BITMAP_BASE + kbm_length);

	put_str("    kernel_pool_bitmap_start:  ");
	put_int((int) kernel_pool.pool_bitmap.bits);
	put_str("\n");
	put_str("    kernel_pool_phy_addr_start:");
	put_int(kernel_pool.phy_addr_start);
	put_str("\n");

	put_str("    user_pool_bitmap_start:    ");
	put_int((int) user_pool.pool_bitmap.bits);
	put_str("\n");
	put_str("    user_pool_phy_addr_start:  ");
	put_int(user_pool.phy_addr_start);
	put_str("\n");

	bitmap_init(&kernel_pool.pool_bitmap);
	bitmap_init(&user_pool.pool_bitmap);

	kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbm_length;
	kernel_vaddr.vaddr_bitmap.bits = \
	(void*) (MEM_BITMAP_BASE + kbm_length + ubm_length);

	kernel_vaddr.vaddr_start = K_HEAP_START;
	bitmap_init(&kernel_vaddr.vaddr_bitmap);
	put_str("  mem_pool_init done\n");
}

/* 内存管理部分初始化入口 */
void mem_init(void) {
	put_str("mem_init start\n");
	uint32_t mem_bytes_total = *((uint32_t*)(0xb00));
	mem_pool_init(mem_bytes_total);
	put_str("mem_init done\n");
}