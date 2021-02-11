#ifndef __IOQUEUE_H
#define __IOQUEUE_H

#include "stdint.h"
#include "thread.h"
#include "sync.h"

#define bufsize 64

/* 环形队列 */
typedef struct {
	lock lock;
	/*
	当缓冲区不满时生产者可继续向其中添加数据
	否则陷入睡眠
	*/
	task_struct* producer;
	/*
	当缓冲区不空时消费者可以继续从其中获取数据
	否则陷入睡眠
	*/
	task_struct* consumer;
	char buf[bufsize];
	int32_t head; // 队首，用于写入数据
	int32_t tail; // 队尾，用于读取数据
} ioqueue;

bool ioq_full(ioqueue*);
bool ioq_empty(ioqueue*);
void ioqueue_init(ioqueue*);
char ioq_getchar(ioqueue*);
void ioq_putchar(ioqueue*, char);

#endif