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

int main(void) {
	init_all();
	intr_enable();

	const char* title = "To Lym:\n";
	const char* msg   = "  I love u\n";

	/*
	这里验证了原书存在的一个 bug
		在创建新文件时如果带有可写的模式，那么它不会对 write_deny 置位
		这样就在同一时刻同一文件有两个可写的文件描述符

	此时修复后，因为最初的 fd 没有关闭，后续两次对文件的写模式打开都无法成功
	*/
	printf("\nfd0:\n");
	int fd = open_file_with_tip("/toMyLove", O_CREAT|O_WRONLY);

	printf("\nfd1:\n");
	fd = open_file_with_tip("/toMyLove", O_RDWR);
	sys_write(fd, title, strlen(title));

	printf("\nfd2:\n");
	fd = open_file_with_tip("/toMyLove", O_RDWR);
	sys_write(fd, msg, strlen(msg));
	close_file_with_tip(fd);

	printf("\nfd3:\n");
	fd = open_file_with_tip("/toMyLove", O_RDONLY);
	sys_write(fd, msg, strlen(msg));
	close_file_with_tip(fd);

	while(1);
	return 0;
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