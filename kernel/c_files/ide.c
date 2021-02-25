#include "io.h"
#include "ide.h"
#include "stdio.h"
#include "timer.h"

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

/* 选择读写的硬盘 */
static void select_disk(disk* hd) {
	uint8_t reg_device = BIT_DEV_MBS | BIT_DEV_LBA;
	// 若为从盘就置 DEV 为 1
	if (hd->dev_no == 1) {
		reg_device |= BIT_DEV_DEV;
	}
	outb(reg_dev(hd->my_channel), reg_device);
}

/* 向硬盘控制器写入起始扇区地址及要读写的扇区数，当 sec_cnt 为 0 时表示 256 个扇区 */
static void select_sector(disk* hd, uint32_t lba, uint8_t sec_cnt) {
	ASSERT(lba <= max_lba);

	ide_channel* channel = hd->my_channel;

	// 写入要读取的扇区数
	outb(reg_sect_cnt(channel), sec_cnt);

	// 写入 lba 地址
	outb(reg_lba_l(channel), lba);
	outb(reg_lba_m(channel), lba >> 8);
	outb(reg_lba_h(channel), lba >> 16);
	outb(reg_dev(channel), BIT_DEV_MBS | BIT_DEV_LBA | \
	(hd->dev_no == 1? BIT_DEV_DEV : 0) | lba >> 24);
}

/* 向通道 channel 发送命令 cmd */
static void cmd_out(ide_channel* channel, uint8_t cmd) {
	// 只要向硬盘发送了命令就置为 1，方便中断处理程序
	channel->expecting_intr = 1;
	outb(reg_cmd(channel), cmd);
}

/* 将以扇区为单位的存储空间转换为以字节为单位 */
static uint32_t sec_cnt2size_in_byte(uint8_t sec_cnt) {
	uint32_t size_in_byte;
	if (sec_cnt == 0) {
		size_in_byte = 256 * 512;
	} else {
		size_in_byte = sec_cnt * 512;
	}
	return size_in_byte;
}

/* 硬盘读入 sec_cnt 个扇区到 buf，假设扇区大小为 512 字节 */
static void read_from_sector(disk* hd, void* buf, uint8_t sec_cnt) {
	uint32_t size_in_byte = sec_cnt2size_in_byte(sec_cnt);
	insw(reg_data(hd->my_channel), buf, size_in_byte / 2);
}

/* 将 buf 中 sec_cnt 扇区的数据写入硬盘 */
static void write2sector(disk* hd, void* buf, uint8_t sec_cnt) {
	uint32_t size_in_byte = sec_cnt2size_in_byte(sec_cnt);
	outsw(reg_data(hd->my_channel), buf, size_in_byte / 2);
}

/* 等待 30 秒，因为据说硬盘处理请求最多花费 31 秒 */
static bool busy_wait(disk* hd) {
	ide_channel* channel = hd->my_channel;
	uint16_t time_limit = 30 * 1000;
	while (time_limit -= 10 >= 0) {
		if (! (inb(reg_status(channel)) & BIT_ALT_STAT_BSY)) {
			return (inb(reg_status(channel)) & BIT_ALT_STAT_DRQ);
		} else {
			mtime_sleep(10);
		}
	}
	return 0;
}

/* 从硬盘读取 sec_cnt 个扇区到 buf */
void ide_read(disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt) {
	ASSERT(lba <= max_lba);
	ASSERT(sec_cnt > 0);
	lock_acquire(&hd->my_channel->lock);

	// 先选择要操作的硬盘
	select_disk(hd);

	// 要操作的总扇区数
	uint32_t secs_op;
	// 已经完成的扇区数
	uint32_t secs_done = 0;
	while (secs_done < sec_cnt) {
		if ((secs_done + 256) <= sec_cnt) {
			secs_op = 256;
		} else {
			secs_op = sec_cnt - secs_done;
		}

		// 写入待读取的扇区数和起始扇区号码
		select_sector(hd, lba + secs_done, secs_op);

		// 写入读命令
		cmd_out(hd->my_channel, CMD_READ_SECTOR);

		// 将当前线程阻塞，直到硬盘完成读操作后唤醒自己
		sema_down(&hd->my_channel->disk_done);

		if (! busy_wait(hd)) {
			// 如果读取失败
			printk("%s read sector %d failed", hd->name, lba);
			intr_disable();
			while (1);
		}

		read_from_sector(hd, (void*)((uint32_t)buf + secs_done * 512), secs_op);
		secs_done += secs_op;
	}
	lock_release(&hd->my_channel->lock);
}

/* 将 buf 中 sec_cnt 扇区数据写入硬盘 */
void ide_write(disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt) {
	ASSERT(lba <= max_lba);
	ASSERT(sec_cnt > 0);
	lock_acquire(&hd->my_channel->lock);

	// 先选择操作的硬盘
	select_disk(hd);

	uint32_t secs_op;
	uint32_t secs_done = 0;
	while (secs_done < sec_cnt) {
		if ((secs_done + 256) <= sec_cnt) {
			secs_op = 256;
		} else {
			secs_op = sec_cnt - secs_done;
		}

		select_sector(hd, lba + secs_done, secs_op);

		cmd_out(hd->my_channel, CMD_WRITE_SECTOR);

		if (! busy_wait(hd)) {
			// 如果硬盘当前不可写
			printk("%s read sector %d failed", hd->name, lba);
			intr_disable();
			while (1);
		}

		write2sector(hd, (void*)((uint32_t)buf + secs_done * 512), secs_op);

		sema_down(&hd->my_channel->disk_done);
		secs_done += secs_op;
	}
}

/* 硬盘中断处理程序 */
void intr_hd_handler(uint8_t irq_no) {
	ASSERT(irq_no == 0x2e || irq_no == 0x2f);
	uint8_t ch_no = irq_no - 0x2e;
	ide_channel* channel = &channels[ch_no];
	ASSERT(channel->irq_no == irq_no);

	if (channel->expecting_intr) {
		channel->expecting_intr = 0;
		sema_up(&channel->disk_done);

		/*
		读取状态寄存器使硬盘控制器认为此次中断已被处理，从而可以继续读写
		硬盘的中断在下列情况下会被清掉
		 1.读取了 status 寄存器
		 2.发出了 reset 命令
		 3.又向 reg_cmd 写入了新的命令
		*/
		inb(reg_status(channel));
	}
}

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

		register_handler(channel->irq_no, intr_hd_handler);
		channel_no++;
	}
	printk("ide_init done\n");
}