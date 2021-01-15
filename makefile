# 当环境变量 RUNNER='bochs' 时使用 bochs 运行镜像，否则使用 qemu
all: run
.PHONY: run clean make_img compile compile_loader compile_mbr

# 待编译的汇编代码
MBR_FILE=mbr.asm
LOADER_FILE=loader.asm
# 编译产生的临时文件
MBR_TMP_FILE=$$(echo $(MBR_FILE) | cut -d. -f1)
LOADER_TMP_FILE=$$(echo $(LOADER_FILE) | cut -d. -f1)
# 写入的镜像文件
IMG_FILE=hd60M.img

make_img:
	@bximage -mode=create -hd=60M -q $(IMG_FILE) && echo "Make IMG File"

compile: compile_mbr compile_loader
	@echo "Done"

compile_mbr: $(MBR_FILE)
	@nasm $(MBR_FILE) \
		&& dd if=$(MBR_TMP_FILE) of=$(IMG_FILE) bs=512 count=1 conv=notrunc,sync \
		&& rm -f $(MBR_TMP_FILE) \
		&& echo "Compile MBR"

compile_loader: $(LOADER_FILE)
	@nasm $(LOADER_FILE) \
		&& dd if=$(LOADER_TMP_FILE) of=$(IMG_FILE) bs=512 count=1 seek=2 conv=notrunc,sync \
		&& rm -f $(LOADER_TMP_FILE) \
		&& echo "Compile Loader"

run: compile
	@echo "Run MBR"
	@([ -z $$RUNNER ] || [ $$RUNNER != "bochs" ]) && qemu-system-i386 $(IMG_FILE) || bochs -f ./bochs.conf

clean:
	@echo "Clean"
	@rm -f $(IMG_FILE)