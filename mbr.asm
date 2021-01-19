%include "boot.inc"

section mbr vstart=0x7c00
	mov ax, cs
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov sp, 0x7c00

	; 清屏
	mov ax, 0x600
	mov bx, 0x700
	mov cx, 0
	mov dx, 0x184f
	int 10h

	; 设置光标位置到 (0, 0) 处
	mov dx, 0
	mov bh, 0
	mov ah, 2
	int 10h

	; 输出信息
	mov ax, message
	mov bp, ax
	mov cx, message_end - message
	mov ax, 0x1301
	mov bx, 0x2
	int 0x10

	mov ax, LOADER_START_SECTOR
	mov bx, LOADER_BASE_ADDR
	mov cx, 4
	call rd_disk
	jmp LOADER_BASE_ADDR

; 从硬盘读取 n 个扇区放到内存中
; - ax: 起始扇区，LBA格式
; - bx: 加载到到内存首地址
; - cl: 加载的扇区数量
rd_disk:
	mov esi, eax
	mov di, cx

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
	mov [bx], ax
	add bx, 2
	loop .go_on_read
	ret


message: db "Hello MBR!"
message_end:
	times 510 - ($ - $$) db 0
	dw 0xaa55