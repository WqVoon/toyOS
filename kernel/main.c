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

void process_task(void);

int main(void) {
	init_all();
	intr_enable();

	while(1);
	return 0;
}

void process_task(void) {
	while (1);
}