#include "syscall.h"
#include "string.h"
#include "stdint.h"
#include "stdio.h"
#include "file.h"

// 最大支持键入 128 个字符的命令输入
#define cmd_len 128
// 加上命令名外，最多支持 15 个参数
#define MAX_ARG_NR 16

/* 用来存储输入的命令 */
static char cmd_line[cmd_len] = {0};

/* 用来输出命令提示符，由于还没实现 cwd，故先使用 / */
void print_prompt(void) {
	printf("[mylym@localhost %s]$ ", "/");
}

/* 从键盘输入缓冲区中最多读入 count 个字节到 buf 中 */
static void readline(char* buf, int32_t count) {
	char* pos = buf;
	while (read(stdin_no, pos, 1) != -1 && (pos - buf) < count) {
		switch (*pos) {
		/* 找到回车或换行符后就直接返回 */
		case '\r':
		case '\n':
			*pos = 0;
			putchar('\n');
			return;

		case '\b':
			/* 避免删除非本次输入的信息 */
			if (buf[0] != '\b') {
				--pos;
				putchar('\b');
			}
			break;

		default:
			putchar(*pos);
			pos++;
		}
	}
	printf("[ERROR] out of max cmd len\n");
}

/* 简单的 shell */
void my_shell(void) {
	while (1) {
		print_prompt();
		memset(cmd_line, 0, cmd_len);
		readline(cmd_line, cmd_len);
		/* 说明只键入了一个回车 */
		if (cmd_line[0] == 0) {
			continue;
		}
	}
}