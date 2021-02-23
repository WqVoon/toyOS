#include "syscall.h"
#include "print.h"
#include "thread.h"

#define SYSCALL_NR 32
typedef void* syscall;
syscall syscall_table[SYSCALL_NR];

#define _syscall0(NUMBER) ({\
	int retval;\
	__asm__ __volatile__ (\
		"int $0x80"\
		: "=a"(retval)\
		: "a"(NUMBER)\
		: "memory"\
	);\
	retval;\
})

/* 返回当前任务的pid */
uint32_t getpid(void) {
	return _syscall0(SYS_GETPID);
}

/* 返回当前任务的pid */
uint32_t sys_getpid(void) {
	return running_thread()->pid;
}

void syscall_init(void) {
	put_str("syscall_init start\n");
	syscall_table[SYS_GETPID] = sys_getpid;
	put_str("syscall_init done\n");
}