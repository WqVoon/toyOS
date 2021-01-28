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
# Kernel 头文件所在目录
KERNEL_LIB_HEADER=kernel/lib/kernel
# Kernel 库函数们
KERNEL_LIB_FUNCS_SRC=kernel/lib/kernel/*.asm
KERNEL_LIB_FUNCS_DST=kernel/lib/kernel/*.o
# Kernel 辅助函数们
KERNEL_UTI_FUNCS_SRC=kernel/utils/*.c
KERNEL_UTI_FUNCS_DST=kernel/utils/*.o
# 内核镜像文件
KERNEL_IMG=kernel/kernel.bin
# 写入的镜像文件
IMG_FILE=hd60M.img

make_img:
	@bximage -mode=create -hd=60M -q $(IMG_FILE) && echo "Make IMG File"

compile: compile_mbr compile_loader compile_kernel
	@echo "Done"

compile_mbr: $(MBR_FILE)
	@[ -e $(IMG_FILE) ] || make make_img
	@nasm $(MBR_FILE) \
		&& dd if=$(MBR_TMP_FILE) of=$(IMG_FILE) bs=512 count=1 conv=notrunc,sync \
		&& echo "Compile MBR"

compile_loader: $(LOADER_FILE)
	@nasm $(LOADER_FILE) \
		&& dd if=$(LOADER_TMP_FILE) of=$(IMG_FILE) bs=512 count=4 seek=2 conv=notrunc,sync \
		&& echo "Compile Loader"

compile_asm: $(KERNEL_LIB_FUNCS_SRC)
	@for i in $(KERNEL_LIB_FUNCS_SRC); do nasm -f elf $$i; done

compile_c: $(KERNEL_UTI_FUNCS_SRC)
	@for i in $(KERNEL_UTI_FUNCS_SRC); \
		do $(GCC) -I $(KERNEL_LIB_HEADER) -m32 -c $$i -o $$(echo $$i | cut -d. -f1).o; \
		done

compile_kernel: compile_c compile_asm
	@$(GCC) -m32 -I $(KERNEL_LIB_HEADER) -c -o $(KERNEL_TMP_FILE) $(KERNEL_FILE) \
		&& $(LD) -m elf_i386 $(KERNEL_TMP_FILE) $(KERNEL_LIB_FUNCS_DST) $(KERNEL_UTI_FUNCS_DST) \
			-Ttext 0xc0001500 -e main -o $(KERNEL_IMG) \
		&& dd if=$(KERNEL_IMG) of=$(IMG_FILE) bs=512 count=200 seek=9 conv=notrunc,sync \
		&& echo "Compile kernel"

run: compile
	@bochs -f ./bochs.conf

clean:
	@echo "Clean"
	@rm -f $(IMG_FILE) $(KERNEL_IMG) $(KERNEL_TMP_FILE) \
		$(LOADER_TMP_FILE) $(MBR_TMP_FILE) \
		$(KERNEL_LIB_FUNCS_DST) $(KERNEL_UTI_FUNCS_DST)