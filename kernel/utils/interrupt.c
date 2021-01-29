#include "io.h"
#include "print.h"
#include "global.h"
#include "interrupt.h"

#define PIC_M_CTRL 0x20 // 主片控制端口
#define PIC_M_DATA 0x21 // 主片数据端口
#define PIC_S_CTRL 0xa0 // 从片...
#define PIC_S_DATA 0xa1 // ...


/**
 * 初始化 8259A 芯片
 */
static void pic_init(void) {
	// 初始化主片
	outb(PIC_M_CTRL, 0x11); // ICW1：边沿触发，级联8259，需要ICW4
	outb(PIC_M_DATA, 0x20); // ICW2：起始中断向量号 0x20

	outb(PIC_M_DATA, 0x04); // ICW3：IR2 接从片
	outb(PIC_M_DATA, 0x01); // ICW4: 8086 模式，正常 EOI


	// 初始化从片
	outb(PIC_S_CTRL, 0x11); // ICW1：边沿触发，级联8259，需要ICW4
	outb(PIC_S_DATA, 0x28); // ICW2：起始中断向量号 0x28

	outb(PIC_S_DATA, 0x02); // ICW3：设置从片连接到主片的 IR2
	outb(PIC_S_DATA, 0x01); // ICW4：8086 模式，正常 EOI

	// 打开主片的 IR0，当前仅支持时钟中断
	outb(PIC_M_DATA, 0xfe);
	outb(PIC_S_DATA, 0xff);

	put_str("  pic_init done\n");
}


/**
 * 中断描述符结构体
 */
typedef struct {
	uint16_t  func_offset_low_word;
	uint16_t  selector;

	uint8_t   dcount;
	uint8_t   attribute;
	uint16_t  func_offset_high_word;
} gate_desc;

// 中断描述符的数量
#define IDT_DESC_CNT 0x21
// 中断描述符们
static gate_desc idt[IDT_DESC_CNT];
// 中断处理程序们
extern intr_handler intr_entry_table[IDT_DESC_CNT];


/**
 * 初始化一个中断描述符
 */
static void make_idt_desc(
	gate_desc* desc, uint8_t attr, intr_handler function
) {
	desc->selector  = SELECTOR_K_CODE;
	desc->dcount    = 0;
	desc->attribute = attr;
	desc->func_offset_low_word  = (uint32_t) function & 0x0000ffff;
	desc->func_offset_high_word = ((uint32_t) function & 0xffff0000) >> 16;
}


/**
 * 初始化中断描述符表
 */
static void idt_desc_init(void) {
	for (int i=0; i<IDT_DESC_CNT; i++) {
		make_idt_desc(
			&idt[i],
			IDT_DESC_ATTR_DPL0,
			intr_entry_table[i]
		);
	}
	put_str("  idt_desc_init done\n");
}

/**
 * 完成中断相关的初始化工作
 */
void idt_init() {
	put_str("idt_init start\n");

	idt_desc_init();
	pic_init();

	// #pragma pack(1)
	// struct {
	// 	uint16_t limit;
	// 	uint32_t base_addr;
	// } idt_operand;

	// idt_operand.limit     = sizeof(idt)-1;
	// idt_operand.base_addr = (uint32_t)idt;

	uint64_t idt_operand = (
		sizeof(idt)-1 | (uint64_t)idt << 16
	);

	__asm__ volatile ("lidt %0": : "m"(idt_operand));

	put_str("idt_init done\n");
}