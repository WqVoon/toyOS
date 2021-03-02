#ifndef __LIB_USER_SYSCALL_H
#define __LIB_USER_SYSCALL_H

#include "stdint.h"

typedef enum {
	SYS_GETPID,
	SYS_WRITE,
	SYS_MALLOC,
	SYS_FREE,
	SYS_FORK,
	SYS_READ
} stscall_nr;

uint32_t getpid(void);

uint32_t write(int32_t fd, const void* buf, uint32_t count);

void syscall_init(void);

void* malloc(uint32_t size);

void free(void* ptr);

int16_t fork(void);

int32_t read(int32_t fd, void* buf, uint32_t count);

#endif