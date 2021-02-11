#include "sync.h"
#include "string.h"
#include "print.h"
#include "init.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "keyboard.h"

#define LOOP_TIMES 100000
uint64_t value = 0;

void thread_task(void*);

int main(void) {
	put_str("I am kernel\n");
	init_all();
	intr_enable();

	thread_start("T-a", 1, thread_task, "A");
	thread_start("T-b", 1, thread_task, "B");
	thread_start("T-c", 1, thread_task, "C");
	thread_start("T-d", 1, thread_task, "D");
	thread_start("T-e", 1, thread_task, "E");

	while(1);
	return 0;
}

void thread_task(void* arg) {
	const char* str = (const char*)arg;

	while (1) {
		console_put_str("This is a msg from Thread-");
		console_put_str(str);
		console_put_str(" ");
	}
}