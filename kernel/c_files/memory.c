#include "memory.h"
#include "stdint.h"
#include "print.h"
#include "global.h"
#include "debug.h"
#include "string.h"
#include "thread.h"
#include "sync.h"
#include "console.h"

// 每一页的大小
#define PG_SIZE 4096

// 内存位图的基址
#define MEM_BITMAP_BASE 0xc009a000

/**
 * 内核堆的起始地址，由于当前该地址在分页机制下是页目录表
 * 因此将来内核的虚拟地址 0xc0100000~0xc0101fff 并不映射到这个位置
 */
#define K_HEAP_START 0xc0100000

/* 内存仓库 */
typedef struct {
	mem_block_desc* desc;
	// 当 large 为 true 时，cnt 表示页框数，否则表示空闲的 mem_block 数量
	uint32_t cnt;
	bool large;
} arena;

//  内存块描述符们，分别支持 16, 32, 64, 128, 256, 512, 1024 七种规模
mem_block_desc k_block_descs[DESC_CNT];

pool kernel_pool, user_pool;
virtual_addr kernel_vaddr;

/* 初始化内存池 */
static void mem_pool_init(uint32_t all_mem) {
	put_str("  mem_pool_init start\n");
	lock_init(&kernel_pool.lock);
	lock_init(&user_pool.lock);
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
	block_desc_init(k_block_descs);
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
		task_struct* cur = running_thread();
		bit_idx_start = bitmap_scan(&cur->userprog_vaddr.vaddr_bitmap, pg_cnt);
		if (bit_idx_start == -1) return NULL;

		while (cnt < pg_cnt) {
			bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
		}

		vaddr_start = cur->userprog_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
		//TODO:(0xc0000000 - PG_SIZE) 作为用户 3 级栈已经在 start_process 被分配？
		ASSERT((uint32_t)vaddr_start < (0xc0000000 - PG_SIZE));
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

	// console_put_str("\nPage_table_add Info:\n");
	// console_put_str(" Vad:"); console_put_int(vaddr);         console_put_char('\n');
	// console_put_str(" Pad:"); console_put_int(page_phyaddr);  console_put_char('\n');
	// console_put_str(" PDE:"); console_put_int((uint32_t)pde); console_put_char('\n');
	// console_put_str(" PTE:"); console_put_int((uint32_t)pte); console_put_char('\n');

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

/* 从用户空间中申请 4K 的内存 */
void* get_user_pages(uint32_t pg_cnt) {
	lock_acquire(&user_pool.lock);
	void* vaddr = malloc_page(PF_USER, pg_cnt);
	if (vaddr != NULL) {
		memset(vaddr, 0, pg_cnt*PG_SIZE);
	}
	lock_release(&user_pool.lock);
	return vaddr;
}

/* 得到虚拟地址对应的物理地址 */
uint32_t addr_v2p(uint32_t vaddr) {
	uint32_t* pte = pte_ptr(vaddr);
	return ((*pte & 0xfffff000) + (vaddr & 0x00000fff));
}

/* 将地址 vaddr 与 pf 池中的物理地址关联，仅支持一页空间分配 */
void* get_a_page(pool_flags pf, uint32_t vaddr) {
	pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
	lock_acquire(&mem_pool->lock);

	task_struct* cur = running_thread();
	int32_t bit_idx = -1;

	// 如果是用户进程申请内存
	if (cur->pgdir != NULL && pf == PF_USER) {
		bit_idx = (vaddr - cur->userprog_vaddr.vaddr_start) / PG_SIZE;
		ASSERT(bit_idx > 0);
		bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx, 1);

	// 如果是内核线程申请内核内存
	} else if (cur->pgdir == NULL && pf == PF_KERNEL) {
		bit_idx = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
		ASSERT(bit_idx > 0);
		bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx, 1);

	// 剩下的情况均错误
	} else {
		ASSERT(!"[ERROR] get_a_page error");
	}

	void* page_phyaddr = palloc(mem_pool);
	if (page_phyaddr == NULL) return NULL;

	page_table_add((void*) vaddr, page_phyaddr);
	lock_release(&mem_pool->lock);
	return (void*) vaddr;
}

/* 初始化内存块描述符，为 malloc 作准备 */
void block_desc_init(mem_block_desc* desc_array) {
	uint16_t block_size = 16;
	for (int i=0; i<DESC_CNT; i++) {
		desc_array[i].block_size = block_size;
		desc_array[i].blocks_per_arena = \
		(PG_SIZE - sizeof(arena)) / block_size;
		list_init(&desc_array[i].free_list);
		block_size *= 2;
	}
}

