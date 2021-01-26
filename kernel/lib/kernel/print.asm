TI_GDT equ 0
RPL0   equ 0
SELECTOR_VIDEO equ (0x0003<<3) + TI_GDT + RPL0

[bits 32]
section .text

;------------- put_char 函数 -------------
; 将栈中的一个字符输出到光标所在的位置
global put_char
put_char:
	; 压入所有双字长的寄存器
	; 顺序为 eax, ecx, edx, ebx, esp, ebp, esi, edi
	pushad

	; 保险起见，每次调用都赋值 gs 为视频段选择子
	;TODO: 这里是否需要先保存原值，并在返回用户进程前还原选择子
	mov ax, SELECTOR_VIDEO
	mov gs, ax

;获取当前光标位置
	; 获取高 8 位
	mov dx, 0x03d4
	mov al, 0x0e
	out dx, al
	mov dx, 0x03d5
	in al, dx

	; 暂存至 ah
	mov ah, al

	; 获取低 8 位
	mov dx, 0x03d4
	mov al, 0x0f
	out dx, al
	mov dx, 0x03d5
	in al, dx

	; 暂存光标位置到 bx，这个值为下一个字符的光标坐标值
	mov bx, ax

;获取待打印的字符，因为 pushad 压入 8 个 32 位寄存器
;加上 call 调用该函数时压入的 4 Byte 返回地址，故参数在 esp+36 位置上
	mov ecx, [esp + 36]

;判断待打印字符的值，来进入不同的流程
	; 如果是 '\r'
	cmp cl, 0xd
	jz .is_carriage_return
	; 如果是 '\n'
	cmp cl, 0xa
	jz .is_line_feed
	; 如果是 '\b'
	cmp cl, 0x8
	jz .is_backspace
	; 否则直接打印
	jmp .put_other

;各种字符的处理流程
.is_backspace:
	; 回退一位并乘二，得到相对地址
	dec bx
	shl bx, 1

	mov byte [gs:bx], 0x20 ; 写入空格
	inc bx
	mov byte [gs:bx], 0x07 ; 写入属性

	shr bx, 1 ; 调整光标坐标值，为 set_cursor 作准备
	jmp .set_cursor

.put_other:
	shl bx, 1
	mov [gs:bx], cl
	inc bx
	mov byte [gs:bx], 0x07
	shr bx, 1
	inc bx
	cmp bx, 2000
	jl .set_cursor ; 若超过 2000 则应该卷屏，交给下面的流程处理

.is_line_feed:
.is_carriage_return:
	; 将光标坐标除以80获取余数
	xor dx, dx
	mov ax, bx
	mov si, 80
	div si
	; 减掉余数即获得了本行行首的坐标
	sub bx, dx
	; 移至下一行
	add bx, 80
	cmp bx, 2000
	jl .set_cursor ; 还不需要卷屏

.roll_screen:
	; 搬运 1～24 行的内容到 0~23 行
	cld
	mov ecx, 960 ; (2000-80)*2/4 = 960次
	mov esi, 0xc00b80a0 ; 第 1 行行首
	mov edi, 0xc00b8000 ; 第 0 行行首
	rep movsd

	; 将最后一行用空白填充
	mov ebx, 3840 ; 最后一行的偏移量为 (2000-80)*2 = 3840
	mov ecx, 80
.cls:
	mov word [gs:ebx], 0x0720 ; 写入默认属性值的空格
	add ebx, 2
	loop .cls

	mov bx, 1920 ; 设置光标偏移

.set_cursor:
	; 设置光标的高 8 位
	mov dx, 0x03d4
	mov al, 0x0e
	out dx, al
	mov dx, 0x03d5
	mov al, bh
	out dx, al

	; 设置光标的低 8 位
	mov dx, 0x03d4
	mov al, 0x0f
	out dx, al
	mov dx, 0x03d5
	mov al, bl
	out dx, al

.done:
	popad
	ret


;------------- put_str 函数 -------------
; 将栈中的地址所指向的字符串输出到光标所在位置
global put_str
put_str:
	push ebx
	push ecx
	xor ecx, ecx
	mov ebx, [esp + 12] ; 将地址赋给 ebx

.goon:
	mov cl, [ebx]
	cmp cl, 0 ; 判断是否已经到结尾
	jz .str_over

	push ecx
	call put_char
	add esp, 4

	inc ebx
	jmp .goon

.str_over:
	pop ecx
	pop ebx
	ret