# 当环境变量 RUNNER='bochs' 时使用 bochs 运行镜像，否则使用 qemu
all: run
.PHONY: run clean make_img

# 待编译的汇编代码
SRC_FILE=mbr.asm
# 编译产生的临时文件
TMP_FILE=$$(echo $(SRC_FILE) | cut -d. -f1)
# 写入的镜像文件
IMG_FILE=hd60M.img

make_img:
	@bximage -mode=create -hd=60M -q $(IMG_FILE) && echo "Make IMG File"

compile: $(SRC_FILE)
	@nasm $(SRC_FILE) \
		&& dd if=$(TMP_FILE) of=$(IMG_FILE) bs=512 count=1 conv=notrunc,sync \
		&& rm -f $(TMP_FILE) \
		&& echo "Compile MBR"

run: compile
	@echo "Run MBR"
	@([ -z $$RUNNER ] || [ $$RUNNER != "bochs" ]) && qemu-system-i386 $(IMG_FILE) || bochs -f ./bochs.conf

clean:
	@echo "Clean"
	@rm -f $(IMG_FILE)