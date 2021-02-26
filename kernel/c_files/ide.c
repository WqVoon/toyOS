#include "io.h"
#include "ide.h"
#include "stdio.h"
#include "timer.h"
#include "memory.h"
#include "string.h"

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

// 用于记录总扩展分区的起始 lba，初始为 0，partition_scan 使以此为标记
int32_t ext_lba_base = 0;

// 用来记录硬盘主分区和逻辑分区的下标
uint8_t p_no = 0, l_no = 0;

// 分区队列
struct list partition_list;

// 16 字节大小的结构体，用来存分区表
struct partition_table_entry {
	// 是否可引导，为 0 表示不可引导，为 0x80 表示可引导
	uint8_t bootable;
	// 起始磁头号
	uint8_t start_head;
	// 起始扇区号
	uint8_t start_sec;
	// 起始柱面号
	uint8_t start_chs;
	// 分区类型
	uint8_t fs_type;
	// 结束磁头号
	uint8_t end_head;
	// 结束扇区号
	uint8_t end_sec;
	// 结束柱面号
	uint8_t end_chs;
	// 本分区起始扇区的 lba 地址，这个值要相对于其上级的偏移，具体可见 partition_scan 的实现
	uint32_t start_lba;
	// 本分区的扇区数目
	uint32_t sec_cnt;
} __attribute__((packed));

/* 引导扇区，mbr 或 ebr 所在的扇区 */
struct boot_sector {
	// 引导代码
	uint8_t other[446];
	// 分区表
	struct partition_table_entry partition_table[4];
	// 0x55 0xaa 的签名
	uint16_t signature;
} __attribute__((packed));

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

/* 等待 30 秒或到硬盘准备好，因为据说硬盘处理请求最多花费 31 秒 */
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

/* 将 dst 中 len 个相邻字节交换位置后存入 buf，因为 identify 返回的数据是以字为单位的 */
static void swap_pairs_bytes(const char* dst, char* buf, uint32_t len) {
	int idx = 0;
	for (; idx < len; idx+=2) {
		buf[idx + 1] = *dst++;
		buf[idx] = *dst++;
	}
	buf[idx] = '\0';
}

/* 通过发送 identify 来获得硬盘参数信息 */
static void identify_disk(disk* hd) {
	char hd_info[512];
	select_disk(hd);
	cmd_out(hd->my_channel, CMD_IDENTIFY);
	sema_down(&hd->my_channel->disk_done);

	if (! busy_wait(hd)) {
		printk("%s identify failed", hd->name);
		intr_disable();
		while (1);
	}

	read_from_sector(hd, hd_info, 1);

	char buf[64];
	uint8_t sn_start = 10 * 2, sn_len = 20, md_start = 27 * 2, md_len = 40;
	swap_pairs_bytes(&hd_info[sn_start], buf, sn_len);
	printk("  disk %s info:\n   SN: %s\n", hd->name, buf);
	memset(buf, 0, sizeof(buf));
	swap_pairs_bytes(&hd_info[md_start], buf, md_len);
	printk("   MODULE: %s\n", buf);
	//TODO: 这里可供使用的扇区数应该是长度为 2 的整型，用 uint32_t 是否会有问题
	uint32_t sectors = *(uint32_t*)&hd_info[60 * 2];
	printk("   SECTORS: %d\n", sectors);
	printk("   CAPACITY: %dMB\n", sectors * 512 / 1024 / 1024);
}

/* 扫描硬盘 hd 中地址为 ext_lba 的扇区中所有的分区 */
static void partition_scan(disk* hd, uint32_t ext_lba) {
	// 这里的内存要用堆内存，因为当前栈空间有限，最多差不多只能容纳 6 个扇区
	struct boot_sector* bs = sys_malloc(sizeof(struct boot_sector));
	ide_read(hd, ext_lba, bs, 1);
	uint8_t part_idx = 0;
	struct partition_table_entry* p = bs->partition_table;

	/* 遍历分区表 4 个分区表项 */
	while (part_idx++ < 4) {
		if (p->fs_type == 0x5) {
			// 若为扩展分区
			if (ext_lba_base != 0) {
				// 不是第一次调用，扫描的是 EBR 的分区表，要加上总扩展分区的偏移
				partition_scan(hd, p->start_lba + ext_lba_base);
			} else {
				// 表示当前是第一次调用 partition_scan 函数，即扫描的是 MBR 的分区表
				ext_lba_base = p->start_lba;
				partition_scan(hd, p->start_lba);
			}
		} else if (p->fs_type != 0) {
			// 否则识别那些 fs_type 不为 0 的分区项，因为为 0 表示无效
			if (ext_lba == 0) {
				// 此时说明扫描的是 MBR 分区，由于总扩展分区已经在上面处理过，所以这里一定是主分区
				hd->prim_parts[p_no].start_lba = ext_lba + p->start_lba;
				hd->prim_parts[p_no].sec_cnt = p->sec_cnt;
				hd->prim_parts[p_no].my_disk = hd;
				list_append(&partition_list, &hd->prim_parts[p_no].part_tag);
				sprintf(
					hd->prim_parts[p_no].name,
					"%s%d",
					hd->name, p_no+1
				);
				p_no++;
				ASSERT(p_no < 4);
			} else {
				// 此时说明是逻辑分区
				hd->logic_parts[l_no].start_lba = ext_lba + p->start_lba;
				hd->logic_parts[l_no].sec_cnt = p->sec_cnt;
				hd->logic_parts[l_no].my_disk = hd;
				list_append(&partition_list, &hd->logic_parts[l_no].part_tag);
				sprintf(
					hd->logic_parts[l_no].name,
					"%s%d",
					hd->name, l_no+5
				);
				l_no++;
				if (l_no >= 8) return;
			}
		}
		p++;
	}
	sys_free(bs);
}

static bool partition_info(struct list_elem* pelem, int arg) {
	partition* part = elem2entry(partition, part_tag, pelem);
	printk(
		"   %s start_lba: 0x%x, sec_cnt: 0x%x\n",
		part->name, part->start_lba, part->sec_cnt
	);
	return 0;
}

/* 硬盘数据结构初始化 */
void ide_init() {
	printk("ide_init start\n");
	list_init(&partition_list);

	// 获取硬盘数
	uint8_t hd_cnt = *((uint8_t*)(0x475));
	ASSERT(hd_cnt > 0);
	// 反推有几个 ide 通道
	channel_cnt = DIV_ROUND_UP(hd_cnt, 2);

	ide_channel* channel;
	uint8_t channel_no = 0;
	uint8_t dev_no = 0;

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

		while (dev_no < 2) {
			disk* hd = &channel->devices[dev_no];
			hd->my_channel = channel;
			hd->dev_no = dev_no;
			sprintf(hd->name, "sd%c", 'a'+channel_no*2 + dev_no);
			identify_disk(hd);
			if (dev_no != 0) {
				// 不处理 hd60M.img 这个裸盘
				partition_scan(hd, 0);
			}
			p_no = 0, l_no = 0;
			dev_no++;
		}
		dev_no = 0;
		channel_no++;
	}
	printk("\n  all partition info\n");
	list_traversal(&partition_list, partition_info, (int)NULL);
	printk("ide_init done\n");
}