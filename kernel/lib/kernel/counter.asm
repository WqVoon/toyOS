;------------- get_counter 函数 -------------
; 每次调用会自增 counter_buffer

section .data
counter_buffer dq 0

section .text
global get_counter
get_counter:
	mov eax, [counter_buffer]
	inc dword [counter_buffer]
	ret