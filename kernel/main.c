#include "print.h"

int main(void) {
	// 测试 \r \h \n 三个控制字符以及基本光标移动
	const char str[] = "\rh\bHello,\nKernel!";
	const int lenth = sizeof(str)-1;

	for (int idx=0; idx < lenth; idx++) {
		put_char(str[idx]);
	}
	return 0;
}