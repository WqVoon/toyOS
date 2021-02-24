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

void process_task(void);

int main(void) {
	init_all();
	intr_enable();

	void* ptr = malloc(33);
	/*
	根据内存管理策略，如果下面的 malloc(34) 的注释保留，那么两次申请的内存应该在同一个虚拟地址
	因为 free 被执行时，内存对应的 arena 会被整体释放
	如果去掉注释，那么两次应该相差 64Byte ，因为被释放的内存在 free_list 的末尾
	*/
	// ptr = malloc(34);
	printf("First  malloc addr: %x\n", ptr);
	free(ptr);
	printf("Second malloc addr: %x\n", malloc(63));

	while(1);
	return 0;
}

void process_task(void) {
	/* 下面的 console_put_str 会引发 GP 异常 */
	// console_put_str("Wahahaha");
	printf("%s pid: %d\n", "User process", -1 * getpid());
	while (1);
}