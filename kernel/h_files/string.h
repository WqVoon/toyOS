#ifndef __LIB_STRING_H
#define __LIB_STRING_H

#include "stdint.h"

void memset(void*, uint8_t, uint32_t);

void memcpy(void*, const void*, uint32_t);

int memcmp(const void*, const void*, uint32_t);

char* strcpy(char*, const char*);

uint32_t strlen(const char*);

int8_t strcmp(const char*, const char*);

char* strchr(const char*, const uint8_t);

char* strrchr(const char*, const uint8_t);

char* strcat(char*, const char*);

uint32_t strchrs(const char*, uint8_t);

#endif