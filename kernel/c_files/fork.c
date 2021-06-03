#include "process.h"
#include "string.h"
#include "stdint.h"
#include "thread.h"
#include "file.h"

extern void intr_exit(void);

/* 将父进程的 pcb 拷贝给子进程 */
static int32_t copy_pcb_vaddrbitmap_stack0 (
	task_struct* child_thread, task_struct* parent_thread
) {
	// TODO: 复制了进程文件表 fd_table，如果里面有可写的文件描述符会有问题？

/* 复制 pcb 所在的整个页，里面包括 pcb 信息及特权 0 即栈和返回地址 */
	memcpy(child_thread, parent_thread, PG_SIZE);

	// 下面分别单独修改一些内容
	child_thread->pid = fork_pid();
	child_thread->status = TASK_READY;
	child_thread->ticks = child_thread->priority;
	child_thread->parent_id = parent_thread->pid;
	child_thread->general_tag.prev =\
	child_thread->general_tag.next =\
	child_thread->all_list_tag.prev =\
	child_thread->all_list_tag.next = NULL;
	block_desc_init(child_thread->u_block_desc);

/* 复制父进程的虚拟地址池的位图 */
	uint32_t bitmap_pg_cnt = DIV_ROUND_UP(
		(0xc0000000 - USER_VADDR_START) / PG_SIZE / 8,
		PG_SIZE
	);
	void* vaddr_btmp = get_kernel_pages(bitmap_pg_cnt);

	// 此时 child_thread->userprog_vaddr.vaddr_bitmap.bits 还是指向父进程虚拟地址
	memcpy(
		vaddr_btmp, child_thread->userprog_vaddr.vaddr_bitmap.bits,
		bitmap_pg_cnt * PG_SIZE
	);
	child_thread->userprog_vaddr.vaddr_bitmap.bits = vaddr_btmp;

	ASSERT(strlen(child_thread->name) < 11);
	strcat(child_thread->name, "_fork");
	return 0;
}

/* 复制子进程的进程体（代码和数据）及用户栈 */
static void copy_body_stack3 (
	task_struct* child_thread,
	task_struct* parent_thread,
	void* buf_page
) {
	uint8_t* vaddr_btmp = parent_thread->userprog_vaddr.vaddr_bitmap.bits;
	uint32_t btmp_bytes_len = parent_thread->userprog_vaddr.vaddr_bitmap.btmp_bytes_len;
	uint32_t vaddr_start = parent_thread->userprog_vaddr.vaddr_start;
	uint32_t idx_byte = 0, idx_bit = 0, prog_vaddr = 0;

	// 在父进程的地址空间中查找已有数据的页
	while (idx_byte < btmp_bytes_len) {
		if (vaddr_btmp[idx_byte]) {
			idx_bit = 0;
			while (idx_bit < 8) {
				if ((BITMAP_MASK << idx_bit) & vaddr_btmp[idx_byte]) {
					prog_vaddr = (idx_byte * 8 + idx_bit) * PG_SIZE + vaddr_start;

					// 先将父进程在用户空间中的数据复制到内核缓冲区 buf_page
					memcpy(buf_page, (void*)prog_vaddr, PG_SIZE);

					page_dir_activate(child_thread);

					// 申请虚拟地址
					get_a_page_without_opvaddrbitmap(PF_USER, prog_vaddr);

					// 将缓冲区中的数据复制到子进程
					memcpy((void*)prog_vaddr, buf_page, PG_SIZE);

					page_dir_activate(parent_thread);
				}
				idx_bit++;
			}
		}
		idx_byte++;
	}
}

