#include "debug.h"
#include "stdint.h"
#include "thread.h"
#include "interrupt.h"

// 8253 每秒产生的中断数，默认约 18 次
#define IRQ0_FREQUENCY 18

#define mil_seconds_per_intr (1000 / IRQ0_FREQUENCY)

// 自中断开启以来的总滴答数
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

/* 以 ticks 为单位的 sleep，不是很精确 */
static void ticks_to_sleep(uint32_t sleep_ticks) {
	uint32_t start_tick = ticks;
	uint32_t now_tick = 0;

	do {
		thread_yeild();
		now_tick = (ticks > start_tick? ticks: ticks + (uint32_t)-1);
	} while (now_tick - start_tick < sleep_ticks);
}

/* 以毫秒为单位的 sleep */
void mtime_sleep(uint32_t m_seconds) {
	uint32_t sleep_ticks = DIV_ROUND_UP(m_seconds, mil_seconds_per_intr);
	ASSERT(sleep_ticks > 0);
	ticks_to_sleep(sleep_ticks);
}

void timer_init(void) {
	put_str("timer_init start\n");
	register_handler(0x20, intr_timer_handler);
	put_str("timer_init done\n");
}