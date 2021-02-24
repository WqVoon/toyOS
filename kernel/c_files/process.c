#include "debug.h"
#include "global.h"
#include "thread.h"
#include "string.h"
#include "memory.h"
#include "console.h"
#include "process.h"
#include "interrupt.h"

// 用于更新 tss 中的特权级0
extern void update_tss_esp(task_struct* pthread);
// 中断退出函数，用于切换进程
extern void intr_exit(void);
// 就绪队列
extern struct list thread_ready_list;
// 全部任务队列
extern struct list thread_all_list;

/* 构建用户进程初始上下文信息 */
void start_process(void* filename_) {
	void* function = filename_;
	task_struct* cur = running_thread();
	cur->self_kstack += sizeof(thread_stack);

	intr_stack* proc_stack = (intr_stack*) cur->self_kstack;
	memset(proc_stack, 0, sizeof(intr_stack));
	proc_stack->ds = proc_stack->es = proc_stack->fs = SELECTOR_U_DATA;
	proc_stack->eip = function;
	proc_stack->cs = SELECTOR_U_CODE;
	proc_stack->eflags = (EFLAGS_IOPL_0 | EFLAGS_MBS | EFLAGS_IF_1);
	proc_stack->esp = (void*) ((uint32_t)get_a_page(PF_USER, USER_STACK3_VADDR) + PG_SIZE);
	proc_stack->ss = SELECTOR_U_DATA;
	__asm__ __volatile__ ("movl %0, %%esp; jmp intr_exit;" :: "g"(proc_stack): "memory");
}

/* 激活页表 */
void page_dir_activate(task_struct* p_thread) {
	/*
	默认为内核线程的页目录表地址，如果 p_thread->pgdir 不为 NULL
	则说明参数对应的 task 为用户进程，需要获取其页目录表地址的物理地址
	*/
	uint32_t pagedir_phy_addr = 0x100000;
	if (p_thread->pgdir != NULL) {
		pagedir_phy_addr = addr_v2p((uint32_t) p_thread->pgdir);
	}

	__asm__ __volatile__ ("movl %0, %%cr3" :: "r"(pagedir_phy_addr): "memory");
}

/* 激活线程或进程的页表，更新 tss 中 esp0 为进程的特权级0的栈 */
void process_activate(task_struct* p_thread) {
	ASSERT(p_thread != NULL);
	page_dir_activate(p_thread);

	// 仅在用户进程时需要更新 esp0，因为内核线程本身就是特权级0
	if (p_thread->pgdir) {
		update_tss_esp(p_thread);
	}
}

/* 创建页目录表 */
uint32_t* create_page_dir(void) {
	// 用户进程的页目录表也放置在内核空间内
	uint32_t* page_dir_vaddr = get_kernel_pages(1);
	if (page_dir_vaddr == NULL) {
		console_put_str("create_page_dir: get_kernel_page failed!");
		return NULL;
	}

	// 1.先复制页表，把内核页表中 0x300(768)~1023共计256个页目录项复制到新页表中
	memcpy(
		(uint32_t*)((uint32_t)page_dir_vaddr + 0x300*4),
		(uint32_t*) (0xfffff000 + 0x300*4),
		1024
	);

	// 2.更新页目录地址，将最后一个页目录项修改为新的页目录表
	uint32_t new_page_dir_phy_addr = addr_v2p((uint32_t) page_dir_vaddr);
	page_dir_vaddr[1023] = new_page_dir_phy_addr | PG_US_U | PG_RW_W | PG_P_1;

	return page_dir_vaddr;
}

/* 创建用户进程虚拟地址位图 */
void create_user_vaddr_bitmap(task_struct* user_prog) {
	user_prog->userprog_vaddr.vaddr_start = USER_VADDR_START;

	uint32_t bitmap_pg_cnt = DIV_ROUND_UP(
		(0xc0000000 - USER_VADDR_START) / PG_SIZE / 8,
		PG_SIZE
	);
	user_prog->userprog_vaddr.vaddr_bitmap.bits = get_kernel_pages(bitmap_pg_cnt);
	user_prog->userprog_vaddr.vaddr_bitmap.btmp_bytes_len = \
	(0xc0000000 - USER_VADDR_START) / PG_SIZE / 8;
	bitmap_init(&user_prog->userprog_vaddr.vaddr_bitmap);
}

/* 创建用户进程 */
void process_execute(void* filename, char* name) {
	// TODO: 待确认该值为多少
	uint32_t default_prio = 31;
	// PCB 在内核空间中申请
	task_struct* thread = get_kernel_pages(1);
	init_thread(thread, name, default_prio);
	create_user_vaddr_bitmap(thread);
	thread_create(thread, start_process, filename);
	thread->pgdir = create_page_dir();
	block_desc_init(thread->u_block_desc);

	intr_status old_status = intr_disable();
	ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
	list_append(&thread_ready_list, &thread->general_tag);

	ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
	list_append(&thread_all_list, &thread->all_list_tag);
	intr_set_status(old_status);
}