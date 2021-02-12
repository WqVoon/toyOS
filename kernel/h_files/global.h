#ifndef __KERNEL_GLOBAL_H
#define __KERNEL_GLOBAL_H

#include "stdint.h"

typedef uint8_t bool;

// GDT 描述符属性
// G位为0表示段界限粒度为1字节，为1表示粒度为4KB
#define DESC_G_4K  1
// 为1表示操作数是32位，为0表示操作数是16位
#define DESC_D_32  1
// 64位代码标记，此处标记为0
#define DESC_L     0
// 用不到的 AVaiLable 位
#define DESC_AVL   0
// 段是否存在，属于开启分页前的内存解决方案
#define DESC_P     1
#define DESC_DPL_0 0
#define DESC_DPL_1 1
#define DESC_DPL_2 2
#define DESC_DPL_3 3

// 代码段和数据段均属于存储段，tss和各种门描述符属于系统段，通过s位来区分
#define DESC_S_CODE    1
#define DESC_S_DATA    DESC_S_CODE
#define DESC_S_SYS     0
// x=1, c=0, r=0, a=0 代码段是可执行的、非依从的、不可读的、已访问位a清零
#define DESC_TYPE_CODE 8
// x=0, e=0, w=1, a=0 数据段是不可执行的、向上扩展的、可写的、已访问位a清零
#define DESC_TYPE_DATA 2
#define DESC_TYPE_TSS  9


#define RPL0 0
#define RPL1 1
#define RPL2 2
#define RPL3 3

#define TI_GDT 0
#define TI_LDT 1

// 内核当前的几个选择子
#define SELECTOR_K_CODE  ((1<<3) + (TI_GDT<<2) + RPL0)
#define SELECTOR_K_DATA  ((2<<3) + (TI_GDT<<2) + RPL0)
#define SELECTOR_K_STACK SELECTOR_K_DATA
#define SELECTOR_K_GS    ((3<<3) + (TI_GDT<<2) + RPL0)
// 第三个段描述符是显存，第四个是tss
#define SELECTOR_U_CODE  ((5<<3) + (TI_GDT<<2) + RPL3)
#define SELECTOR_U_DATA  ((6<<3) + (TI_GDT<<2) + RPL3)
#define SELECTOR_U_STACK SELECTOR_U_DATA

#define GDT_ATTR_HIGH \
((DESC_G_4K << 7) + (DESC_D_32 << 6) + (DESC_L << 5) + (DESC_AVL << 4))

#define GDT_CODE_ATTR_LOW_DPL3 \
((DESC_P << 7) + \
(DESC_DPL_3 << 5) + \
(DESC_S_CODE << 4) + \
DESC_TYPE_CODE)

#define GDT_DATA_ATTR_LOW_DPL3 \
((DESC_P << 7) + \
(DESC_DPL_3 << 5) + \
(DESC_S_DATA << 4) + \
DESC_TYPE_DATA)

// TSS 描述符属性
#define TSS_DESC_D 0

#define TSS_ATTR_HIGH \
((DESC_G_4K << 7) + \
(TSS_DESC_D << 6) + \
(DESC_L << 5) + \
(DESC_AVL << 4) + 0x0)

#define TSS_ATTR_LOW \
((DESC_P << 7) + \
(DESC_DPL_0 << 5) + \
(DESC_S_SYS << 4) + \
DESC_TYPE_TSS)

#define SELECTOR_TSS ((4 << 3) + (TI_GDT << 2) + RPL0)

// 段描述符结构体
typedef struct {
	// 段界限低16位
	uint16_t limit_low_word;
	// 段基址低16位
	uint16_t base_low_word;
	// 段基址中8位
	uint8_t  base_mid_byte;
	// 内部属性，低4位为TYPE，1位S，2位DPL，1位P
	uint8_t  attr_low_byte;
	// 4位段界限，1位AVL，1位L，1位D/B，1位G
	uint8_t  limit_high_attr_high;
	// 段基址高8位
	uint8_t  base_high_byte;
} gdt_desc;


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