/* 返回 arena 中第 idx 个内存块的地址 */
static mem_block* arena2block(arena* a, uint32_t idx) {
	return (mem_block*)\
	((uint32_t)a + sizeof(arena) + idx * a->desc->block_size);
}

/* 返回内存块 b 所在的 arena 地址 */
static arena* block2arena(mem_block* b) {
	return (arena*)((uint32_t)b & 0xfffff000);
}

/* 在堆中申请 size 字节的内存 */
void* sys_malloc(uint32_t size) {
	pool_flags PF;
	pool* mem_pool;
	uint32_t pool_size;
	mem_block_desc* descs;
	task_struct* cur_thread = running_thread();

	// 判断使用哪个内存池
	if (cur_thread->pgdir == NULL) {
		// 若为内核线程
		PF = PF_KERNEL;
		pool_size = kernel_pool.pool_size;
		mem_pool = &kernel_pool;
		descs = k_block_descs;
	} else {
		// 若为用户进程
		PF = PF_USER;
		pool_size = user_pool.pool_size;
		mem_pool = &user_pool;
		descs = cur_thread->u_block_desc;
	}

	// 若申请的内存不在内存池容量范围内，则直接返回 NULL
	if (!(size > 0 && size < pool_size)) return NULL;

	arena* a;
	mem_block* b;
	lock_acquire(&mem_pool->lock);

	if (size > 1024) {
		// 当大小大于 1024 时，直接分配页框
		uint32_t page_cnt = \
		DIV_ROUND_UP(size + sizeof(arena), PG_SIZE);

		a = malloc_page(PF, page_cnt);

		if (a != NULL) {
			memset(a, 0, page_cnt * PG_SIZE);
			a->desc = NULL;
			a->cnt = page_cnt;
			a->large = 1;
			lock_release(&mem_pool->lock);
			return (void*) (a + 1);
		} else {
			lock_release(&mem_pool->lock);
			return NULL;
		}
	} else {
		// 否则在 mem_block_desc 中去适配

		// 在描述符中找到合适的规模
		uint8_t desc_idx;
		for (desc_idx=0; desc_idx<DESC_CNT; desc_idx++) {
			if (size <= descs[desc_idx].block_size) {
				break;
			}
		}

		// 如果没有可用的 mem_block，就创建新的 arena
		if (list_empty(&descs[desc_idx].free_list)) {
			a = malloc_page(PF, 1);
			if (a == NULL) {
				lock_release(&mem_pool->lock);
				return NULL;
			}
			memset(a, 0, PG_SIZE);

			a->desc = &descs[desc_idx];
			a->large = 0;
			a->cnt = descs[desc_idx].blocks_per_arena;
			uint32_t block_idx;

			intr_status old_status = intr_disable();

			for (
				block_idx=0;
				block_idx<descs[desc_idx].blocks_per_arena;
				block_idx++
			) {
				b = arena2block(a, block_idx);
				ASSERT(! elem_find(&a->desc->free_list, &b->free_elem));
				list_append(&a->desc->free_list, &b->free_elem);
			}

			intr_set_status(old_status);
		}

		// 开始分配内存块
		b = elem2entry(mem_block, free_elem,\
		list_pop(&(descs[desc_idx].free_list)));
		memset(b, 0, descs[desc_idx].block_size);

		a = block2arena(b);
		a->cnt--;
		lock_release(&mem_pool->lock);
		return (void*) b;
	}
}

/* 将物理页地址 pg_phy_addr 会收到物理内存池 */
void pfree(uint32_t pg_phy_addr) {
	pool* mem_pool;
	uint32_t bit_idx = 0;
	if (pg_phy_addr > user_pool.phy_addr_start) {
		// 用户物理内存池
		mem_pool = &user_pool;
		bit_idx = (pg_phy_addr - user_pool.phy_addr_start) / PG_SIZE;
	} else {
		// 内核物理内存池
		mem_pool = &kernel_pool;
		bit_idx = (pg_phy_addr - kernel_pool.phy_addr_start) / PG_SIZE;
	}
	bitmap_set(&mem_pool->pool_bitmap, bit_idx, 0);
}

/* 去掉页表中虚拟地址 vaddr 的映射，只去掉 vaddr 对应的 pte */
static void page_table_pte_remove(uint32_t vaddr) {
	uint32_t* pte = pte_ptr(vaddr);
	*pte &= ~PG_P_1;
	__asm__ __volatile__ ("invlpg %0" :: "m"(vaddr) : "memory"); // 更新 tlb
}

