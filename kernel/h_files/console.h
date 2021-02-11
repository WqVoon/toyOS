#ifndef __CONSOLE_H
#define __CONSOLE_H

#include "stdint.h"

void console_init();
void console_acquire();
void console_release();
void console_set_cursor();
uint32_t console_get_cursor();
void console_put_str(const char*);
void console_put_char(uint8_t);
void console_put_int(uint32_t);

#endif