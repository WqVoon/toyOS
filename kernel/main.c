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
#include "syscall.h"

uint32_t pid = 0;
void thread_task(void*);
void process_task(void);

int main(void) {
	init_all();
	intr_enable();

	process_execute(process_task, "process");
	thread_start("thread", 31, thread_task, NULL);
	console_put_str("main pid: ");
	console_put_int(getpid());
	console_put_char('\n');

	while(1);
	return 0;
}

void thread_task(void* arg) {
	console_put_str("process pid: ");
	console_put_int(pid);

	console_put_str("\nthread pid: ");
	console_put_int(getpid());
	console_put_char('\n');
}

void process_task(void) {
	/* 下面的 console_put_str 会引发 GP 异常 */
	// console_put_str("Wahahaha");
	pid = getpid();
	while (1);
}