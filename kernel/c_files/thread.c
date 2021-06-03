#include "timer.h"
#include "stdio.h"
#include "process.h"
#include "debug.h"
#include "print.h"
#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "memory.h"
#include "interrupt.h"

void process_activate(task_struct* p_thread);
// 任务调度函数指针，实际可能指向不同的调度算法
void (*schedule)(void);

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

/* 获取一个可用的 pid，内部是对静态函数 allocate_pid 的封装 */
int16_t fork_pid(void) {
	return allocate_pid();
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
 * TODO:当前仅实现了必要的任务切换以及方便调试的输出，实际资源的清理并没有做
 */
static void task_done() {
	task_struct* cur = running_thread();
	logk("Task `%s` ended\n", cur->name);
	thread_block(TASK_BLOCKED);
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
	strcpy(pthread->name, name);

	if (pthread == main_thread) {
		pthread->status = TASK_RUNNING;
	} else {
		pthread->status = TASK_READY;
	}

	task_struct* cur = running_thread();
	pthread->pid = cur->pid;
	pthread->parent_id = cur->parent_id;
	pthread->ticks = prio;
	pthread->priority = prio;
	pthread->created_timestamp = get_time_stamp();
	pthread->pgdir = NULL;
	// 当前线程在内核态下使用的栈顶地址
	pthread->self_kstack = (uint32_t*)((uint32_t)pthread + PG_SIZE);

	// 定义 stdin, stdout, stderr
	pthread->fd_table[0] = 0;
	pthread->fd_table[1] = 1;
	pthread->fd_table[2] = 2;
	// 其余的置为 -1
	for (int i=3; i<MAX_FILES_OPEN_PER_PROC; i++) {
		pthread->fd_table[i] = -1;
	}

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

	logk(
		"Task `%s` created, timestamp: %d, priority: %d\n",
		name, thread->created_timestamp, thread->priority
	);
	return thread;
}

/* 将 kernel 中的 main 函数完善为主线程 */
static void make_main_thread(void) {
	main_thread = running_thread();
	init_thread(main_thread, "main", 255);
	ASSERT(!elem_find(&thread_all_list, &main_thread->all_list_tag));
	list_append(&thread_all_list, &main_thread->all_list_tag);
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

/* 展示当前的所有任务 */
void show_tasks() {
	intr_status old_status = intr_disable();
	struct list* plist = &thread_ready_list;
	struct list_elem* elem = plist->head.next;

	printk("Tasks: ");
	while (elem != &plist->tail) {
		task_struct* task = elem2entry(task_struct, general_tag, elem);
		printk("%s ", task->name);
		elem = elem->next;
	}
	printk("\n");
	intr_set_status(old_status);
}

/* 实现 round robin 任务调度 */
static void schedule_round_robin(void) {
	ASSERT(intr_get_status() == INTR_OFF);

	task_struct* cur = running_thread();
	if (cur->status == TASK_RUNNING) {
		// 如果线程只是 cpu 时间片到了，将其加入就绪队列尾部
		ASSERT(! elem_find(&thread_ready_list, &cur->general_tag));
		list_append(&thread_ready_list, &cur->general_tag);
		cur->ticks = cur->priority;
		cur->status = TASK_READY;
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

	// 切换任务
	process_activate(next);
	switch_to(cur, next);
}

/* 实现 FCFS 任务调度 */
static void schedule_fcfs(void) {
	ASSERT(intr_get_status() == INTR_OFF);

	task_struct* cur = running_thread();
	// FCFS 是非抢占式任务调度，因此只有当前任务终结才会发生任务切换
	if (cur->status != TASK_DIED && cur->status != TASK_BLOCKED) {
		return;
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
	// 切换任务
	process_activate(next);
	switch_to(cur, next);
}

/* 实现 SJF 任务调度（非抢占式） */
static void schedule_sjf(void) {
	ASSERT(intr_get_status() == INTR_OFF);

	task_struct* cur = running_thread();
	// SJF 是非抢占式任务调度，因此只有当前任务终结才会发生任务切换
	if (cur->status != TASK_DIED && cur->status != TASK_BLOCKED) {
		return;
	}
	// if (cur->status == TASK_RUNNING) {
	// 	// 如果线程只是 cpu 时间片到了，将其加入就绪队列尾部
	// 	ASSERT(! elem_find(&thread_ready_list, &cur->general_tag));
	// 	list_append(&thread_ready_list, &cur->general_tag);
	// 	cur->ticks = cur->priority;
	// 	cur->status = TASK_READY;
	// }

	if (list_empty(&thread_ready_list)) {
		thread_unblock(idle_thread);
	}

	int min_value = 255;
	task_struct* next = NULL;
	struct list* plist = &thread_ready_list;
	thread_tag = plist->head.next;

	while (thread_tag != &plist->tail) {
		task_struct* task = elem2entry(task_struct, general_tag, thread_tag);
		if (task->priority < min_value) {
			min_value = task->priority;
			next = task;
		}
		thread_tag = thread_tag->next;
	}

	if (next == NULL) return;
	list_remove(&next->general_tag);
	next->status = TASK_RUNNING;
	// 切换任务
	process_activate(next);
	switch_to(cur, next);
}

/* 实现 HRRN 任务调度 */
static void schedule_hrrn(void) {
	ASSERT(intr_get_status() == INTR_OFF);

	task_struct* cur = running_thread();
	// HRRN 是非抢占式任务调度，因此只有当前任务终结才会发生任务切换
	if (cur->status != TASK_DIED && cur->status != TASK_BLOCKED) {
		return;
	}

	if (list_empty(&thread_ready_list)) {
		thread_unblock(idle_thread);
	}

	uint32_t max_value = 1;
	task_struct* next = NULL;
	struct list* plist = &thread_ready_list;
	thread_tag = plist->head.next;

	while (thread_tag != &plist->tail) {
		task_struct* task = elem2entry(task_struct, general_tag, thread_tag);

		// 响应比等于 1 + 等待时间/要求服务的时间
		uint32_t wait_time = get_time_stamp();
		uint32_t response_radio = \
			1 + (wait_time - task->created_timestamp) / task->priority;

		if (response_radio > max_value) {
			max_value = task->priority;
			next = task;
		}
		thread_tag = thread_tag->next;
	}

	if (next == NULL) return;
	list_remove(&next->general_tag);
	next->status = TASK_RUNNING;
	// 切换任务
	process_activate(next);
	switch_to(cur, next);
}

/* 用来在 schedulers 中用作下标来选择具体的调度算法 */
enum SCHEDULER_TYPE {
	SJF,
	FCFS,
	HRRN,
	ROUND_ROBIN,
};

/* 当前支持的任务调度算法列表 */
static void(*schedulers[])(void) = {
	schedule_sjf,
	schedule_fcfs,
	schedule_hrrn,
	schedule_round_robin,
};

/* 初始化线程环境 */
void thread_init(void) {
	put_str("thread_init start\n");
	list_init(&thread_ready_list);
	list_init(&thread_all_list);
	lock_init(&pid_lock);
	make_main_thread();
	idle_thread = thread_start("idle", 1, idle, NULL);
	// 设置具体的调度算法
	schedule = schedulers[ROUND_ROBIN];
	put_str("thread_init done\n");
}