/* 为子进程构建 thread_stack 和修改返回值 */
static int32_t build_child_stack(task_struct* child_thread) {
// 使子进程的 pid 返回 0
	intr_stack* intr_0_stack =\
	(intr_stack*)((uint32_t)child_thread + PG_SIZE - sizeof(intr_stack));
	intr_0_stack->eax = 0;

/* 为 switch_to 构建 thread_stack，将其安置在 intr_stack 之下
	unused_retaddr，function，func_arg 三者因为用不到故不构建
	因此该函数执行后栈中是除去上面三个元素的 thread_stack 后接 intr_stack
	intr_stack 在执行了 copy_pcb_vaddrbitmap_stack0 时已经被赋值成父进程的样子
	其中就包括从中断退出后 cs:eip 应该回到的地址，而此时父子进程的虚拟地址位图结构完全一致
	故放置在 thread_stack.eip 上的 intr_exit 可以正常执行并把执行流带回到 fork 调用的地方
*/
	uint32_t* ret_addr_in_thread_stack = (uint32_t*)intr_0_stack - 1;
	uint32_t* esi_ptr_in_thread_stack = (uint32_t*)intr_0_stack - 2;
	uint32_t* edi_ptr_in_thread_stack = (uint32_t*)intr_0_stack - 3;
	uint32_t* ebx_ptr_in_thread_stack = (uint32_t*)intr_0_stack - 4;

	// ebp 此时的地址就是当时的 esp
	uint32_t* ebp_ptr_in_thread_stack = (uint32_t*)intr_0_stack - 5;

	// 将 switch_to 的返回值更新为 intr_exit
	*ret_addr_in_thread_stack = (uint32_t)intr_exit;

	*ebp_ptr_in_thread_stack = *ebx_ptr_in_thread_stack =\
	*edi_ptr_in_thread_stack = *esi_ptr_in_thread_stack = 0;

	child_thread->self_kstack = ebp_ptr_in_thread_stack;
	return 0;
}

extern file* file_table;

/* 更新 inode 打开数 */
static void update_inode_open_cnts(task_struct* thread) {
	int32_t local_fd = 3, global_fd = 0;
	while (local_fd < MAX_FILES_OPEN_PER_PROC) {
		global_fd = thread->fd_table[local_fd];
		ASSERT(global_fd < MAX_FILE_OPEN);
		if (global_fd != -1) {
			file_table[global_fd].fd_inode->i_open_cnts++;
		}
		local_fd++;
	}
}

/* 拷贝父进程本身所占资源给子进程 */
static int32_t copy_process(
	task_struct* child_thread, task_struct* parent_thread
) {
	// 需要在内核空间中申请，这样进程间才可以共享这部分内容
	void* buf_page = get_kernel_pages(1);
	if (buf_page == NULL) {
		return -1;
	}

// 复制父进程的 pcb、虚拟地址位图、内核栈
	if (copy_pcb_vaddrbitmap_stack0(child_thread, parent_thread) == -1) {
		return -1;
	}

// 为子进程创建页表
	child_thread->pgdir = create_page_dir();
	if (child_thread->pgdir == NULL) {
		return -1;
	}

// 复制父进程进程体及用户栈给子进程
	copy_body_stack3(child_thread, parent_thread, buf_page);

// 构建子进程 thread_stack 和修改返回值 pid
	build_child_stack(child_thread);

// 更新文件 inode 的打开次数
	update_inode_open_cnts(child_thread);

	mfree_page(PF_KERNEL, buf_page, 1);
	return 0;
}

extern struct list thread_ready_list;
extern struct list thread_all_list;

int16_t sys_fork(void) {
	task_struct* parent_thread = running_thread();
	// 为子进程分配一页来创建 pcb
	task_struct* child_thread = get_kernel_pages(1);
	if (child_thread == NULL) {
		return -1;
	}

	ASSERT(INTR_OFF == intr_get_status() && parent_thread->pgdir != NULL);

	if (copy_process(child_thread, parent_thread) == -1) {
		return -1;
	}

	ASSERT(!elem_find(&thread_ready_list, &child_thread->general_tag));
	list_append(&thread_ready_list, &child_thread->general_tag);
	ASSERT(!elem_find(&thread_all_list, &child_thread->all_list_tag));
	list_append(&thread_all_list, &child_thread->all_list_tag);

	return child_thread->pid;
}