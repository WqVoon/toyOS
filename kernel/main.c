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

	printf("\nfd0:\n");
	int fd = open_file_with_tip("/toMyLove", O_CREAT);
	close_file_with_tip(fd);

	printf("\nfd1:\n");
	fd = open_file_with_tip("/toMyLove", O_RDWR);
	sys_write(fd, title, strlen(title));
	close_file_with_tip(fd);

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