#include "syscall.h"
#include "fs.h"
#include "print.h"
#include "thread.h"
#include "console.h"
#include "string.h"
#include "memory.h"
#include "fork.h"

#define SYSCALL_NR 32
typedef void* syscall;
syscall syscall_table[SYSCALL_NR];

#define _syscall3(NUMBER, ARG1, ARG2, ARG3) ({\
	int retval;\
	__asm__ __volatile__ (\
		"int $0x80"\
		: "=a"(retval)\
		: "a"(NUMBER), "b"(ARG1), "c"(ARG2), "d"(ARG3)\
		: "memory"\
	);\
	retval;\
})

#define _syscall0(NUMBER)             _syscall3(NUMBER, NULL, NULL, NULL)
#define _syscall1(NUMBER, ARG1)       _syscall3(NUMBER, ARG1, NULL, NULL)
#define _syscall2(NUMBER, ARG1, ARG2) _syscall3(NUMBER, ARG1, ARG2, NULL)

/*---------- 用户态使用，可直接当作一般函数来调用 ----------*/

/* 返回当前任务的pid */
uint32_t getpid(void) {
	return _syscall0(SYS_GETPID);
}

/* 将 buf 中 count 个字符写入文件描述符 fd */
uint32_t write(int32_t fd, const void* buf, uint32_t count) {
	return _syscall3(SYS_WRITE, fd, buf, count);
}

/* 申请 size 字节大小的内存，并返回首地址 */
void* malloc(uint32_t size) {
	return (void*)_syscall1(SYS_MALLOC, size);
}

/* 释放 ptr 指向的内存 */
void free(void* ptr) {
	_syscall1(SYS_FREE, ptr);
}

/* fork 一个子进程出来 */
int16_t fork(void) {
	return _syscall0(SYS_FORK);
}

/* 从文件描述符 fd 中读取 count 个字节到 buf 中 */
int32_t read(int32_t fd, void* buf, uint32_t count) {
	return _syscall3(SYS_READ, fd, buf, count);
}

/*---------- 内核态使用，即需要被注册到 syscall_table 的具体实现 ----------*/

uint32_t sys_getpid(void) {
	return running_thread()->pid;
}

void syscall_init(void) {
	put_str("syscall_init start\n");
	syscall_table[SYS_GETPID] = sys_getpid;
	syscall_table[SYS_WRITE]  = sys_write;
	syscall_table[SYS_MALLOC] = sys_malloc;
	syscall_table[SYS_FREE]   = sys_free;
	syscall_table[SYS_FORK]   = sys_fork;
	syscall_table[SYS_READ]   = sys_read;
	put_str("syscall_init done\n");
}