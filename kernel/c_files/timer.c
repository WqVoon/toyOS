#include "debug.h"
#include "stdint.h"
#include "thread.h"
#include "interrupt.h"

uint32_t ticks;

/* 时钟的中断处理函数 */
static void intr_timer_handler(void) {

	task_struct* cur_thread = running_thread();
	// 如果 stack_magic 的值不正确，那么程序已经没必要运行了
	ASSERT(cur_thread->stack_magic == *((uint32_t*) "iLym"));

	cur_thread->elapsed_ticks++;
	ticks++;

	if (cur_thread->ticks == 0) {
		schedule();
	} else {
		cur_thread->ticks--;
	}
}

void timer_init(void) {
	put_str("timer_init start\n");
	register_handler(0x20, intr_timer_handler);
	put_str("timer_init done\n");
}