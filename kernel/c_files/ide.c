#include "ide.h"
#include "stdio.h"

/* 定义硬盘各寄存器的端口号 */
// 命令块寄存器们

// 16 位寄存器，其他的都是 8 位，读写数据时都操作这一端口
#define reg_data(channel)       (channel->port_base + 0)
// 读硬盘时，这里存储读取失败的信息，未读取的扇区数在 reg_sect_cnt 中
// 写硬盘时，这里用于指定额外的参数
#define reg_error(channel)      (channel->port_base + 1)
// 表示要读取或写入的扇区数，由于是 8 位寄存器故最大 255 个扇区
// 特殊点在于，如果该值为 0 表示要操作 256 个扇区
#define reg_sect_cnt(channel)   (channel->port_base + 2)
// 下面的三个寄存器用于指定 LBA 的低 24 位，其余的在 reg_dev 中
#define reg_lba_l(channel)      (channel->port_base + 3)
#define reg_lba_m(channel)      (channel->port_base + 4)
#define reg_lba_h(channel)      (channel->port_base + 5)
/*
杂项，0～3 位用于存储 LBA 的剩余 4 位
第 4 位表示主盘或从盘，为 0 表示主盘，为 1 表示从盘
第 6 位表示是否启用 LBA，1 表示启用，0 表示启用 CHS 模式
第 5 位和第 7 位固定为 1，称为 MBS 位
*/
#define reg_dev(channel)        (channel->port_base + 6)
/*
读硬盘时，此寄存器表示状态
第 0 位用于表示命令是否出错
第 3 位表示硬盘已经准备好数据，可以读取
第 6 位表示设备正常
第 7 位表示硬盘正忙
*/
#define reg_status(channel)     (channel->port_base + 7)
// 写硬盘时，此寄存器表示命令，命令定义在下面
#define reg_cmd(channel)        (reg_status(channel))
// 控制块寄存器
#define reg_alt_status(channel) (channel->port_base + 0x206)
#define reg_ctl(channel)        (reg_alt_status(channel))

/* reg_alt_status 寄存器的一些关键位 */
// 硬盘忙
#define BIT_ALT_STAT_BSY  0x80
// 驱动器准备好
#define BIT_ALT_STAT_DRDY 0x40
// 数据传输准备好
#define BIT_ALT_STAT_DRQ  0x8

/* device 寄存器的一些关键位 */
#define BIT_DEV_MBS 0xa0
#define BIT_DEV_LBA 0x40
#define BIT_DEV_DEV 0x10

/* 一些硬盘操作的指令 */
#define CMD_IDENTIFY     0xec
#define CMD_READ_SECTOR  0x20
#define CMD_WRITE_SECTOR 0x30

/* 调试用，定义可读写的最大扇区数，当前支持 80MB 的硬盘 */
#define max_lba ((80*1024*1024/512)-1)

// 按硬盘数计算的通道数
uint8_t channel_cnt;

// 有两个 ide 通道
ide_channel channels[2];

/* 硬盘数据结构初始化 */
void ide_init() {
	printk("ide_init start\n");
	// 获取硬盘数
	uint8_t hd_cnt = *((uint8_t*)(0x475));
	ASSERT(hd_cnt > 0);
	// 反推有几个 ide 通道
	channel_cnt = DIV_ROUND_UP(hd_cnt, 2);

	ide_channel* channel;
	uint8_t channel_no = 0;

	// 处理每个通道上的硬盘
	while (channel_no < channel_cnt) {
		channel = &channels[channel_no];
		sprintf(channel->name, "ide%d", channel_no);

		// 为每个 ide 通道初始化端口基址及中断向量
		switch (channel_no) {
		case 0:
			channel->port_base = 0x1f0;
			channel->irq_no = 0x20 + 14;
			break;

		case 1:
			channel->port_base = 0x170;
			channel->irq_no = 0x20 + 15;
			break;
		}

		channel->expecting_intr = 0;
		lock_init(&channel->lock);

		/*
		初始化为 0，这样在硬盘控制器请求数据后对应的线程会阻塞
		直到硬盘完成后通过中断程序来进行 sema_up，从而唤醒线程
		*/
		sema_init(&channel->disk_done, 0);

		channel_no++;
	}
	printk("ide_init done\n");
}