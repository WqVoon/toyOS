#ifndef __SUPER_BLOCK_H
#define __SUPER_BLOCK_H

#include "stdint.h"

/* 超级块结构体 */
struct super_block {
	// 用来标识文件系统类型
	uint32_t magic;
	// 本分区总共的扇区数
	uint32_t sec_cnt;
	// 本分区中 inode 数量
	uint32_t inode_cnt;
	// 本分区的起始 lba 地址
	uint32_t part_lba_base;
	// 块位图本身起始扇区地址
	uint32_t block_bitmap_lba;
	// 块位图占用的扇区数量
	uint32_t block_bitmap_sects;
	// inode 位图起始扇区地址
	uint32_t inode_bitmap_lba;
	// inode 位图占用的扇区数量
	uint32_t inode_bitmap_sects;
	// inode 结点表起始扇区 lba 地址
	uint32_t inode_table_lba;
	// inode 结点表占用的扇区数量
	uint32_t inode_table_sects;
	// 数据区开始的第一个扇区号
	uint32_t data_start_lba;
	// 根目录所在的 inode 号
	uint32_t root_inode_no;
	// 目录项大小
	uint32_t dir_entry_size;
	// 加上 460 字节，以凑够 512 字节的大小
	uint8_t pad[460];
} __attribute__ ((packed));

#endif