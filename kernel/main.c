#include "sync.h"
#include "string.h"
#include "print.h"
#include "init.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "keyboard.h"
#include "global.h"
#include "process.h"
#include "syscall.h"
#include "stdio.h"

int main(void) {
	init_all();
	intr_enable();

	printf("Disk cnt: %d\n", *(char*)0x475);

	while(1);
	return 0;
}