#include "thread.h"
#include "debug.h"
#include "sync.h"
#include "interrupt.h"

/* 初始化信号量 */
void sema_init(semaphore* psema, uint8_t value) {
	psema->value = value;
	list_init(&psema->waiters);
}

/* 初始化锁 plock */
void lock_init(lock* plock) {
	plock->holder = NULL;
	plock->holder_repeat_nr = 0;
	sema_init(&plock->semaphore, 1);
}

/* 信号量 down(P) 操作 */
void sema_down(semaphore* psema) {
	intr_status old_status = intr_disable();

	/*
	 这里用 while 是用来强制当前线程被唤醒后会
	 再判断一次 psema->value 是否为 0
	 TODO: 但是这个 ASSERT 就很奇怪，虽然因为 thread_unblock 的缘故无影响
	*/
	while (psema->value == 0) {
		task_struct* cur = running_thread();
		ASSERT(! elem_find(
			&psema->waiters,
			&cur->general_tag
		));
		list_append(&psema->waiters, &cur->general_tag);
		thread_block(TASK_BLOCKED);
	}

	psema->value--;
	ASSERT(psema->value == 0);

	intr_set_status(old_status);
}

/* 信号量的 up(V) 操作 */
void sema_up(semaphore* psema) {
	intr_status old_status = intr_disable();
	ASSERT(psema->value == 0);

	if (! list_empty(&psema->waiters)) {
		task_struct* thread_blocked = \
		elem2entry(task_struct, general_tag, list_pop(&psema->waiters));
		thread_unblock(thread_blocked);
	}

	psema->value++;
	ASSERT(psema->value == 1);
	intr_set_status(old_status);
}

/* 获取锁 plock */
void lock_acquire(lock* plock) {
	if (plock->holder != running_thread()) {
		// 要先 P 操作后再修改 plock 元信息
		sema_down(&plock->semaphore);
		plock->holder = running_thread();
		ASSERT(plock->holder_repeat_nr == 0);
		plock->holder_repeat_nr = 1;
	} else {
		plock->holder_repeat_nr++;
	}
}

/* 释放锁 plock */
void lock_release(lock* plock) {
	ASSERT(plock->holder == running_thread());
	if (plock->holder_repeat_nr > 1) {
		plock->holder_repeat_nr--;
		return;
	}
	ASSERT(plock->holder_repeat_nr == 1);
	// 要先修改 plock 元信息后再 V 操作
	plock->holder = NULL;
	plock->holder_repeat_nr = 0;
	sema_up(&plock->semaphore);
}