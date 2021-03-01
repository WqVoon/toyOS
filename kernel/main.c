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

	const char* msg = "To Lym:\n I love u\n";
	uint32_t msg_len = strlen(msg);
	void* buf = sys_malloc(512);

	int wr_fd = open_file_with_tip("/ToMyLove", O_CREAT|O_WRONLY);
	sys_write(wr_fd, msg, msg_len);

	int rd_fd1 = open_file_with_tip("/ToMyLove", O_RDONLY);
	printk("-----\n");
	printk("1.read %d bytes\n", sys_read(rd_fd1, buf, 10));
	printk("2.read %d bytes\n", sys_read(rd_fd1, buf+10, 10));
	printk("3.read %d bytes\n", sys_read(rd_fd1, buf+10, 10));
	*((char*)buf + msg_len) = 0;
	printk("-----\n%s\n", buf);

	sys_lseek(rd_fd1, 0, SEEK_SET);
	memset(buf, 0, msg_len);
	sys_read(rd_fd1, buf, msg_len);
	printk("-----\n%s", buf);

	close_file_with_tip(wr_fd);
	close_file_with_tip(rd_fd1);

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