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

;------------- 开启保护模式 -------------
;进入保护模式的三步：
; 1.打开 A20 地址线
; 2.加载 gdt
; 3.将 cr0 的 pe 位（最低位）设为 1
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

;------------- 开启分页 -------------
;开启分页的三步：
; 1.准备好页表
; 2.将页表基地址加载到 cr3 中
; 3.将 cr0 的 pg 位（最高位）设为 1

	call setup_page

	sgdt [gdt_ptr]
	; 将 GDT 的基地址加载到 ebx
	mov ebx, [gdt_ptr + 2]
	; 分别将 GDT 和 显存段 放置到内核空间中
	or dword [ebx + 0x18 + 4], KERNEL_BASE_ADDR
	add dword [gdt_ptr + 2], KERNEL_BASE_ADDR
	; 将栈放置到内核空间中
	add esp, KERNEL_BASE_ADDR

	mov eax, PAGE_DIR_TABLE_POS
	mov cr3, eax

	mov eax, cr0
	or eax, 0x80000000
	mov cr0, eax

	lgdt [gdt_ptr]

	mov byte [gs:162], 'V'

	jmp $

;------------- 设置页表及页目录表的内存位图 -------------
;PAGE_DIR_TABLE_POS 作为页表的起始地址
;从该地址开始，第一个 4KB 空间作为页目录表
;真正的页表从第二个 4KB 开始（但是实际的位置有可能不连续？）
setup_page:

;先清零所有的 PDE，这里会一并把 PDE 的 P 属性置 0，
;于是在防问那些尚未被初始化的 PDE 时，会导致 PageFault
	mov ecx, 4096
	mov esi, 0
.clear_page_dir:
	mov byte [PAGE_DIR_TABLE_POS + esi], 0
	inc esi
	loop .clear_page_dir

.create_pde:
	mov eax, PAGE_DIR_TABLE_POS
	add eax, 0x1000
	mov ebx, eax

	;构建 PDE 表项指向第一个页表，并把这个表项送给页目录的 0 和 0xc00 上
	;原因看下面的解释
	or eax, PG_US_U | PG_RW_W | PG_P
	mov [PAGE_DIR_TABLE_POS + 0x0], eax
	mov [PAGE_DIR_TABLE_POS + 0xc00], eax

	;让最后一个 PDE 指向页目录本身，原因待探究
	sub eax, 0x1000
	mov [PAGE_DIR_TABLE_POS + 4092], eax

;创建第一张页表中前 1MB 大小的 PTE 表项，让其正确映射到物理地址上
;因为加载 loader 时尚未开启分页，所以要保证低 1MB 的虚拟地址=物理地址
	mov ecx, 256 ; 1MB / 4KB = 256个表项
	mov esi, 0
	mov edx, PG_US_U | PG_RW_W | PG_P
.create_pte:
	mov [ebx + esi*4], edx
	add edx, 4096 ; 每次增加 4KB
	inc esi
	loop .create_pte

;创建内核其他页表对应的 PDE，方便进程共享内核
;循环 254 次，加上第一个页表共 255 张页表分给内核
;由于页目录表的最后一项指向页目录本身，不属于内核，所以内核实际的空间为 1GB-4MB
;但这里只是预备工作，因为页表中不存在实际有效的 PTE，也就是尚未分配实际的内存给内核
	mov eax, PAGE_DIR_TABLE_POS
	add eax, 0x2000
	or eax, PG_US_U | PG_RW_W | PG_P
	mov ebx, PAGE_DIR_TABLE_POS
	mov ecx, 254
	mov esi, 769
.create_kernel_pde:
	mov [ebx + esi*4], eax
	inc esi
	add eax, 0x1000
	loop .create_kernel_pde
	ret