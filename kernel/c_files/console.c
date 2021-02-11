#include "console.h"
#include "print.h"
#include "sync.h"

static lock console_lock;
extern void set_cursor(uint32_t);
extern uint32_t get_cursor(void);

void console_init() {
	lock_init(&console_lock);
}

void console_acquire() {
	lock_acquire(&console_lock);
}

void console_release() {
	lock_release(&console_lock);
}

/* 设置当前光标的位置 */
void console_set_cursor(uint32_t idx) {
	console_acquire();
	set_cursor(idx);
	console_release();
}

/* 获取当前光标的位置（未乘二） */
uint32_t console_get_cursor(void) {
	console_acquire();
	uint32_t ret = get_cursor();
	console_release();
	return ret;
}

void console_put_str(const char* str) {
	console_acquire();
	put_str(str);
	console_release();
}

void console_put_char(uint8_t asci) {
	console_acquire();
	put_char(asci);
	console_release();
}

void console_put_int(uint32_t num) {
	console_acquire();
	put_int(num);
	console_release();
}