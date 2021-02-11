#include "ioqueue.h"
#include "interrupt.h"
#include "global.h"
#include "debug.h"

/* 初始化 io 队列 */
void ioqueue_init(ioqueue* ioq) {
	lock_init(&ioq->lock);
	ioq->producer = ioq->consumer = NULL;
	ioq->head = ioq->tail = 0;
}

/* 返回 pos 在缓冲区中的下一个位置 */
static int32_t next_pos(int32_t pos) {
	return (pos + 1) % bufsize;
}

/* 判断队列是否满 */
bool ioq_full(ioqueue* ioq) {
	ASSERT(intr_get_status() == INTR_OFF);
	return next_pos(ioq->head) == ioq->tail;
}

/* 判断队列是否空 */
bool ioq_empty(ioqueue* ioq) {
	ASSERT(intr_get_status() == INTR_OFF);
	return ioq->head == ioq->tail;
}

/* 使当前生产者或消费者在该缓冲区上等待 */
static void ioq_wait(task_struct** waiter) {
	ASSERT(*waiter == NULL && waiter != NULL);
	*waiter = running_thread();
	thread_block(TASK_BLOCKED);
}

/* 使当前队列上的生产者或消费者被唤醒 */
static void wakeup(task_struct** waiter) {
	ASSERT(*waiter != NULL);
	thread_unblock(*waiter);
	*waiter = NULL;
}

/* 消费者从 ioq 队列中获取一个字符 */
char ioq_getchar(ioqueue* ioq) {
	ASSERT(intr_get_status() == INTR_OFF);

	/*
	只要为空，就阻塞当前消费者，
	后面所有的消费者被记录在 lock 的 waiters 中
	*/
	while (ioq_empty(ioq)) {
		lock_acquire(&ioq->lock);
		ioq_wait(&ioq->consumer);
		lock_release(&ioq->lock);
	}

	char byte = ioq->buf[ioq->tail];
	ioq->tail = next_pos(ioq->tail);

	// 此时说明队列中有空位，可唤醒生产者
	if (ioq->producer != NULL) {
		wakeup(&ioq->producer);
	}

	return byte;
}

/* 生产者向 ioq 队列中写入一个字符 */
void ioq_putchar(ioqueue* ioq, char byte) {
	ASSERT(intr_get_status() == INTR_OFF);

	// 原理同上
	while (ioq_full(ioq)) {
		lock_acquire(&ioq->lock);
		ioq_wait(&ioq->producer);
		lock_release(&ioq->lock);
	}

	ioq->buf[ioq->head] = byte;
	ioq->head = next_pos(ioq->head);

	if (ioq->consumer != NULL) {
		wakeup(&ioq->consumer);
	}
}