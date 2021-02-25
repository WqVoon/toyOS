#include "sync.h"
#include "string.h"
#include "print.h"
#include "init.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "keyboard.h"
#include "global.h"
#include "process.h"
#include "syscall.h"
#include "stdio.h"

void thread_task(void*);
void process_task(void);

int main(void) {
	init_all();
	intr_enable();

	/*
	由于内核线程应该共享同一份虚拟地址池，
	所以 main, thread-a, thread-b 共计 5 次的申请得到的地址应该呈累加关系
	而每个用户进程有自己的虚拟地址池，所以不同进程申请得到的虚拟地址有可能相同
	*/
	printf("Main     addr: %x\n", malloc(33));
	thread_start("thread-a", 31, thread_task, NULL);
	thread_start("thread-b", 31, thread_task, NULL);
	process_execute(process_task, "process-a");
	process_execute(process_task, "process-b");

	while(1);
	return 0;
}

void thread_task(void* arg) {
	printf("Thread  addr1: %x\n", malloc(33));
	printf("Thread  addr2: %x\n", malloc(33));
}

void process_task(void) {
	/* 下面的 console_put_str 会引发 GP 异常 */
	// console_put_str("Wahahaha");
	printf("Process addr1: %x\n", malloc(33));
	printf("Process addr2: %x\n", malloc(33));
	while (1);
}