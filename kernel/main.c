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

	/*
	验证了原书在 sys_close 函数实现上存在 bug
	 "file->fd_inode->write_deny = false" 这句被 fd2 的关闭执行到
	 结果 fd3 可以用写文件的方式打开
	*/
	int fd1 = open_file_with_tip("/toMyLove", O_WRONLY);
	int fd2 = open_file_with_tip("/toMyLove", O_RDONLY);
	close_file_with_tip(fd2);
	int fd3 = open_file_with_tip("/toMyLove", O_WRONLY);

	close_file_with_tip(fd1);
	close_file_with_tip(fd3);

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