section mbr vstart=0x7c00
	mov ax, cs
	mov es, ax
	mov ds, ax
	mov ss, ax
	mov fs, ax
	mov sp, 0x7c00

	; 获取光标位置
	mov ah, 3
	mov bh, 0
	int 0x10

	; 打印字符串
	mov ax, message
	mov bp, ax
	mov cx, message_end - message
	mov ax, 0x1301
	mov bx, 0x2
	int 0x10

message: db "Hello MBR!"
message_end:
	times 510 - ($ - $$) db 0
	dw 0xaa55