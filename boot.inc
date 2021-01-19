;------------- Loader 属性 -------------

; loader 程序被加载到内存的位置
LOADER_BASE_ADDR    equ 0x900
; loader 程序在硬盘的位置（LBA格式）
LOADER_START_SECTOR equ 0x2

;------------- GDT 属性 -------------

; 将 G 设为 1 表示段界限粒度为 4KB
DESC_G_4K equ 1_000_0000_0000_0000_0000_0000b
; 将 D 设为 1 表示 D/B 位使用 32 位操作数
DESC_D_32 equ 1_00_0000_0000_0000_0000_0000b
; 将 L 设为 0 表示不使用保留字段
DESC_L    equ 0_0_0000_0000_0000_0000_0000b
; AVL 也设为全 0， 因为 cpu 用不到它
DESC_AVL  equ 0_0000_0000_0000_0000_0000b

; 段界限的 16-19 位，由于是平坦模式，因此全为1
DESC_LIMIT_CODE2  equ 1111_0000_0000_0000_0000b
DESC_LIMIT_DATA2  equ DESC_LIMIT_CODE2
DESC_LIMIT_VIDEO2 equ 0000_0000_0000_0000_0000b
; Present 字段，先设为 1，后续变为 0 时再处理
DESC_P           equ 1_000_0000_0000_0000b

; 下面是 4 中 DPL 等级
DESC_DPL_0 equ 00_0_0000_0000_0000b
DESC_DPL_1 equ 01_0_0000_0000_0000b
DESC_DPL_2 equ 10_0_0000_0000_0000b
DESC_DPL_3 equ 11_0_0000_0000_0000b

; 两种数据段（区别于系统段），故 S 字段设为 1
DESC_S_CODE equ 1_0000_0000_0000b
DESC_S_DATA equ DESC_S_CODE
; 系统段
DESC_S_SYS  equ 0_0000_0000_0000b

;几种常用的 type
;TODO:中间两位的顺序待确认
; x=1,c=0,r=0,a=0 代码可执行，非一致，不可读，已访问位 a 清零
DESC_TYPE_CODE equ 1000_0000_0000b
; x=0,e=0,w=1,a=0 数据不可执行，向上扩展，可写，a 清零
DESC_TYPE_DATA equ 0010_0000_0000b

; 定义几种常用段的高 4 Byte
; 代码段
DESC_CODE_HIGH4 equ (0x00<<24) + DESC_G_4K + DESC_D_32 + \
DESC_L + DESC_AVL + DESC_LIMIT_CODE2 + \
DESC_P + DESC_DPL_0 + DESC_S_CODE + \
DESC_TYPE_CODE + 0x00
; 数据段
DESC_DATA_HIGH4 equ (0x00<<24) + DESC_G_4K + DESC_D_32 + \
DESC_L + DESC_AVL + DESC_LIMIT_DATA2 + \
DESC_P + DESC_DPL_0 + DESC_S_DATA + \
DESC_TYPE_DATA + 0x00
; 显存段
DESC_VIDEO_HIGH4 equ (0x00<<24) + DESC_G_4K + DESC_D_32 + \
DESC_L + DESC_AVL + DESC_LIMIT_VIDEO2 + DESC_P + \
DESC_DPL_0 + DESC_S_DATA + DESC_TYPE_DATA + 0x0b

;------------- Selector 属性 -------------
RPL0 equ 00b
RPL1 equ 01b
RPL2 equ 10b
RPL3 equ 11b
TI_GDT equ 000b
TI_LDT equ 100b