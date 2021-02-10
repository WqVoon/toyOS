#include "sync.h"
#include "string.h"
#include "print.h"
#include "init.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"

#define LOOP_TIMES 100000
uint64_t value = 0;

void thread_task(void*);

int main(void) {
	put_str("I am kernel\n");
	init_all();
	intr_enable();

	while(1);
	return 0;
}

void thread_task(void* arg) {
	const char* str = (const char*)arg;

	put_str(arg);
}