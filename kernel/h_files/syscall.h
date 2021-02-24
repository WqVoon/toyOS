#ifndef __LIB_USER_SYSCALL_H
#define __LIB_USER_SYSCALL_H

#include "stdint.h"

typedef enum {
	SYS_GETPID,
	SYS_WRITE
} stscall_nr;

uint32_t getpid(void);

uint32_t write(const char* str);

void syscall_init(void);

#endif