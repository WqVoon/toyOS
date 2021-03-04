> \<\<[操作系统真象还原](https://book.douban.com/subject/26745156/)\>\>  的读书笔记加部分概念的PoC



由于使用了 MacOSX 的 fdisk 命令用于格式化分区，故 repo 中的makefile 只能工作在该系统下，其他环境具体如下：

```shell
# 以下均可通过 homebrew 安装
x86_64-elf-gcc (GCC) 9.3.0
x86_64-elf-ld 2.34
Bochs x86 Emulator 2.6.9
NASM version 2.14.02
```