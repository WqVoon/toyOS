%include "boot.inc"

section loader vstart=LOADER_BASE_ADDR
	; 获取光标位置
	mov bh, 0
	mov ah, 0x3
	int 10h

	; 输出信息
	mov ax, message
	mov bp, ax
	mov cx, message_end - message
	mov ax, 0x1301
	mov bx, 0x2
	int 0x10
	jmp $

message db 13, 10, "Hello Loader!"
message_end: