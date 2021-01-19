%include "boot.inc"

section loader vstart=LOADER_BASE_ADDR
LOADER_STACK_TOP equ LOADER_BASE_ADDR
jmp loader_start

; gdt 内部元素及相关的内容
GDT_BASE:        dd 0x00000000, 0x00000000
CODE_DESC:       dd 0x0000ffff, DESC_CODE_HIGH4
DATA_STACK_DESC: dd 0x0000ffff, DESC_DATA_HIGH4
VIDEO_DESC:      dd 0x80000007, DESC_VIDEO_HIGH4

GDT_SIZE equ $ - GDT_BASE
GDT_LIMIT equ GDT_SIZE - 1
times 60 dq 0 ; 预留 60 个描述符空位

SELECTOR_CODE  equ (0x0001 << 3) + TI_GDT + RPL0
SELECTOR_DATA  equ (0x0002 << 3) + TI_GDT + RPL0
SELECTOR_VIDEO equ (0x0003 << 3) + TI_GDT + RPL0

;gdt 指针
gdt_ptr:
	dw GDT_LIMIT
	dd GDT_BASE

loader_start:
;进入保护模式的三步：
; 1.打开 A20 地址线，加载 gdt，将 cr0 的 pe 位（最低位）设为 1
	; 打开 A20
	in al, 0x92
	or al, 0x02
	out 0x92, al

	; 加载 gdt
	lgdt [gdt_ptr]

	; 设置 cr0
	mov eax, cr0
	or eax, 0x00000001
	mov cr0, eax

	; 刷新流水线
	jmp dword SELECTOR_CODE:p_mode_start

[bits 32]
p_mode_start:
	mov ax, SELECTOR_DATA
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov esp, LOADER_STACK_TOP
	mov ax, SELECTOR_VIDEO
	mov gs, ax

	mov byte [gs:160], 'P'

	jmp $
