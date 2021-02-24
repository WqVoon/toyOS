#ifndef __LIB_USER_SYSCALL_H
#define __LIB_USER_SYSCALL_H

#include "stdint.h"

typedef enum {
	SYS_GETPID,
	SYS_WRITE,
	SYS_MALLOC,
	SYS_FREE
} stscall_nr;

uint32_t getpid(void);

uint32_t write(const char* str);

void syscall_init(void);

void* malloc(uint32_t size);

void free(void* ptr);

#endif