#include "bitmap.h"
#include "stdint.h"
#include "string.h"
#include "print.h"
#include "interrupt.h"
#include "debug.h"

/**
 * 初始化位图，将其设置全部设置为 0
 */
void bitmap_init(bitmap* btmp) {
	memset(btmp->bits, 0, btmp->btmp_bytes_len);
}

/**
 * 判断 bit_idx 位是否为 1
 */
uint8_t bitmap_scan_test(bitmap* btmp, uint32_t bit_idx) {
	// 向下取整用于获取数组下标
	uint32_t byte_idx = bit_idx / 8;
	// 取余用于索引对应比特的位
	uint32_t bit_odd  = bit_idx % 8;
	return (btmp->bits[byte_idx] & (BITMAP_MASK << bit_odd));
}

/**
 * 在位图中申请连续 cnt 个位，若成功则返回其起始位的下标，否则返回 -1
 */
int bitmap_scan(bitmap* btmp, uint32_t cnt) {
	uint32_t idx_byte = 0;
	while (
		(0xff == btmp->bits[idx_byte])
		&&
		(idx_byte < btmp->btmp_bytes_len)
	) { idx_byte++; }


	ASSERT(idx_byte < btmp->btmp_bytes_len);
	if (idx_byte == btmp->btmp_bytes_len) return -1;

	int idx_bit = 0;
	while (
		(uint8_t)(BITMAP_MASK << idx_bit)
		&
		btmp->bits[idx_byte]
	) { idx_bit++; }

	int bit_idx_start = idx_byte*8 + idx_bit;
	if (cnt == 1) return bit_idx_start;

	uint32_t bit_left = (btmp->btmp_bytes_len*8 - bit_idx_start);
	uint32_t next_bit = bit_idx_start + 1;
	uint32_t count = 1;

	bit_idx_start = -1;
	while (bit_left-- > 0) {
		if (! bitmap_scan_test(btmp, next_bit)) {
			count++;
		} else {
			count = 0;
		}

		if (count == cnt) {
			bit_idx_start = next_bit - cnt + 1;
			break;
		}
		next_bit++;
	}
	return bit_idx_start;
}

/**
 * 将位图 btmp 的 bit_idx 位设置为 value
 */
void bitmap_set(bitmap* btmp, uint32_t bit_idx, int8_t value) {
	ASSERT((value == 0) || (value == 1));
	uint32_t byte_idx = bit_idx / 8;
	uint32_t bit_odd  = bit_idx % 8;

	if (value) {
		btmp->bits[byte_idx] |= (BITMAP_MASK << bit_odd);
	} else {
		btmp->bits[byte_idx] &= ~(BITMAP_MASK << bit_odd);
	}
}