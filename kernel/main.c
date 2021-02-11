#include "sync.h"
#include "string.h"
#include "print.h"
#include "init.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"
#include "keyboard.h"

#define LOOP_TIMES 100000
uint64_t value = 0;

void thread_task(void*);

int main(void) {
	put_str("I am kernel\n");
	init_all();
	intr_enable();

	thread_start("T-a", 31, thread_task, "A_");
	thread_start("T-b", 31, thread_task, "B_");

	while(1);
	return 0;
}

void thread_task(void* arg) {
	const char* str = (const char*)arg;
	while (1) {
		intr_status old_status = intr_disable();
		if (! ioq_empty(&kbd_buf)) {
			put_str(arg);
			char byte = ioq_getchar(&kbd_buf);
			put_char(byte);
			put_char(' ');
		}
		intr_set_status(old_status);
	}
}