#include "sync.h"
#include "string.h"
#include "print.h"
#include "init.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "keyboard.h"
#include "global.h"

void thread_task(void*);

int main(void) {
	put_str("I am kernel\n");
	init_all();
	intr_enable();

	gdt_desc c1 = *(gdt_desc*)0x908;
	gdt_desc c2 = *(gdt_desc*)0x928;

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