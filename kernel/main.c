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

int32_t open_file_with_tip(const char* pathname, oflags flag);
int32_t close_file_with_tip(int32_t fd);
void thread_task(void*);
void init(void);

int main(void) {
	init_all();
	intr_enable();

	/*
	TODO: 这个放在 thread_init 里会导致 PF 异常，尚不知道原因
	为了依然保证 init 进程的 pid 为 1，把 fork_pid 变成仅在创建新进程时才被调用
	线程会继承对应进程的 pid，main thread 和 idle thread 不处理
	*/
	printk("main pid: %d\n", getpid());
	thread_start("thread", 31, thread_task, NULL);
	process_execute(init, "init");
	while(1);
	return 0;
}

void thread_task(void* arg) {
	printf("kernel thread pid: %d\n", getpid());
}

void init(void) {
	uint32_t ret_pid = fork();
	if (ret_pid) {
		printf(
			"I am father, my pid is %d"
			", child pid is %d\n",
			getpid(), ret_pid
		);
	} else {
		printf(
			"I am child, my pid is %d"
			", ret pid is %d\n",
			getpid(), ret_pid
		);
	}
	while (1);
}

int32_t open_file_with_tip(const char* pathname, oflags flag) {
	int32_t fd = -1;
	if ((fd = sys_open(pathname, flag)) != -1) {
		printk("open file successful, fd: %d\n", fd);
	} else {
		printk("open file error\n");
	}
	return fd;
}

int32_t close_file_with_tip(int32_t fd) {
	if (sys_close(fd) != -1) {
		printk("close file successful, fd: %d\n", fd);
	} else {
		printk("close file error\n");
	}
}