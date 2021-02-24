#include "syscall.h"
#include "string.h"
#include "debug.h"

// printf 字符缓冲区的大小
#define BUF_SIZE 1024
// 可变参数的参数扫描指针
typedef char* va_list;
// 初始化参数指针
#define va_start(ap, v) ap = (va_list)&v
// 将指针指向下一个参数并返回其值
#define va_arg(ap, t)   *((t*)(ap += 4))
// 清空参数指针
#define va_end(ap)      ap = NULL

/* 将整型转换成字符 */
static void itoa(uint32_t value, char** buf_ptr_addr, uint8_t base) {
	uint32_t m = value % base;
	uint32_t i = value / base;

	if (i) itoa(i, buf_ptr_addr, base);
	if (m < 10) {
		*((*buf_ptr_addr)++) = '0' + m;
	} else {
		*((*buf_ptr_addr)++) = 'A' + m - 10;
	}
}

/* 将参数 ap 按照格式 format 输出到字符串 str，并返回 strlen(str) */
uint32_t vsprintf(char* str, const char* format, va_list ap) {
	char* buf_ptr = str;
	const char* index_ptr = format;
	char index_char = *index_ptr;

	int32_t arg_int;
	char* arg_str;
	while (index_char) {
		if (index_char != '%') {
			*(buf_ptr++) = index_char;
			index_char = *(++index_ptr);
			continue;
		}
		index_char = *(++index_ptr);
		switch (index_char) {
		case 'x':
			arg_int = va_arg(ap, int);
			*(buf_ptr++) = '0';
			*(buf_ptr++) = 'x';
			itoa(arg_int, &buf_ptr, 16);
			break;

		case 's':
			arg_str = va_arg(ap, char*);
			strcpy(buf_ptr, arg_str);
			buf_ptr += strlen(arg_str);
			break;

		case 'c':
			*(buf_ptr++) = va_arg(ap, char);
			break;

		case 'd':
			arg_int = va_arg(ap, int);
			if (arg_int < 0) {
				arg_int = 0 - arg_int;
				*buf_ptr++ = '-';
			}
			itoa(arg_int, &buf_ptr, 10);
			break;

		case '%':
			*(buf_ptr++) = index_char;
			break;

		default:
			//TODO: 先引发 GP 异常，以后再做处理
			ASSERT(!"[ERROR] unsupported arg type!!");
		}

		index_char = *(++index_ptr);
	}

	return strlen(str);
}

/* 格式化输出字符串 format */
uint32_t printf(const char* format, ...) {
	va_list args;
	va_start(args, format);
	char buf[BUF_SIZE] = {0};
	vsprintf(buf, format, args);
	va_end(args);
	return write(buf);
}