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

	/*
	分别申请了 33 和 63 个 Byte 的空间
	则会选择 block_size 为 64 Byte 的 arena
	这样第一次分配得到的地址应该是 0x...00C，因为这一页前面有 0xC 大小的 sizeof(arena)
	而且第二次分配得到的地址应该是 0x...04C，因为两块内存大小应该差 64 Byte
	*/
	printf("First  malloc addr: %x\n", sys_malloc(33));
	printf("Second malloc addr: %x\n", sys_malloc(63));

	while(1);
	return 0;
}

void process_task(void) {
	/* 下面的 console_put_str 会引发 GP 异常 */
	// console_put_str("Wahahaha");
	printf("%s pid: %d\n", "User process", -1 * getpid());
	while (1);
}