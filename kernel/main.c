#include "print.h"

extern void showmsg();

int main(void) {
	put_str("\nHello Kernel!\n");
	showmsg();

	while(1);
	return 0;
}