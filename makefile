# qemu 在 'mov cr0, eax' 语句执行后不能正常工作，故移除
all: run
.PHONY: run clean make_img compile compile_loader compile_mbr compile_kernel compile_asm compile_c

# 待编译的汇编代码
MBR_FILE=boot/mbr.asm
LOADER_FILE=boot/loader.asm
KERNEL_FILE=kernel/main.c
# 编译产生的临时文件
MBR_TMP_FILE=$$(echo $(MBR_FILE) | cut -d. -f1)
LOADER_TMP_FILE=$$(echo $(LOADER_FILE) | cut -d. -f1)
KERNEL_TMP_FILE=$$(echo $(KERNEL_FILE) | cut -d. -f1)
# 交叉编译工具
GCC=x86_64-elf-gcc
LD=x86_64-elf-ld
# 一般头文件所在目录
LIB_HEADER=kernel/lib
# Kernel 头文件所在目录
KERNEL_LIB_HEADERS=kernel/h_files
# Kernel 库函数们
KERNEL_LIB_ASM_FUNCS_SRC=kernel/asm_files/*.asm
KERNEL_LIB_ASM_FUNCS_DST=kernel/asm_files/*.o
# Kernel 辅助函数们
KERNEL_LIB_C_FUNCS_SRC=kernel/c_files/*.c
KERNEL_LIB_C_FUNCS_DST=kernel/c_files/*.o
# 内核镜像文件
KERNEL_IMG=kernel/kernel.bin
# 写入的镜像文件
MASTER_IMG_FILE=hd60M.img
SLAVE_IMG_FILE=hd80M.img

make_img:
	@bximage -mode=create -hd=60M -q $(MASTER_IMG_FILE) && echo "Make Master IMG File"
	@bximage -mode=create -hd=80M -q $(SLAVE_IMG_FILE) && echo "Make Slave IMG File"

compile: compile_mbr compile_loader compile_kernel
	@echo "Done"

compile_mbr: $(MBR_FILE)
	@[ -e $(MASTER_IMG_FILE) ] || make make_img
	@nasm $(MBR_FILE) \
		&& dd if=$(MBR_TMP_FILE) of=$(MASTER_IMG_FILE) bs=512 count=1 conv=notrunc,sync \
		&& echo "Compile MBR"

compile_loader: $(LOADER_FILE)
	@nasm $(LOADER_FILE) \
		&& dd if=$(LOADER_TMP_FILE) of=$(MASTER_IMG_FILE) bs=512 count=4 seek=2 conv=notrunc,sync \
		&& echo "Compile Loader"

compile_asm: $(KERNEL_LIB_ASM_FUNCS_SRC)
	@for i in $(KERNEL_LIB_ASM_FUNCS_SRC); do nasm -f elf $$i; done

compile_c: $(KERNEL_LIB_C_FUNCS_SRC)
	@for i in $(KERNEL_LIB_C_FUNCS_SRC); \
		do $(GCC) -std=c99 -fno-builtin -I $(KERNEL_LIB_HEADERS) -m32 -c $$i -o $$(echo $$i | cut -d. -f1).o; \
		done

compile_kernel: compile_c compile_asm
	@$(GCC) -std=c99 -fno-builtin -m32 -I $(KERNEL_LIB_HEADERS) -c -o $(KERNEL_TMP_FILE) $(KERNEL_FILE) \
		&& $(LD) -m elf_i386 $(KERNEL_TMP_FILE) $(KERNEL_LIB_ASM_FUNCS_DST) $(KERNEL_LIB_C_FUNCS_DST) \
			-Ttext 0xc0001500 -e main -o $(KERNEL_IMG) \
		&& dd if=$(KERNEL_IMG) of=$(MASTER_IMG_FILE) bs=512 count=200 seek=9 conv=notrunc,sync \
		&& echo "Compile kernel"

run: clean compile
	@bochs -f ./bochs.conf

clean:
	@echo "Clean"
	@rm -f $(MASTER_IMG_FILE) $(SLAVE_IMG_FILE) $(KERNEL_IMG) $(KERNEL_TMP_FILE) \
		$(LOADER_TMP_FILE) $(MBR_TMP_FILE) \
		$(KERNEL_LIB_ASM_FUNCS_DST) $(KERNEL_LIB_C_FUNCS_DST)