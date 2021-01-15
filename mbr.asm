section mbr vstart=0x7c00
	mov ax, cs
	mov ds, ax
	mov ss, ax
	mov sp, 0x7c00
	; 显卡工作在文本模式下，故段寄存器赋值 0xb800
	mov ax, 0xb800
	mov gs, ax

	; 清屏
	mov ax, 0x600
	mov bx, 0x700
	mov cx, 0
	mov dx, 0x184f
	int 10h

	; 每个字符占 2 个字节
	; 这 2 个字节的作用如下：
	;  - 第一个字节为 ASCII 码
	;  - 第二个字节低 4 位是前景色颜色，0-4 bit 分别为 RGB 和是否高亮
	;  - 第二个字节高 4 位是背景色颜色，5-7 bit 分别为 RGB 和是否闪烁
	; 默认显示模式为 80*25，故一屏所需 4000 Byte
	; 故 32 KB 可显示 32KB/4000B=8 屏
	mov bx, message
	xor cx, cx
	xor di, di
	loop_start:
		mov al, cl
		xlat
		mov byte[gs:di], al
		; 采用闪烁的方式输出，qemu 中无效，bochs 中正常
		mov byte[gs:di+1], 0x8f

		inc cx
		add di, 2
		cmp cx, message_end-message
		je loop_end
		jmp loop_start
	loop_end:


section_end:
	jmp $

	message db "Hello, MBR!"
	message_end:
	times 510 - ($-$$) db 0
	dw 0xaa55