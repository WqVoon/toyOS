[bits 32]
section .text

global switch_to
switch_to:
	; 栈中此处是返回地址
	push esi
	push edi
	push ebx
	push ebp

	; 得到栈中的参数 cur
	mov eax, [esp + 20]
	; 此时 eax 指向 cur 的 task_struct，
	; 由于 self_kstack 在 task_struct 中的偏移为 0，
	; 故可以直接写入
	mov [eax], esp

	;---------- 上面是备份线程环境，下面是恢复线程环境 ----------

	; 得到栈中的参数 next
	mov eax, [esp + 24]
	; 恢复该任务的栈地址，从此刻开始资源环境已经属于 next 对应的任务
	mov esp, [eax]

	pop ebp
	pop ebx
	pop edi
	pop esi
	ret