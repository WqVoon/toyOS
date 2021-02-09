#ifndef __THREAD_SYNC_H
#define __THREAD_SYNC_H

#include "list.h"
#include "stdint.h"
#include "thread.h"

/* 信号量结构 */
typedef struct {
	uint8_t value;
	struct list waiters;
} semaphore;

/* 锁结构 */
typedef struct {
	// 锁当前的持有者
	task_struct* holder;
	// 用二元信号量实现锁
	semaphore semaphore;
	// 锁的持有者重复申请锁的次数
	uint32_t holder_repeat_nr;
} lock;


void sema_init(semaphore*, uint8_t);
void lock_init(lock*);
void sema_down(semaphore*);
void sema_up(semaphore*);
void lock_acquire(lock*);
void lock_release(lock*);

#endif