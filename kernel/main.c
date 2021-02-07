#include "string.h"
#include "print.h"
#include "init.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"

void thread_task(void*);

int main(void) {
	put_str("I am kernel\n");
	init_all();

	put_str("Main-Thread PCB Addr: ");
	put_int((uint32_t) running_thread());
	put_char('\n');

	thread_start("k_thread_a", 31, thread_task, "argA ");
	thread_start("k_thread_b", 8, thread_task, "argB ");

	intr_enable();
	
	put_str("Main ");

	while(1);
	return 0;
}

void thread_task(void* arg) {
	char* str = (char*) arg;
	put_str(str);
}