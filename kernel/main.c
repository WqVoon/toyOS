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


	thread_start("T-1", 31, task, "This is T-1\n");
	thread_start("T-2", 31, task, "This is T-2\n");


	while (1);
	return 0;
}

void task(void* arg) {
	printk((char*)arg);
}