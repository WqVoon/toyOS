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
	lock plock;
	lock_init(&plock);

	put_str("I am kernel\n");
	init_all();

	thread_start("k_thread_a", 1, thread_task, &plock);
	thread_start("k_thread_b", 1, thread_task, &plock);

	intr_enable();

	for (int i=0; i<LOOP_TIMES; i++) {
		// lock_acquire(&plock);
		value += 1;
		// lock_release(&plock);
	}

	while(1) {
		put_str("Value: ");
		put_int(value);
		put_char('\n');
	}

	return 0;
}

void thread_task(void* arg) {
	lock* plock = (lock*)arg;
	for (int i=0; i<LOOP_TIMES; i++) {
		// lock_acquire(plock);
		value += 1;
		// lock_release(plock);
	}
}