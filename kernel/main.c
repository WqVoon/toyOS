#include "print.h"

extern int get_counter();

int main(void) {
	put_str("\nHello Kernel!\n");
	put_int(get_counter()); put_char(' ');
	put_int(get_counter()); put_char(' ');
	put_int(get_counter()); put_char(' ');
	put_int(get_counter()); put_char(' ');
	return 0;
}