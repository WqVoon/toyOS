#ifndef __THREAD_H
#define __THREAD_H

#include "stdint.h"

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

	// 以下由 cpu 从低特权级进入高特权级时压入
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
	// 以下结构仅在线程第一次运行时使用，详见 kernel_thread 函数
	void (*unused_retaddr);
	thread_func* function;
	void* func_arg;
} thread_stack;

/* 进程或线程的 pcb，程序控制块 */
typedef struct {
	uint32_t* self_kstack;
	task_status status;
	uint8_t priority;
	char name[16];
	uint32_t stack_magic;
} task_struct;

task_struct* thread_start(char*, int, thread_func, void*);

#endif