#include "print.h"
#include "init.h"

extern void showmsg();

int main(void) {
	put_str("\nHello Interrupt!\n");
	init_all();

	__asm__ volatile ("sti");

	while(1);
	return 0;
}