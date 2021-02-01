#ifndef __LIB_KERNEL_BITMAP_H
#define __LIB_KERNEL_BITMAP_H

#include "global.h"

#define BITMAP_MASK 1
typedef struct {
	uint32_t btmp_bytes_len;
	uint8_t* bits;
} bitmap;

void bitmap_init(bitmap*);
uint8_t bitmap_scan_test(bitmap*, uint32_t);
int bitmap_scan(bitmap*, uint32_t);
void bitmap_set(bitmap*, uint32_t, int8_t);

#endif