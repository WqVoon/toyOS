#include "string.h"
#include "print.h"
#include "init.h"
#include "debug.h"
#include "memory.h"

extern void showmsg();

int main(void) {
	put_str("\nHello Interrupt!\n");
	init_all();

	void* addr = get_kernel_pages(4);
	put_str("\n get_kernel_page start vaddr is ");
	put_int((uint32_t) addr);
	put_str("\n");

	while(1);
	return 0;
}