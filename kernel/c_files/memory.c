#include "memory.h"
#include "stdint.h"
#include "print.h"
#include "global.h"
#include "debug.h"
#include "string.h"

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


// 获取 addr 中 PDE 的下标
#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22)
// 获取 addr 中 PTE 的下标
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12)

/**
 * 在 pf 表示的虚拟内存池中申请 pg_cnt 个虚拟页
 * 成功则返回虚拟页的起始地址，失败则返回 NULL
 */
static void* vaddr_get(pool_flags pf, uint32_t pg_cnt) {
	int vaddr_start, bit_idx_start;
	uint32_t cnt = 0;
	if (pf == PF_KERNEL) {
		bit_idx_start = bitmap_scan(&kernel_vaddr.vaddr_bitmap, pg_cnt);
		if (bit_idx_start == -1) return NULL;

		while (cnt < pg_cnt) {
			bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
		}

		vaddr_start = kernel_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
	} else {
		//TODO: 用户内存池，暂不处理
	}

	return (void*) vaddr_start;
}

/* 获取 vaddr 的 pte 指针 */
uint32_t* pte_ptr(uint32_t vaddr) {
	uint32_t* pte = (uint32_t*) (
		// 用于指向页目录表项中的最后一项 pde，该 pde 指向页目录表本身
		0xffc00000 + \
		// 获取 vaddr 的实际 pde 下标，用于在上面获取的页目录表中查找对应的 pde
		((vaddr & 0xffc00000) >> 10) + \
		// 获取 vaddr 的实际 pte 下标，用于获取其对应的 pte 的地址
		// 这里需要手动 x4 ，因为处理器不管
		PTE_IDX(vaddr) * 4
	);
	return pte;
}

/* 获取 vaddr 的 pde 指针 */
uint32_t* pde_ptr(uint32_t vaddr) {
	uint32_t* pde = (uint32_t*) (
		(0xfffff000) + PDE_IDX(vaddr)*4
	);
	return pde;
}

/**
 * 在 m_pool 指向的物理内存中分配一个物理页
 * 成功返回页框的物理地址，失败返回 NULL
 */
static void* palloc(pool* m_pool) {
	int bit_idx = bitmap_scan(&m_pool->pool_bitmap, 1);
	if (bit_idx == -1) return NULL;

	bitmap_set(&m_pool->pool_bitmap, bit_idx, 1);
	uint32_t page_phyaddr = ((bit_idx * PG_SIZE) + m_pool->phy_addr_start);
	return (void*) page_phyaddr;
}

/* 在页表中添加虚拟地址 _vaddr 与物理地址 _page_phyaddr 的映射 */
static void page_table_add(void* _vaddr, void* _page_phyaddr) {
	uint32_t vaddr = (uint32_t) _vaddr;
	uint32_t page_phyaddr = (uint32_t) _page_phyaddr;
	uint32_t* pde = pde_ptr(vaddr);
	uint32_t* pte = pte_ptr(vaddr);

	put_str("\nPage_table_add Info:\n");
	put_str(" Vad:"); put_int(vaddr);         put_char('\n');
	put_str(" Pad:"); put_int(page_phyaddr);  put_char('\n');
	put_str(" PDE:"); put_int((uint32_t)pde); put_char('\n');
	put_str(" PTE:"); put_int((uint32_t)pte); put_char('\n');

	if (*pde & 0x1) {
		ASSERT(! (*pte & 0x1));

		if (! (*pte & 0x1)) {
			*pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
		} else {
			//TODO: 由于前面的 ASSERT 目前应该执行不到这里
		}
	} else {
		// 页表中的页框都从内核空间分配
		uint32_t pde_phyaddr = (uint32_t)palloc(&kernel_pool);
		ASSERT((uint32_t*)pde_phyaddr != NULL);

		*pde = (pde_phyaddr | PG_US_U | PG_RW_W | PG_P_1);

		// 清空页表中所有的内容，避免陈旧的数据使页表混乱
		memset((void*)((int)pte & 0xfffff000), 0, PG_SIZE);

		ASSERT(!(*pte & 0x1));
		*pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
	}
}

/**
 * 分配 pg_cnt 个页空间，成功返回起始的虚拟地址，否则返回 NULL
 * 具体步骤为：
 *  1.通过 vaddr_get 在虚拟内存池中申请虚拟地址
 *  2.通过 palloc 在物理内存池中申请物理页
 *  3.通过 page_table_add 将上述两个地址通过页表绑定
 */
void* malloc_page(pool_flags pf, uint32_t pg_cnt) {
	// 当前内存总量 32MB ，假设 kernel 和 user 各拥有 15MB 的内存
	// 15 * 1024 * 1024 / 4096 = 3840页
	ASSERT(pg_cnt > 0 && pg_cnt < 3840);

	void* vaddr_start = vaddr_get(pf, pg_cnt);
	if (vaddr_start == NULL) return NULL;

	uint32_t vaddr = (uint32_t) vaddr_start, cnt = pg_cnt;
	pool* mem_pool = pf & PF_KERNEL? &kernel_pool: &user_pool;

	// 虽然虚拟地址是连续的，但物理地址可以是不连续的，因此要逐个做映射
	while (cnt-- > 0) {
		void* page_phyaddr = palloc(mem_pool);

		if (page_phyaddr == NULL) {
			//TODO: 这里是否要回收上面 vaddr_get 申请的虚拟内存？
			return NULL;
		}
		page_table_add((void*)vaddr, page_phyaddr);
		vaddr += PG_SIZE;
	}

	return vaddr_start;
}

/* 从内核物理内存池中申请 pg_cnt 页内存，成功返回虚拟地址，失败返回 NULL */
void* get_kernel_pages(uint32_t pg_cnt) {
	void* vaddr = malloc_page(PF_KERNEL, pg_cnt);
	if (vaddr != NULL) {
		memset(vaddr, 0, pg_cnt*PG_SIZE);
	}
	return vaddr;
}