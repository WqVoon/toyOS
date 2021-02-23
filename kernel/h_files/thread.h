#ifndef __THREAD_H
#define __THREAD_H

#include "stdint.h"
#include "list.h"
#include "memory.h"

#define PG_SIZE 4096

/* 自定义的通用函数类型，将被用在很多线程函数中作为参数类型 */
typedef void thread_func(void*);

/* 进程或线程的几种状态 */
typedef enum {
	TASK_RUNNING,
	TASK_READY,
	TASK_BLOCKED,
	TASK_WAITING,
	TASK_HANGING,
	TASK_DIED
} task_status;

/* 中断栈，在 PCB 的最顶端 */
typedef struct {
	uint32_t vec_no;
	uint32_t edi;
	uint32_t esi;
	uint32_t ebp;
	// 虽然 pushad 会把 esp 压入，但由于其是变化的，所以会被 popad 忽略
	uint32_t esp_dummy;
	uint32_t ebx;
	uint32_t edx;
	uint32_t ecx;
	uint32_t eax;
	uint32_t gs;
	uint32_t fs;
	uint32_t es;
	uint32_t ds;

	// 以下做中断恢复用，其中 esp 和 ss 仅在涉及特权级变换时被 cpu 压入
	uint32_t err_code;
	void (*eip) (void);
	uint32_t cs;
	uint32_t eflags;
	void* esp;
	uint32_t ss;
} intr_stack;

/* 线程栈 */
typedef struct {
	uint32_t ebp;
	uint32_t ebx;
	uint32_t edi;
	uint32_t esi;
	// 该结构在线程第一次运行时指向 kernel_thread
	// 其他的时候指向 switch_to 的返回地址
	void (*eip) (thread_func* func, void* func_arg);
	void (*unused_retaddr);
	// 以下结构仅在线程第一次运行时使用，详见 kernel_thread 函数
	thread_func* function;
	void* func_arg;
} thread_stack;

/* 进程或线程的 pcb，程序控制块 */
typedef struct __task_struct {
	uint32_t* self_kstack;
	int16_t pid;
	task_status status;
	uint8_t priority;
	char name[16];

	// 任务当前的 ticks ，每次加入到 ready 队列时置为 priority
	// 占用 cpu 其间每次发生时钟中断时减一，为零则让出 cpu
	uint32_t ticks;
	// 任务从上 cpu 运行后至今一共占用了多少 ticks，只增不减
	uint32_t elapsed_ticks;
	// 其他 list 中的结点标记，用于表示此任务当前的状态
	// 比如若该标记在 thread_ready_list 中则表示当前任务出于就绪状态
	struct list_elem general_tag;
	// thread_all_list 中的结点标记，用于表示此任务属于一个合法的线程
	// TODO:线程若被创建则一定在 thread_all_list 中
	struct list_elem all_list_tag;
	// 进程自己页表的虚拟地址，若当前任务为线程则该项为 NULL
	uint32_t* pgdir;
	// 进程自己的虚拟地址池
	virtual_addr userprog_vaddr;
	// 魔数，用于检测 PCB 信息是否被损坏
	uint32_t stack_magic;
} task_struct;

task_struct* thread_start(char*, int, thread_func, void*);
task_struct* running_thread();
void thread_init(void);
void schedule(void);
void thread_block(task_status);
void thread_unblock(task_struct*);
void init_thread(task_struct* pthread, char* name, int prio);
void thread_create(task_struct* pthread, thread_func function, void* func_arg);

#endif