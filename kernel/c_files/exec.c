#include "stdint.h"

extern void intr_exit(void);
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* 32 位 elf 头 */
typedef struct {
	/*
	各位含义如下：
		0_3: 0x7f, "ELF" 是 ELF 文件的魔数
		4: 0 表示不可识别，1 表示是 32 位 elf，2 表示是 64 位 elf
		5: 指定编码格式，0 表示非法，1 表示小端序，2 表示大端序
		6: 0 表示非法版本，1 表示当前版本
		7～15 保留，暂不使用，初始化为 0
	*/
	unsigned char e_ident[16];
	// 为 2 时表示 exec 文件
	Elf32_Half    e_type;
	// 用来表示运行在哪种硬件平台下
	Elf32_Half    e_machine;
	// 版本信息
	Elf32_Word    e_version;
	// 程序的入口虚拟地址
	Elf32_Addr    e_entry;
	// 程序表头在文件中的字节偏移
	Elf32_Off     e_phoff;
	// 节头表在文件内的字节偏移
	Elf32_Off     e_shoff;
	// 不必管
	Elf32_Word    e_flags;
	// elf header 的大小
	Elf32_Half    e_ehsize;
	// 程序表头中每个条目的大小
	Elf32_Half    e_phentsize;
	// 程序表头中条目的数量
	Elf32_Half    e_phnum;
	// 节头表中每个条目的大小
	Elf32_Half    e_shentsize;
	// 节头表中条目的数量
	Elf32_Half    e_shnum;
	// 指明 string name table 在节头表中的索引
	Elf32_Half    e_shstrndx;
} Elf32_Ehdr;

/* 程序头表 program header，也就是段描述头 */
typedef struct {
	// 段类型，详见下面的枚举
	Elf32_Word p_type;
	// 本段在文件内的起始偏移地址
	Elf32_Off  p_offset;
	// 本段在内存中的起始虚拟地址
	Elf32_Addr p_vaddr;
	// 本段在内存中的起始物理地址，暂用不到
	Elf32_Addr p_paddr;
	// 本段在文件中的大小
	Elf32_Word p_filesz;
	// 本段在内存中的大小
	Elf32_Word p_memsz;
	// 本段相关的标志，用来指明 WRX 等权限
	Elf32_Word p_flags;
	// 内存对齐标志
	Elf32_Word p_align;
} Elf32_Phdr;

/* 段类型 */
typedef enum {
	PT_NULL,    // 忽略
	PT_LOAD,    // 可加载的程序段
	PT_DYNAMIC, // 动态加载信息
	PT_INTERP,  // 动态加载器名称
	PT_NOTE,    // 一些辅助信息
	PT_SHLIB,   // 保留
	PT_PHDR     // 程序头表
} segment_type;