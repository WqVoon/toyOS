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

/* 分析字符串 cmd_str 中以 token 为分隔符的单词，并将其指针存入 argv 数组 */
static int32_t cmd_parse(char* cmd_str, char** argv, char token) {
	int32_t arg_idx = 0;
	while (arg_idx < MAX_ARG_NR) {
		argv[arg_idx] = NULL;
		arg_idx++;
	}

	char* next = cmd_str;
	int32_t argc = 0;
	while (*next) {
		// 跳过所有的 token
		while (*next == token) {
			next++;
		}

		// 处理命令最后带 token 的情况
		if (*next == 0) {
			break;
		}
		argv[argc] = next;

		// 开始处理一个单词，扫描直到遇到 \0 或者 token
		while (*next && *next != token) {
			next++;
		}
		// 分割字符串
		*next = 0;

		if (argc > MAX_ARG_NR) {
			return -1;
		}

		next++;
		argc++;
	}
	return argc;
}

char* argv[MAX_ARG_NR];
int32_t argc = -1;

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
		argc = cmd_parse(cmd_line, argv, ' ');
		if (argc == -1) {
			printf("[ERROR] out of max number of argv");
			continue;
		}

		int32_t arg_idx = 0;
		while (arg_idx < argc) {
			printf("%s ", argv[arg_idx]);
			arg_idx++;
		}
		putchar('\n');
	}
}