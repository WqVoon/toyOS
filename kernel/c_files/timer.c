#include "io.h"
#include "debug.h"
#include "stdint.h"
#include "thread.h"
#include "interrupt.h"

// 8253 每秒产生的中断数，默认约 18 次
#define IRQ0_FREQUENCY       100
// 计数器 0 的工作脉冲信号频率
#define INPUT_FREQUENCY      1193180
// 将要设置的计数器初值
#define COUNTER0_VALUE       INPUT_FREQUENCY / IRQ0_FREQUENCY
// 计数器 0 的端口号
#define COUNTER0_PORT        0x40
// 在控制字中选择计数器号码
#define COUNTER0_NO          0
// 设置工作模式为比率发生器
#define COUNTER_MODE         2
// 先读写低 8 位，再读写高 8 位
#define READ_WRITE_LATCH     3
// 控制字寄存器的端口
#define PIT_CONTROL_PORT     0x43
// sleep 系列函数的换算
#define mil_seconds_per_intr (1000 / IRQ0_FREQUENCY)

// 自中断开启以来的总滴答数
uint32_t ticks;

extern void(*schedule)(void);
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

/*
把操作的计数器 counter_no、读写锁属性 rwl、计数器模式 counter_mode 写入模式控制寄存器
并将计数初值设置为 counter_value
*/
static void frequency_set(
	uint8_t counter_port,
	uint8_t counter_no,
	uint8_t rwl,
	uint8_t counter_mode,
	uint16_t counter_value
) {
	outb(PIT_CONTROL_PORT, (uint8_t)(counter_no<<6 | rwl<<4 | counter_mode << 1));
	outb(counter_port, (uint8_t)counter_value);
	outb(counter_port, (uint8_t)counter_value >> 8);
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
	frequency_set(
		COUNTER0_PORT,
		COUNTER0_NO,
		READ_WRITE_LATCH,
		COUNTER_MODE,
		COUNTER0_VALUE
	);
	register_handler(0x20, intr_timer_handler);
	put_str("timer_init done\n");
}