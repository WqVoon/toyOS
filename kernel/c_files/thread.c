#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "print.h"
#include "memory.h"

#define PG_SIZE 4096

/**
 * 由 kernel_thread 去执行 function(fun_arg)
 * 该函数作为 thread_stack 中的 eip 由 ret 指令跳转并执行
 * function 和 func_arg 也在 thread_stack 中被定义成同名变量
 * 由于 thread_stack.unused_retaddr 的存在，该函数可以正常使用两个参数
 */
static void kernel_thread(thread_func* function, void* func_arg) {
	function(func_arg);
}

// static void test_func() {
// 	while (1) put_str("Test ");
// }

/* 初始化线程栈 thread_stack */
void thread_create(task_struct* pthread, thread_func function, void* func_arg) {
	pthread->self_kstack -= sizeof(intr_stack);

	pthread->self_kstack -= sizeof(thread_stack);

	thread_stack* kthread_stack = (thread_stack*)pthread->self_kstack;
	kthread_stack->eip = kernel_thread;
	kthread_stack->function = function;
	kthread_stack->func_arg = func_arg;
	kthread_stack->ebp = kthread_stack->ebx = \
	kthread_stack->esi = kthread_stack->edi = 0;
	// kthread_stack->unused_retaddr = test_func;
}

/* 在 PCB 中初始化线程基本信息，信息位于 PCB 所在页的低地址 */
void init_thread(task_struct* pthread, char* name, int prio) {
	memset(pthread, 0, sizeof(*pthread));
	strcpy(pthread->name, name);
	pthread->status = TASK_RUNNING;
	pthread->priority = prio;
	// 当前线程在内核态下使用的栈顶地址
	pthread->self_kstack = (uint32_t*)((uint32_t)pthread + PG_SIZE);
	// 用于检测边界的魔数，避免压栈操作破坏 PCB 的基本信息
	pthread->stack_magic = *((uint32_t*) "iLym");
}

task_struct* thread_start(
	char* name, int prio,
	thread_func function,
	void* func_arg
) {
	task_struct* thread = get_kernel_pages(1);
	init_thread(thread, name, prio);
	thread_create(thread, function, func_arg);

	__asm__ __volatile__ (
		"movl %0, %%esp;"
		"pop %%ebp;"
		"pop %%ebx;"
		"pop %%edi;"
		"pop %%esi;"
		"ret;"
		: :"g"(thread->self_kstack): "memory"
	);
	return thread;
}