/* 在虚拟地址池中释放以 _vaddr 起始的连续 pg_cnt 个虚拟页地址 */
static void vaddr_remove(pool_flags pf, void* _vaddr, uint32_t pg_cnt) {
	uint32_t bit_idx_start = 0, vaddr = (uint32_t)_vaddr, cnt = 0;

	if (pf == PF_KERNEL) {
		bit_idx_start = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
		while (cnt < pg_cnt) {
			bitmap_set(
				&kernel_vaddr.vaddr_bitmap,
				bit_idx_start + cnt++, 0
			);
		}
	} else {
		task_struct* cur_thread = running_thread();
		bit_idx_start = \
		(vaddr - cur_thread->userprog_vaddr.vaddr_start) / PG_SIZE;
		while (cnt < pg_cnt) {
			bitmap_set(
				&cur_thread->userprog_vaddr.vaddr_bitmap,
				bit_idx_start + cnt++, 0
			);
		}

	}
}

/* 释放以虚拟地址 vaddr 为起始的 cnt 个物理页框 */
void mfree_page(pool_flags pf, void* _vaddr, uint32_t pg_cnt) {
	uint32_t vaddr = (int32_t)_vaddr, page_cnt = 0;
	ASSERT(pg_cnt >= 1 && vaddr % PG_SIZE == 0);
	uint32_t pg_phy_addr = addr_v2p(vaddr);

	// 确保待释放的物理内存在低端 1MB+1KB 的页目录+1KB 的页表地址范围外
	ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr >= 0x102000);

	if (pg_phy_addr >= user_pool.phy_addr_start) {
		// 位于用户内存池
		vaddr -= PG_SIZE;
		while (page_cnt < pg_cnt) {
			vaddr += PG_SIZE;
			pg_phy_addr = addr_v2p(vaddr);

			// 确保物理地址属于用户物理内存池
			ASSERT(
				(pg_phy_addr % PG_SIZE) == 0
				&& pg_phy_addr >= user_pool.phy_addr_start
			);

			// 先归还物理页框
			pfree(pg_phy_addr);
			// 再从页表中清除此虚拟地址所在的页表项 pte
			page_table_pte_remove(vaddr);

			page_cnt++;
		}
	} else {
		// 位于内核内存池
		vaddr -= PG_SIZE;
		while (page_cnt < pg_cnt) {
			vaddr += PG_SIZE;
			pg_phy_addr = addr_v2p(vaddr);

			// 确保物理地址只属于内核物理内存池
			ASSERT(
				(pg_phy_addr % PG_SIZE) == 0
				&& pg_phy_addr >= kernel_pool.phy_addr_start
				&& pg_phy_addr < user_pool.phy_addr_start
			);

			pfree(pg_phy_addr);
			page_table_pte_remove(vaddr);
			page_cnt++;
		}
	}

	vaddr_remove(pf, _vaddr, pg_cnt);
}

/* 回收内存 ptr */
void sys_free(void* ptr) {
	ASSERT(ptr != NULL);

	pool_flags pf;
	pool* mem_pool;

	// 判断是内核内存还是用户内存
	if (running_thread()->pgdir == NULL) {
		ASSERT((uint32_t)ptr >= K_HEAP_START);
		pf = PF_KERNEL;
		mem_pool = &kernel_pool;
	} else {
		pf = PF_USER;
		mem_pool = &user_pool;
	}

	lock_acquire(&mem_pool->lock);
	mem_block* b = ptr;
	arena* a = block2arena(b);

	ASSERT(a->large == 0 || a->large == 1);
	if (a->desc == NULL && a->large == 1) {
		// 大块内存直接回收
		mfree_page(pf, a, a->cnt);
	} else {
		// 小块内存特殊处理
		// 先将该内存块放回 free_list
		list_append(&a->desc->free_list, &b->free_elem);

		// 再判断此 arena 是否全部空闲，如果是就释放之
		if (++a->cnt == a->desc->blocks_per_arena) {
			for (int i=0; i<a->desc->blocks_per_arena; i++) {
				mem_block* b = arena2block(a, i);
				ASSERT(elem_find(&a->desc->free_list, &b->free_elem));
				list_remove(&b->free_elem);
			}
			mfree_page(pf, a, 1);
		}
	}
	lock_release(&mem_pool->lock);
}

/* 安装 1 页大小的 vaddr，专门针对 fork 时虚拟地址位图无需操作的情况 */
void* get_a_page_without_opvaddrbitmap(
	pool_flags pf, uint32_t vaddr
) {
	pool* mem_pool = pf & PF_KERNEL ? &kernel_pool: &user_pool;
	lock_acquire(&mem_pool->lock);
	void* page_phyaddr = palloc(mem_pool);
	if (page_phyaddr == NULL) {
		lock_release(&mem_pool->lock);
		return NULL;
	}
	page_table_add((void*)vaddr, page_phyaddr);
	lock_release(&mem_pool->lock);
	return (void*)vaddr;
}