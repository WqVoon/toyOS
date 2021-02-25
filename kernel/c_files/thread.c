#include "debug.h"
#include "print.h"
#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "memory.h"
#include "interrupt.h"

void process_activate(task_struct* p_thread);

// 主线程的 PCB
task_struct* main_thread;
// 就绪队列
struct list thread_ready_list;
// 全部任务队列
struct list thread_all_list;
// 用于保存队列中的线程结点
static struct list_elem* thread_tag;
// idle 线程
task_struct* idle_thread;

extern void switch_to(task_struct* cur, task_struct* next);

/* 获取当前线程的 PCB 指针 */
task_struct* running_thread() {
	uint32_t esp;
	__asm__ __volatile__ ("mov %%esp, %0" : "=g"(esp));
	return (task_struct*) (esp & 0xfffff000);
}

lock pid_lock;
static int16_t allocate_pid(void) {
	static int16_t next_pid = 0;
	lock_acquire(&pid_lock);
	next_pid++;
	lock_release(&pid_lock);
	return next_pid;
}

/**
 * 由 kernel_thread 去执行 function(fun_arg)
 * 该函数作为 thread_stack 中的 eip 由 ret 指令跳转并执行
 * function 和 func_arg 也在 thread_stack 中被定义成同名变量
 * 由于 thread_stack.unused_retaddr 的存在，该函数可以正常使用两个参数
 */
static void kernel_thread(thread_func* function, void* func_arg) {
	// 打开中断来避免后面的时钟中断被屏蔽而无法调度其他线程
	intr_enable();
	function(func_arg);
}

/**
 * 为了让线程函数执行结束后内核依然能正常运行，该函数赋值给 unused_retaddr
 * TODO:当前仅做无限循环，后面要清理其页表以及从 thread_all_list 中移除
 */
static void task_done() {
	while (1);
}

/* 初始化线程栈 thread_stack */
void thread_create(task_struct* pthread, thread_func function, void* func_arg) {
	pthread->self_kstack -= sizeof(intr_stack);

	pthread->self_kstack -= sizeof(thread_stack);

	thread_stack* kthread_stack = (thread_stack*)pthread->self_kstack;
	kthread_stack->unused_retaddr = task_done;
	kthread_stack->eip = kernel_thread;
	kthread_stack->function = function;
	kthread_stack->func_arg = func_arg;
	kthread_stack->ebp = kthread_stack->ebx = \
	kthread_stack->esi = kthread_stack->edi = 0;
}

/* 在 PCB 中初始化线程基本信息，信息位于 PCB 所在页的低地址 */
void init_thread(task_struct* pthread, char* name, int prio) {
	memset(pthread, 0, sizeof(*pthread));
	pthread->pid = allocate_pid();
	strcpy(pthread->name, name);

	if (pthread == main_thread) {
		pthread->status = TASK_RUNNING;
	} else {
		pthread->status = TASK_READY;
	}

	pthread->ticks = prio;
	pthread->priority = prio;
	pthread->elapsed_ticks = 0;
	pthread->pgdir = NULL;
	// 当前线程在内核态下使用的栈顶地址
	pthread->self_kstack = (uint32_t*)((uint32_t)pthread + PG_SIZE);
	// 用于检测边界的魔数，避免压栈操作破坏 PCB 的基本信息
	pthread->stack_magic = *((uint32_t*) "iLym");
}

/* 创建一个线程，并将其 PCB 初始化为参数的值 */
task_struct* thread_start(
	char* name, int prio,
	thread_func function,
	void* func_arg
) {
	task_struct* thread = get_kernel_pages(1);
	init_thread(thread, name, prio);
	thread_create(thread, function, func_arg);

	ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
	list_append(&thread_ready_list, &thread->general_tag);

	ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
	list_append(&thread_all_list, &thread->all_list_tag);

	return thread;
}

/* 将 kernel 中的 main 函数完善为主线程 */
static void make_main_thread(void) {
	main_thread = running_thread();
	init_thread(main_thread, "main", 31);
	ASSERT(!elem_find(&thread_all_list, &main_thread->all_list_tag));
	list_append(&thread_all_list, &main_thread->all_list_tag);
}

/* 实现任务调度 */
void schedule(void) {
	ASSERT(intr_get_status() == INTR_OFF);

	task_struct* cur = running_thread();
	if (cur->status == TASK_RUNNING) {
		// 如果线程只是 cpu 时间片到了，将其加入就绪队列尾部
		ASSERT(! elem_find(&thread_ready_list, &cur->general_tag));
		list_append(&thread_ready_list, &cur->general_tag);
		// 下面三行代码用于营造 ready_list 为空的环境，从而测试 idle 线程
		if (cur != idle_thread) {
			list_remove(&cur->general_tag);
		}
		cur->ticks = cur->priority;
		cur->status = TASK_READY;
	} else {
		// TODO: ?
		// 若此线程需要某些事件发生后才能继续上 cpu 运行
		// 不需要将其加入队列，因为当前线程不在就绪队列中
	}

	if (list_empty(&thread_ready_list)) {
		thread_unblock(idle_thread);
	}
	// 获取队列队首的 elem
	thread_tag = NULL;
	thread_tag = list_pop(&thread_ready_list);
	// 获取 elem 对应的 PCB
	task_struct* next = elem2entry(task_struct, general_tag, thread_tag);
	next->status = TASK_RUNNING;

	process_activate(next);

	// 切换任务
	switch_to(cur, next);
}

/* 系统空闲时运行的任务 */
static void idle(void* arg) {
	while (1) {
		thread_block(TASK_BLOCKED);
		__asm__ __volatile__ (
			"sti; hlt"
			::: "memory"
		);
	}
}

/* 初始化线程环境 */
void thread_init(void) {
	put_str("thread_init start\n");
	list_init(&thread_ready_list);
	list_init(&thread_all_list);
	lock_init(&pid_lock);
	make_main_thread();
	idle_thread = thread_start("idle", 10, idle, NULL);
	put_str("thread_init done\n");
}

/* 将当前线程阻塞，并把其状态标记为 stat */
void thread_block(task_status stat) {
	ASSERT(
		(stat == TASK_BLOCKED)
		|| (stat == TASK_WAITING)
		|| (stat == TASK_HANGING)
	);

	intr_status old_status = intr_disable();

	/*
	 将当前线程的 status 改变后，就不会加入到 thread_ready_list 中
	 从而通过主动发起 schedule 实现线程阻塞
	*/
	task_struct* cur_thread = running_thread();
	cur_thread->status = stat;
	schedule();

	intr_set_status(old_status);
}

/* 将线程 pthread 解除阻塞 */
void thread_unblock(task_struct* pthread) {
	intr_status old_status = intr_disable();

	task_status task_stat = pthread->status;
	ASSERT(
		(task_stat == TASK_BLOCKED)
		|| (task_stat == TASK_WAITING)
		|| (task_stat == TASK_HANGING)
	);

	ASSERT(! elem_find(&thread_ready_list, &pthread->general_tag));
	list_push(&thread_ready_list, &pthread->general_tag);
	pthread->status = TASK_READY;

	intr_set_status(old_status);
}

/* 主动让出 cpu，换其他线程运行 */
void thread_yeild(void) {
	task_struct* cur = running_thread();
	intr_status old_status = intr_disable();
	ASSERT(! elem_find(&thread_ready_list, &cur->general_tag));
	list_append(&thread_ready_list, &cur->general_tag);
	cur->status = TASK_READY;
	schedule();
	intr_set_status(old_status);
}