#ifndef __LIB_USER_SYSCALL_H
#define __LIB_USER_SYSCALL_H

#include "stdint.h"

typedef enum {
	SYS_GETPID
} stscall_nr;

uint32_t getpid(void);

void syscall_init(void);

#endif