#include "init.h"
#include "print.h"
#include "interrupt.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "keyboard.h"
#include "syscall.h"

extern void timer_init(void);
extern void tss_init();

/**
 * 初始化所有模块
 */
void init_all() {
	put_str("init_all\n");
	idt_init();
	mem_init();
	timer_init();
	thread_init();
	console_init();
	keyboard_init();
	tss_init();
	syscall_init();
}