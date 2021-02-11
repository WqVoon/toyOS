#include "ioqueue.h"
#include "keyboard.h"
#include "print.h"
#include "interrupt.h"
#include "io.h"
#include "global.h"

// 键盘缓冲区
ioqueue kbd_buf;

// #define LOG(msg) put_str(msg "\n")
#define LOG(msg)

// 键盘 buffer 寄存器端口号为 0x60
#define KBD_BUF_PORT 0x60

// 用转义字符定义部分控制字符
#define esc       '\x1b'
#define backspace '\b'
#define tab       ' '
#define enter     '\r'
#define delete    '\x7f'

// 不可见字符一律定义为 0
#define char_invisible 0
#define ctrl_l_char    char_invisible
#define ctrl_r_char    char_invisible
#define shift_l_char   char_invisible
#define shift_r_char   char_invisible
#define alt_l_char     char_invisible
#define alt_r_char     char_invisible
#define caps_lock_char char_invisible

// 定义控制字符的通码和断码
#define shift_l_make   0x2a
#define shift_r_make   0x36
#define alt_l_make     0x38
#define alt_r_make     0xe038
#define alt_r_break    0xe0b8
#define ctrl_l_make    0x1d
#define ctrl_r_make    0xe01d
#define ctrl_r_break   0xe09d
#define caps_lock_make 0x3a

/*
定义如下变量用于记录相应键位是否被按下
*/
static uint8_t ctrl_status, shift_status,
alt_status, caps_lock_status, ext_scancode;

// 以通码为索引的二维数组
static char keymap[][2] = {
	{0, 0},
	{esc, esc}, {'1', '!'}, {'2', '@'}, {'3', '#'}, {'4', '$'}, {'5', '%'},
	{'6', '^'}, {'7', '&'}, {'8', '*'}, {'9', '('}, {'0', ')'}, {'-', '_'},
	{'=', '+'}, {backspace, backspace}, {tab, tab}, {'q', 'Q'}, {'w', 'W'},
	{'e', 'E'}, {'r', 'R'}, {'t', 'T'}, {'y', 'Y'}, {'u', 'U'}, {'i', 'I'},
	{'o', 'O'}, {'p', 'P'}, {'[', '{'}, {']', '}'}, {enter, enter},
	{ctrl_l_char, ctrl_l_char}, {'a', 'A'}, {'s', 'S'}, {'d', 'D'},
	{'f', 'F'}, {'g', 'G'}, {'h', 'H'}, {'j', 'J'}, {'k', 'K'}, {'l', 'L'},
	{';', ':'}, {'\'', '\"'}, {'`', '~'}, {shift_l_char, shift_l_char},
	{'\\', '|'}, {'z', 'Z'}, {'x', 'X'}, {'c', 'C'}, {'v', 'V'}, {'b', 'B'},
	{'n', 'N'}, {'m', 'M'}, {',', '<'}, {'.', '>'}, {'/', '?'},
	{shift_r_char, shift_r_char}, {'*', '*'}, {alt_l_char, alt_l_char},
	{' ', ' '}, {caps_lock_char, caps_lock_char}
};


static void intr_keyboard_handler(void) {
	uint16_t scancode = (uint16_t) inb(KBD_BUF_PORT);

	// 如果扫描码以 0xe0 开头，表示当前按键会产生多个扫描码
	if (scancode == 0xe0) {
		ext_scancode = 1;
		return;
	}

	if (ext_scancode) {
		scancode = (0xe000 | scancode);
		ext_scancode = 0;
	}

	uint8_t break_code = ((scancode & 0x0080) != 0);
	if (break_code) { // 如果当前的是断码
		uint16_t make_code = (scancode &= 0xff7f);

		if (make_code == ctrl_l_make || make_code == ctrl_r_make) {
			ctrl_status = 0; LOG("Cancel ctrl");
		} else if (make_code == shift_l_make || make_code == shift_r_make) {
			shift_status = 0;
		} else if (make_code == alt_l_make || make_code == alt_r_make) {
			alt_status = 0; LOG("Cancel alt");
		}

		return;
	} else if (
		(scancode > 0x00 && scancode < 0x3b) ||
		(scancode == alt_r_make) ||
		(scancode == ctrl_r_make)
	) {      // 如果当前的是通码
		uint8_t shift = 0;
		if (shift_status && caps_lock_status) {
			shift = 0;
		} else if (shift_status || caps_lock_status) {
			shift = 1;
		} else {
			shift = 0;
		}

		uint8_t index = (scancode &= 0x00ff);
		char cur_char = keymap[index][shift];

		if (cur_char) {
			if (! ioq_full(&kbd_buf)) {
				put_char(cur_char);
				ioq_putchar(&kbd_buf, cur_char);
			}
			return;
		}

		if (scancode == ctrl_l_make || scancode == ctrl_r_make) {
			ctrl_status = 1; LOG("Set ctrl");
		} else if (scancode == shift_l_make || scancode == shift_r_make) {
			shift_status = 1;
		} else if (scancode == alt_l_make || scancode == alt_r_make) {
			alt_status = 1; LOG("Set alt");
		} else if (scancode == caps_lock_make) {
			caps_lock_status = !caps_lock_status;
		}

	} else {
		put_str("Unknown key\n");
	}
}

/* 注册键盘中断处理程序 */
void keyboard_init(void) {
	put_str("keyboard init start\n");
	register_handler(0x21, intr_keyboard_handler);
	ioqueue_init(&kbd_buf);
	put_str("keyboard init done\n");
}