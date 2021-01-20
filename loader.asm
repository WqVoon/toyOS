%include "boot.inc"

section loader vstart=LOADER_BASE_ADDR
LOADER_STACK_TOP equ LOADER_BASE_ADDR

; gdt 内部元素及相关的内容
GDT_BASE:        dd 0x00000000, 0x00000000
CODE_DESC:       dd 0x0000ffff, DESC_CODE_HIGH4
DATA_STACK_DESC: dd 0x0000ffff, DESC_DATA_HIGH4
VIDEO_DESC:      dd 0x80000007, DESC_VIDEO_HIGH4

GDT_SIZE  equ $ - GDT_BASE
GDT_LIMIT equ GDT_SIZE - 1
times 60 dq 0 ; 预留 60 个描述符空位

; 地址为 0xb00
total_mem_bytes dd 0

SELECTOR_CODE  equ (0x0001 << 3) + TI_GDT + RPL0
SELECTOR_DATA  equ (0x0002 << 3) + TI_GDT + RPL0
SELECTOR_VIDEO equ (0x0003 << 3) + TI_GDT + RPL0


;gdt 指针
gdt_ptr:
	dw GDT_LIMIT
	dd GDT_BASE

;人工对齐至 256 个字节，加上 gdt 的 0x200，使得 loader_start 的地址为 0xc00
ards_buf times 244 db 0
ards_nr  dw 0

loader_start:

;------------- 调用 int 15h, ax=0xe820 来获取内存大小 -------------
	xor ebx, ebx
	mov edx, 'PAMS'
	mov di, ards_buf
.e820_mem_get_loop:
	mov ax, 0xe820
	mov ecx, 20
	int 15h
	jc .e820_failed_so_try_e801

	add di, cx
	inc word [ards_nr]
	cmp ebx, 0
	jnz .e820_mem_get_loop

;在所有的 ards 中找到内存最大的
	mov cx, [ards_nr]
	mov ebx, ards_buf
	xor edx, edx
.find_max_mem_area:
	mov eax, [ebx]
	add eax, [ebx+8]
	add ebx, 20
	cmp edx, eax
	jge .next_ards
	mov edx, eax
.next_ards:
	loop .find_max_mem_area
	jmp .mem_get_ok

;------------- 调用 int 15h, ax=0xe801 来获取内存大小 -------------
.e820_failed_so_try_e801:
	mov ax, 0xe801
	int 15h
	jc .e801_failed_so_try88

; 计算低 15 MB 内存，结果暂存在 esi 中
		mov cx, 0x400
		mul cx
		shl edx, 16
		and eax, 0x0000ffff
		or edx, eax
		add edx, 0x100000
		mov esi, edx

; 计算 16 MB 以上的内存
	xor eax, eax
	mov ax, bx
	mov ecx, 0x10000
	mul ecx
	add esi, eax
	mov edx, esi
	jmp .mem_get_ok

;------------- 调用 int 15h, ax=0x88 来获取内存大小 -------------
.e801_failed_so_try88:
	mov ah, 0x88
	int 15h
	jc .error_hlt
	and eax, 0x0000ffff

	mov cx, 0x400
	mul cx
	shl edx, 16
	or edx, eax
	add edx, 0x100000
	jmp .mem_get_ok

;所有的计算方法都失败了，错误处理
.error_hlt:

;将计算获得的结果放入 total_mem_bytes 中
.mem_get_ok:
	mov [total_mem_bytes], edx


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
