#ifndef __KERNEL_DEBUG_H
#define __KERNEL_DEBUG_H

#include "print.h"
#include "interrupt.h"

void panic_spin(const char*, int, const char*, const char*);

#ifdef NDEBUG
	#define ASSERT(CONDITION)
#else
	#define ASSERT(CONDITION)\
		if (! (CONDITION)) panic_spin(__FILE__, __LINE__, __func__, #CONDITION);
#endif

#endif