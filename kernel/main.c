#include "timer.h"
#include "ide.h"
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
#include "fs.h"

void task(void* arg);

int main(void) {
	init_all();
	intr_enable();
	clear();


	thread_start("T-1", 30, task, "This is T-1\n");
	thread_start("T-2", 20, task, "This is T-2\n");
	thread_start("T-3", 10, task, "This is T-3\n");
	show_tasks();

	thread_block(TASK_BLOCKED);
	return 0;
}

void task(void* arg) {
	for (int i=0; i<3; i++) {
		printk((char*)arg);
		for (int i=0; i<0x7fffff; i++);
	}
}