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

	char* buf = sys_malloc(1024);
	printk("\nRead start\n");
	read(0, buf, 10);
	printk("Read end\n");
	printk("Content: %s\n", buf);

	// process_execute(init, "init");
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