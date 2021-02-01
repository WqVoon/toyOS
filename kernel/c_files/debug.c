#include "debug.h"

void panic_spin(
	const char* filename, int line,
	const char* func, const char* condition
) {
	intr_disable();

	put_str("\n\n\n!!!!! ERROR !!!!!\n");
	put_str("filename : "); put_str(filename);  put_char('\n');
	put_str("line     : "); put_int(line);      put_char('\n');
	put_str("function : "); put_str(func);      put_char('\n');
	put_str("condition: "); put_str(condition); put_char('\n');

	while(1);
}
