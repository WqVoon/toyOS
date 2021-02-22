#ifndef __KERNEL_PROCESS_H
#define __KERNEL_PROCESS_H

// 用户特权级3的栈
#define USER_STACK3_VADDR (0xc0000000 - 0x1000)
// 用户虚拟地址
#define USER_VADDR_START 0x8048000

void process_execute(void* filename, char* name);

#endif