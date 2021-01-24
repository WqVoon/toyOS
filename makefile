# qemu 在 'mov cr0, eax' 语句执行后不能正常工作，故移除
all: run
.PHONY: run clean make_img compile compile_loader compile_mbr compile_kernel

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
		&& rm -f $(MBR_TMP_FILE) \
		&& echo "Compile MBR"

compile_loader: $(LOADER_FILE)
	@nasm $(LOADER_FILE) \
		&& dd if=$(LOADER_TMP_FILE) of=$(IMG_FILE) bs=512 count=4 seek=2 conv=notrunc,sync \
		&& rm -f $(LOADER_TMP_FILE) \
		&& echo "Compile Loader"

compile_kernel:
	@$(GCC) -m32 -c -o $(KERNEL_TMP_FILE) $(KERNEL_FILE) \
		&& $(LD) -m elf_i386 $(KERNEL_TMP_FILE) -Ttext 0xc0001500 -e main -o $(KERNEL_IMG) \
		&& rm -f $(KERNEL_TMP_FILE) \
		&& dd if=$(KERNEL_IMG) of=$(IMG_FILE) bs=512 count=200 seek=9 conv=notrunc,sync \
		&& echo "Compile kernel"

run: compile
	@bochs -f ./bochs.conf

clean:
	@echo "Clean"
	@rm -f $(IMG_FILE) $(KERNEL_IMG)