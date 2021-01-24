;此时的内存布局:
; 0x000~0x3ff 中断向量表

; 0x400~0x4ff BIOS数据区

; 0x500 开始为可用地址

; 0x900 Loader部分:
;  0x900 GDT表
;  0xb00 机器内存大小(4 byte)
;  0xb04 GDT表中当前表项的数量(值为0x1f，实际个数为 (0x1f+1)/8=4)
;  0xb06 GDT表的基地址(0xc0000900，开启了分页)
;  0xc00 loader_start(Loader 的代码区，也是 MBR 最终跳转的位置)

; 0x1500 内核映像，即内核二进制文件被分析后的入口地址

; 0x7c00 MBR，已经没有用了，可以覆盖

; 0x70000 内核二进制文件被加载的位置

; 0x9f000 新的栈顶

; 0x9fc00~0x9ffff 扩展 BIOS 数据区

; 这部分不可用，属于显存等内容

; 0x100000 页目录表的基地址


%include "boot/boot.inc"

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

;------------- 加载 kernel 的二进制文件到内存中 -------------
	mov eax, KERNEL_START_SECTOR
	mov ebx, KERNEL_BIN_BASE_ADDR
	mov ecx, 200 ; 由于内核大小不会超过 100KB，因此直接读 100KB 出来避免未来重复修改
	call rd_disk

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

	; 为了以防万一，强制刷新一下流水线
	jmp SELECTOR_CODE:enter_kernel

enter_kernel:
	call kernel_init
	mov esp, 0xc009f000
	jmp KERNEL_ENTRY_POINT

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

;------------- 从硬盘中读取内容到内存 -------------
; FIXME: 可能有 BUG
; eax 扇区号; ebx 加载地址; ecx 扇区数量
rd_disk:
	mov esi, eax
	mov edi, ecx
	xor eax, eax
	xor ecx, ecx

; 设置要读取的扇区数量
	mov dx, 0x1f2
	mov al, cl
	out dx, al

	mov eax, esi
; 开启 LBA 模式并将 LBA 地址写入端口
	; LBA 地址的 0-7 位
	mov dx, 0x1f3
	out dx, al
	; LBA 地址的 8-15 位
	mov cl, 8
	shr eax, cl
	mov dx, 0x1f4
	out dx, al
	; LBA 地址的 16-23 位
	shr eax, cl
	mov dx, 0x1f5
	out dx, al
	; LBA 地址的 24-27 位
	shr eax, cl
	and al, 0x0f
	; 设置 0x1f6(device) 的 7-4 位为 1110
	; 开启 LBA 模式，同时从主设备中读取数据
	or al, 0xe0
	mov dx, 0x1f6
	out dx, al

; 向 0x1f7 中写入读命令
	mov dx, 0x1f7
	mov al, 0x20
	out dx, al

; 轮询直到硬盘准备好
.not_ready:
	nop
	in al, dx
	; 第 4 位为 1 表示硬盘控制器已准备好数据传输
	; 第 7 位为 1 表示硬盘正忙
	and al, 0x88
	cmp al, 0x08
	jnz .not_ready

; 从 0x1f0 端口读取数据
	mov ax, di
	; 读取以字为单位，故一个扇区 256 个字
	mov dx, 256
	mul dx
	mov cx, ax

	mov dx, 0x1f0
.go_on_read:
	in ax, dx
	mov [ebx], ax
	add ebx, 2
	loop .go_on_read
	ret

;------------- 解析与安装 kernel -------------
kernel_init:
	xor eax, eax
	xor ebx, ebx ; ebx 用来记录程序头表的地址
	xor ecx, ecx ; cx 用来记录程序头表中的 program header 数量
	xor edx, edx ; dx 记录 program header 的尺寸

	; elf 文件中偏移 42 Byte 的位置是 program header 的大小
	; 记录在 dx 中留着用
	mov dx, [KERNEL_BIN_BASE_ADDR + 42]

	; elf 文件中偏移 28 Byte 的位置是 ph 表在文件中的偏移量
	; 一般来讲该值为 34
	mov ebx, [KERNEL_BIN_BASE_ADDR + 28]
	; 将刚刚的值与 KERNEL_BASE_ADDR 相加即可得到 ph 表的基址
	add ebx, KERNEL_BIN_BASE_ADDR

	; ph 表中表项的数量记录在 cx 中
	mov cx, [KERNEL_BIN_BASE_ADDR + 44]

.each_segment:
	; 如果是 PT_NULL 类型的段就跳过
	cmp byte [ebx + 0], PT_NULL
	je .PTNULL

	; 调用 mem_cpy(dst, src, size) 来复制内存
	; 压入参数3，段大小
	push dword [ebx + 16]

	; 构造并压入参数2，源地址
	; 先获取在文件中的偏移量
	mov eax, [ebx + 4]
	; 与内核二进制文件地址相加得到段当前在内存中的地址
	add eax, KERNEL_BIN_BASE_ADDR
	push eax

	; 压入参数1，目的地址
	push dword [ebx + 8]
	call mem_cpy

	; 恢复栈指针
	add esp, 12

.PTNULL:
	; 将 ebx 增加 sizeof(program header)，使其指向下一个表项
	add ebx, edx
	loop .each_segment

	ret

;------------- 内存拷贝函数 -------------
; 栈中的三个参数 (dst, src, size)
mem_cpy:
	cld
	push ebp
	mov ebp, esp
	push ecx

	mov edi, [ebp + 8]
	mov esi, [ebp + 12]
	mov ecx, [ebp + 16]
	rep movsb

	pop ecx
	pop ebp
	ret