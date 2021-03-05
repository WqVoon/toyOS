#ifndef __LIB_USER_SYSCALL_H
#define __LIB_USER_SYSCALL_H

#include "stdint.h"
#include "dir.h"

typedef enum {
	SYS_GETPID,
	SYS_WRITE,
	SYS_MALLOC,
	SYS_FREE,
	SYS_FORK,
	SYS_READ,
	SYS_PUTCHAR,
	SYS_CLEAR,
	SYS_OPEN,
	SYS_CLOSE,
	SYS_OPENDIR,
	SYS_CLOSEDIR,
	SYS_READDIR,
	SYS_REWINDDIR
} stscall_nr;

uint32_t getpid(void);

uint32_t write(int32_t fd, const void* buf, uint32_t count);

void syscall_init(void);

void* malloc(uint32_t size);

void free(void* ptr);

int16_t fork(void);

int32_t read(int32_t fd, void* buf, uint32_t count);

void putchar(char ascii);

void clear(void);

int32_t open(const char* pathname, uint8_t flags);

int32_t close(int32_t fd);

dir_entry* readdir(dir* dir);

void rewinddir(dir* dir);

#endif