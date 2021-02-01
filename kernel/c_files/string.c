#include "string.h"
#include "global.h"
#include "debug.h"

/**
 * 将 dst_ 起始的 size 个字节设置为 value
 */
void memset(void* dst_, uint8_t value, uint32_t size) {
	ASSERT(dst_ != NULL);
	uint8_t* dst = (uint8_t*)dst_;
	while (size-->0) *dst++ = value;
}

/**
 * 将 src_ 起始的 size 个字节复制到 dst_
 */
void memcpy(void* dst_, const void* src_, uint32_t size) {
	ASSERT(dst_ != NULL && src_ != NULL);
	uint8_t* dst = (uint8_t*)dst_;
	const uint8_t* src = (const uint8_t*) src_;
	while (size-->0) *dst++ = *src++;
}

/**
 * 连续比较以 a_ 和 b_ 开头的 size 个字节，若相等返回 0
 */
int memcmp(const void* a_, const void* b_, uint32_t size) {
	const uint8_t* a = a_;
	const uint8_t* b = b_;
	//TODO: 为什么是 || 而不是 &&
	ASSERT(a != NULL || b != NULL);
	
	while (size-->0) {
		if (*a != *b) return *a > *b? 1: -1;
		a++; b++;
	}

	return 0;
}

/**
 * 将字符串从 src_ 复制到 dst_，同时返回 dst_ 的地址
 */
char* strcpy(char* dst_, const char* src_) {
	ASSERT(dst_ != NULL && src_ != NULL);
	char* ret = dst_;
	while ((*dst_++ = *src_++));
	return ret;
}

/**
 * 返回字符串的长度
 */
uint32_t strlen(const char* str) {
	ASSERT(str != NULL);
	const char* ptr = str;
	while (*ptr++);
	return (ptr - str - 1);
}

/**
 * 比较两个字符串的大小，相等返回 0
 */
int8_t strcmp(const char* a, const char* b) {
	ASSERT(a != NULL && b != NULL);
	while (*a != 0 && *a == *b) {
		a++; b++;
	}
	return *a < *b? -1: *a > *b;
}

/**
 * 从左向右在 str 中查找 ch 首次出现的地址
 */
char* strchr(const char* str, const uint8_t ch) {
	ASSERT(str != NULL);
	while (*str != 0) {
		if (*str == ch) return (char*)str;
		str++;
	}
	return NULL;
}

/**
 * 从右向左在 str 中查找 ch 首次出现的地址
 */
char* strrchr(const char* str, const uint8_t ch) {
	ASSERT(str != NULL);
	const char* last_char = NULL;
	while (*str != 0) {
		if (*str == ch) last_char = str;
		str++;
	}
	return (char*) last_char;
}

/**
 * 将字符串 src_ 拼接到 dst_ 后，返回拼接的串地址
 */
char* strcat(char* dst_, const char* src_){
	ASSERT(dst_ != NULL && src_ != NULL);
	char* str = dst_;
	while (*str++);
	--str;
	while ((*str++ = *src_++));
	return dst_;
}

/**
 * 在字符串 str 中查找字符 ch 出现的次数
 */
uint32_t strchrs(const char* str, uint8_t ch) {
	ASSERT(str != NULL);
	uint32_t ch_cnt = 0;
	const char* p = str;
	while (*p != 0) {
		if (*p == ch) ch_cnt++;
		p++;
	}
	return ch_cnt;
}