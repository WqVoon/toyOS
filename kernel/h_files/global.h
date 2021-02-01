#ifndef __KERNEL_GLOBAL_H
#define __KERNEL_GLOBAL_H

#include "stdint.h"

#define RPL0 0
#define RPL1 1
#define RPL2 2
#define RPL3 3

#define TI_GDT 0
#define TI_LDT 1

// 内核当前的几个选择子
#define SELECTOR_K_CODE  ((1<<3) + (TI_GDT<<2) + RPL0)
#define SELECTOR_K_DATA  ((2<<3) + (TI_GDT<<2) + RPL0)
#define SELECTOR_K_STACK ((3<<3) + (TI_GDT<<2) + RPL0)
#define SELECTOR_K_GS    ((4<<3) + (TI_GDT<<2) + RPL0)

// IDT 描述符相关的属性
#define IDT_DESC_P    1
#define IDT_DESC_DPL0 0
#define IDT_DESC_DPL3 3
#define IDT_DESC_32_TYPE 0xe // 32位门
#define IDT_DESC_16_TYPE 0x6 // 16位门

// IDT 描述符中的两种特权级的属性
#define IDT_DESC_ATTR_DPL0 \
	((IDT_DESC_P << 7) + (IDT_DESC_DPL0<<5) + IDT_DESC_32_TYPE)

#define IDT_DESC_ATTR_DPL3 \
	((IDT_DESC_P << 7) + (IDT_DESC_DPL3<<5) + IDT_DESC_32_TYPE)

#endif