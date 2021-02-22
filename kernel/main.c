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
#include "process.h"

int var = 0;
void thread_task(void* arg);
void process_task(void);

int main(void) {
	init_all();
	intr_enable();

	thread_start("thread", 31, thread_task, NULL);
	process_execute(process_task, "process");

	while(1);
	return 0;
}

void thread_task(void* arg) {
	while (1) {
		console_put_int(var);
		console_put_char(' ');
	}
}

void process_task(void) {
	/* 下面的 console_put_str 会引发 GP 异常 */
	// console_put_str("Wahahaha");

	while (1) {
		var++;
	}
}