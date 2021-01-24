// 第三行的偏移量
#define LINE_3_BASE_OFFSET 320

int main(void) {
	// 尝试向显存输出内容
	const char msg[] = "Hello, Kernel!";
	// 显存首地址
	char* video_base_addr = (char*)0xc00b8000;

	int guard = sizeof(msg);
	for (int idx=0; idx<guard; idx++) {
		video_base_addr[LINE_3_BASE_OFFSET + idx*2] = msg[idx];
	}

	while(1);
	return 0;
}