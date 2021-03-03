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
void init(void);
extern void my_shell(void);

int main(void) {
	init_all();
	intr_enable();

	printk("\nPress any key to run shell...");

	int32_t buf;
	read(0, &buf, 1);
	process_execute(init, "init");

	while(1);
	return 0;
}

void init(void) {
	clear();
	uint32_t ret_pid = fork();
	if (ret_pid) {
		while (1);
	} else {
		my_shell();
	}
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