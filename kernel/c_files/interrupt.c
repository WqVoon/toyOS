#include "io.h"
#include "print.h"
#include "global.h"
#include "interrupt.h"

#define EFLAGS_IF_MASK 0x00000200
#define GET_EFLAGS(EFLAG_VAR)\
	__asm__ __volatile__ ("pushfl; popl %0": "=g"(EFLAG_VAR))

/**
 * 返回当前 eflags 内中断的状态
 */
intr_status intr_get_status() {
	uint32_t eflags = 0;
	GET_EFLAGS(eflags);
	return (EFLAGS_IF_MASK & eflags)? INTR_ON: INTR_OFF;
}

/**
 * 打开中断并返回之前中断的状态
 */
intr_status intr_enable() {
	intr_status old_status;
	if (INTR_ON == intr_get_status()) {
		old_status = INTR_ON;
	} else {
		old_status = INTR_OFF;
		__asm__ __volatile__ ("sti");
	}
	return old_status;
}

/**
 * 关闭中断并返回之前中断的状态
 */
intr_status intr_disable() {
	intr_status old_status;
	if (INTR_ON == intr_get_status()) {
		old_status = INTR_ON;
		__asm__ __volatile__ ("cli"::: "memory"); //TODO: memory 的用途?
	} else {
		old_status = INTR_OFF;
	}
	return old_status;
}

/**
 * 将中断状态设置为 status
 */
intr_status intr_set_status(intr_status status) {
	return (status & INTR_ON)? intr_enable(): intr_disable();
}


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
// 中断处理程序入口们，定义在 Kernel.asm 中，实际调用 idt_table 中的处理程序
extern intr_handler intr_entry_table[IDT_DESC_CNT];
// 用于保存中断名
char* intr_name[IDT_DESC_CNT];
// 实际的中断处理程序
intr_handler idt_table[IDT_DESC_CNT];

extern void set_cursor(uint32_t pos);
/**
 * 通用的中断处理函数，一般用于异常处理
 */
static void general_intr_handler(uint8_t vec_nr) {
	// TODO:IRQ7 和 IRQ15 会产生伪中断，不用处理（？）
	if (vec_nr == 0x27 || vec_nr == 0x2f) {
		return;
	}
	set_cursor(0);
	int cursor_pos = 0;
	while (cursor_pos < 320) {
		put_char(' ');
		cursor_pos++;
	}

	set_cursor(0);
	put_str("!!!!! exception message begin !!!!!\n");
	set_cursor(88);
	put_str(intr_name[vec_nr]);

	// 如果为 Pagefault，那么打印出缺失的地址并悬停
	if (vec_nr == 14) {
		int page_fault_vaddr = 0;
		// 地址存放在 cr2 中
		__asm__ __volatile__ (
			"movl %%cr2, %0"
			: "=r"(page_fault_vaddr)
		);
		put_str("\npage fault addr is ");
		put_int(page_fault_vaddr);
	}
	put_str("\n!!!!! exception message end   !!!!!\n");

	while (1);
}

/**
 * 注册一般中断处理函数及异常名
 */
static void exception_init(void) {
	for (int i=0; i<IDT_DESC_CNT; i++) {
		idt_table[i] = general_intr_handler;
		intr_name[i] = "unknown";
	}

	intr_name[0x00] = "#DE Device Error";
	intr_name[0x01] = "#DB Debug Exception";
	intr_name[0x02] = "NMI Interrupt";
	intr_name[0x03] = "#BP Breakpoint Exception";
	intr_name[0x04] = "#OF Overflow Exception";
	intr_name[0x05] = "#BR BOUND Range Exceeded Exception";
	intr_name[0x06] = "#UD Invalid Opcode Exception";
	intr_name[0x07] = "#NM Device Not Avaliable Exception";
	intr_name[0x08] = "#DF Double Fault Exception";
	intr_name[0x09] = "Coprocessor Segment Overrun";
	intr_name[0x0a] = "#TS Invalid TSS Exception";
	intr_name[0x0b] = "#NP Segment Not Present";
	intr_name[0x0c] = "#SS Stack Fault Excecption";
	intr_name[0x0d] = "#GP General Protection Exception";
	intr_name[0x0e] = "#PF Page-Fault Exception";
	// 第15项是保留项
	intr_name[0x10] = "#MF x87 FPU Floating-Point Error";
	intr_name[0x11] = "#AC Alignment Check Exception";
	intr_name[0x12] = "#MC Machine-Check Exception";
	intr_name[0x13] = "#XF SIMD Floating-Point Exception";
}


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


/* 为 vector_no 指明的中断注册中断处理函数 function */
void register_handler(uint8_t vector_no, intr_handler function) {
	idt_table[vector_no] = function;
}


/**
 * 初始化 idt 表中所有的中断描述符表
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
	exception_init();
	pic_init();

	uint64_t idt_operand = (
		sizeof(idt)-1 | (uint64_t)idt << 16
	);

	__asm__ volatile ("lidt %0": : "m"(idt_operand));

	put_str("idt_init done\n");
}