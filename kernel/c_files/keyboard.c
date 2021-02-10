#include "keyboard.h"
#include "print.h"
#include "interrupt.h"
#include "io.h"
#include "global.h"

// 键盘 buffer 寄存器端口号为 0x60
#define KBD_BUF_PORT 0x60

static void intr_keyboard_handler(void) {
	put_str("myLym(");
	/* 这里的 inb 读取是必须的，否则 8042 不会再响应中断 */
	put_int(inb(KBD_BUF_PORT));
	put_str(")");
	return;
}

/* 注册键盘中断处理程序 */
void keyboard_init(void) {
	put_str("keyboard init start\n");
	register_handler(0x21, intr_keyboard_handler);
	put_str("keyboard init done\n");
}