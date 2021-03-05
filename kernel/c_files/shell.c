#include "syscall.h"
#include "string.h"
#include "stdint.h"
#include "stdio.h"
#include "file.h"

// 最大支持键入 128 个字符的命令输入
#define cmd_len 128
// 加上命令名外，最多支持 15 个参数
#define MAX_ARG_NR 16
// 用来存储切分后的命令
char* argv[MAX_ARG_NR];
// 用来记录切分后的命令有多少个单词
int32_t argc = -1;
// 用来将 cmd_map 中的第二项强转成函数指针
typedef void(func)(void);
// IO 操作的 buffer，设置为 512 字节大小
static char* buffer = NULL;
/* 用来存储输入的命令 */
static char cmd_line[cmd_len] = {0};

/* 用来输出命令提示符，由于还没实现 cwd，故先使用 / */
void print_prompt(void) {
	printf("\n[mylym@localhost %s]$ ", "/");
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

/* 用来测试 cmd_map list 是否可用 */
static void builtin_say() {
	printf("You said: ");
	for (int idx = 1; idx < argc; idx++) {
		printf(argv[idx]);
		putchar(' ');
	}
	putchar('\n');
}

/* 读取一个文件 */
static void builtin_cat() {
	if (argc != 2 || argv[1][0] != '/') {
		printf(
			"[ERROR] cat cmd should look like `cat /%s`\n",
			(argc >= 2? argv[1]: "file")
		);
		return;
	}

	int32_t fd;
	if ((fd = open(argv[1], O_RDONLY)) != -1) {
		memset(buffer, 0, 512);
		while (read(fd, buffer, 512) != -1) {
			printf(buffer);
			memset(buffer, 0, 512);
		}
		close(fd);
	} else {
		printf("[ERROR] open file error\n");
		return;
	}
}

/* 创建一个文件 */
static void builtin_touch() {
	if (argc != 2 || argv[1][0] != '/') {
		printf(
			"[ERROR] touch cmd should look like `touch /%s`\n",
			(argc >= 2? argv[1]: "file")
		);
		return;
	}

	const char* filename = argv[1];
	int32_t fd;
	if ((fd = open(filename, O_CREAT)) != -1) {
		printf("you create file %s\n", filename);
		close(fd);
	} else {
		printf("[ERROR] fail to create file %s\n", filename);
	}
}

/* TODO: 编辑一个已经存在的文件，当前仅支持在原有内容上追加内容，未来修改之 */
static void builtin_edit() {
	if (argc != 3 || argv[1][0] != '/') {
		printf(
			"[ERROR] edit cmd should look like `edit /%s %s`\n",
			(argc >= 2? argv[1]: "file"),
			(argc >= 3? argv[2]: "$")
		);
		return;
	}

	const char* filename = argv[1];
	const char* delim = argv[2];
	printf("Edit %s, use %c as delim\n", filename, delim[0]);
	int32_t fd;
	if ((fd = open(filename, O_WRONLY)) != -1) {
		memset(buffer, 0, 512);
		char* buf = buffer;
		while (read(0, buf, 1) != -1 && *buf != delim[0]) {
			putchar(*buf);

			if (buf[0] == '\b') {
				if (buffer[0] != '\b') {
					buf--;
				}
				continue;
			}

			if (buf - buffer == 512) {
				write(fd, buffer, 512);
				memset(buffer, 0, 512);
				buf = buffer;
			} else {
				buf++;
			}
		}
		if (buf != buffer) {
			write(fd, buffer, buf - buffer);
		}
		close(fd);
	} else {
		printf("[ERROR] fail to edit file %s\n", filename);
	}
}

static void builtin_rm() {
	if (argc != 2 || argv[1][0] != '/') {
		printf(
			"[ERROR] rm cmd should look like `rm /%s`\n",
			(argc >= 2? argv[1]: "file")
		);
		return;
	}

	if (unlink(argv[1]) == -1) {
		printf("[ERROR] delete %s failed\n", argv[1]);
	} else {
		printf("You deleted %s\n", argv[1]);
	}
}

extern dir root_dir;
/*TODO: 显示当前目录里的内容，目前仅支持 root_dir*/
static void builtin_ls() {
	dir* path = &root_dir;
	rewinddir(path);
	dir_entry* dir_e = NULL;
	while (dir_e = readdir(path)) {
		printf("%s ", dir_e->filename);
	}
	putchar('\n');
}

static void builtin_help() {
	printf(
		"Support the following cmds:\n"
		" say:   print argvs to test\n"
		" rm:    remove a regular file\n"
		" ls:    list root directory contents\n"
		" cat:   print a file content\n"
		" touch: create a empty file\n"
		" edit:  edit a exists file\n"
		" clear: clear the screen\n"
		" logo:  just for fun\n"
		" help:  show this menu\n\n"
		"* all filename/path should start with '/' *"
	);
}

static void builtin_logo() {
	printf(
		" ____  __  _  _  __   ____\n"
		"(_  _)/  \\( \\/ )/  \\ / ___)\n"
		"  )( (  0 ))  /(  0 )\\___ \\\n"
		" (__) \\__/(__/  \\__/ (____/\n"
		"                            by Yuren."
	);
}

extern void clear(void);


// cmd 字符串和函数的映射表
void* cmd_map[][2] = {
	{"say",   builtin_say},
	{"cat",   builtin_cat},
	{"touch", builtin_touch},
	{"edit",  builtin_edit},
	{"clear", clear},
	{"ls",    builtin_ls},
	{"rm",    builtin_rm},
	{"logo",  builtin_logo},
	{"help",  builtin_help}
};

/* 简单的 shell */
void my_shell(void) {
	uint32_t cmd_map_size = sizeof(cmd_map) / 8;
	if ((buffer = malloc(512)) == NULL) {
		printf("[ERROR] fail to create buffer\n");
		return;
	}
	printf("Welcome! please type 'help' before you use this toyOS\n");

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
			printf("[ERROR] out of max number of argv\n");
			continue;
		}

		int idx = 0;
		for (; idx<cmd_map_size; idx++) {
			if (!strcmp(cmd_map[idx][0], argv[0])) {
				((func*)cmd_map[idx][1])();
				break;
			}
		}
		if (idx == cmd_map_size) {
			printf("[ERROR] unsupported cmd, type 'help' to get help\n");
		}
	}
}