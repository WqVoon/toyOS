#ifndef __KERNEL_IDE_H
#define __KERNEL_IDE_H

#include "stdint.h"
#include "bitmap.h"
#include "debug.h"
#include "sync.h"
#include "list.h"

typedef struct __disk disk;
typedef struct __ide_channel ide_channel;

/* 分区结构 */
typedef struct {
	// 起始扇区
	uint32_t start_lba;
	//扇区数
	uint32_t sec_cnt;
	// 分区所属的硬盘
	disk* my_disk;
	// 用于队列中的标记
	struct list_elem* part_tag;
	// 分区名称
	char name[8];
	// 本分区的超级块
	// super_block* sb;
	// 块位图
	bitmap block_bitmap;
	// i结点位图
	bitmap inode_bitmap;
	// 本分区打开的i结点队列
	struct list open_inodes;
} partition;

/* 硬盘结构 */
typedef struct __disk {
	// 硬盘的名称
	char name[8];
	// 此块硬盘归属于哪个 ide 通道
	ide_channel* my_channel;
	// 本硬盘是主/从(0/1)
	uint8_t dev_no;
	// 主分区项最多 4 个
	partition prim_parts[4];
	// 当前支持 8 个逻辑分区
	partition logic_parts[8];
} disk;

/* ata 通道结构 */
typedef struct __ide_channel {
	// 通道的名称
	char name[8];
	/*
	本通道的起始端口号，命令块寄存器端口分别加上0～7，控制块加上 0x206
	比如默认通道一的命令块端口范围为 0x1f0~0x1f7，控制块为 0x3f6
	故可设置 port_base 为 0x1f0
	*/
	uint16_t port_base;
	// 本通道使用的中断号
	uint8_t irq_no;
	// 通道锁，用来保证同一时间仅有主盘或从盘在占用通道
	lock lock;
	// 表示等待硬盘的中断
	bool expecting_intr;
	// 用于阻塞、唤醒驱动程序
	semaphore disk_done;
	// 一个通道上连接两个硬盘，一主一从
	disk devices[2];
} ide_channel;

void ide_init();

#endif