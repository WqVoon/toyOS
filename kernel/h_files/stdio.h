#ifndef __USER_LIB_STDIO_H
#define __USER_LIB_STDIO_H

#include "stdint.h"

uint32_t sprintf(char* str, const char* format, ...);

uint32_t printf(const char* format, ...);

void printk(const char* format, ...);

#define logk(msg, ...)\
	printk("[DEBUG] " msg, ##__VA_ARGS__)

#endif