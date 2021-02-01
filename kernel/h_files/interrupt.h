#ifndef __KERNEL_INTERRUPT_H
#define __KERNEL_INTERRUPT_H

typedef void* intr_handler;
void idt_init(void);

/**
 * 定义中断的两种状态
 */
typedef enum {
	INTR_OFF, INTR_ON
} intr_status;

intr_status intr_get_status();
intr_status intr_set_status();
intr_status intr_enable();
intr_status intr_disable();

#endif