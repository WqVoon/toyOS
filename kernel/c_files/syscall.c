#include "syscall.h"
#include "print.h"
#include "thread.h"
#include "console.h"
#include "string.h"
#include "memory.h"

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

/* 简易版的 write */
uint32_t write(const char* str) {
	return _syscall1(SYS_WRITE, str);
}

/* 申请 size 字节大小的内存，并返回首地址 */
void* malloc(uint32_t size) {
	return (void*)_syscall1(SYS_MALLOC, size);
}

/* 释放 ptr 指向的内存 */
void free(void* ptr) {
	_syscall1(SYS_FREE, ptr);
}

/*---------- 内核态使用，即需要被注册到 syscall_table 的具体实现 ----------*/

uint32_t sys_getpid(void) {
	return running_thread()->pid;
}

uint32_t sys_write(const char* str) {
	console_put_str(str);
	return strlen(str);
}

void syscall_init(void) {
	put_str("syscall_init start\n");
	syscall_table[SYS_GETPID] = sys_getpid;
	syscall_table[SYS_WRITE]  = sys_write;
	syscall_table[SYS_MALLOC] = sys_malloc;
	syscall_table[SYS_FREE]   = sys_free;
	put_str("syscall_init done\n");
}