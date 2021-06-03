#ifndef __TIMER_H
#define __TIMER_H

#include "stdint.h"

void mtime_sleep(uint32_t m_seconds);

uint32_t get_time_stamp();

#endif