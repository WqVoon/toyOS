#include "print.h"
#include "init.h"
#include "debug.h"

extern void showmsg();

int main(void) {
	put_str("\nHello Interrupt!\n");
	init_all();

	while(1);
	return 0;
}