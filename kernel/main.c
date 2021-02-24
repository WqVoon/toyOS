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

void process_task(void);

int main(void) {
	init_all();
	intr_enable();

	process_execute(process_task, "process");
	write("\nThis is main\n");

	while(1);
	return 0;
}

void process_task(void) {
	/* 下面的 console_put_str 会引发 GP 异常 */
	// console_put_str("Wahahaha");
	write("This is process\n");
	while (1);
}