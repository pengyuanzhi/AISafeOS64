/**
 * @file bitmap.c
 * @brief AISafe64 RTOS - 位图操作实现
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 位图操作实现
 *
 * @note MISRA-C:2012合规
 */

#include "bitmap.h"
#include <string.h>

/**
 * @brief 查找第一个置位（从低位开始）
 * @param bitmap 位图
 * @param nbits 位图总位数
 * @return 位号，未找到返回-1
 */
int find_first_set_bit(const uint64_t *bitmap, uint32_t nbits) {
    uint32_t nlongs = BITS_TO_LONGS(nbits);

    for (uint32_t i = 0; i < nlongs; i++) {
        if (bitmap[i] != 0ULL) {
            /* 使用CLZ查找第一个置位 */
            uint32_t bit = (uint32_t)__builtin_clzll(bitmap[i]);
            uint32_t result = i * BITS_PER_LONG + (BITS_PER_LONG - 1 - bit);

            /* 检查是否超出范围 */
            if (result < nbits) {
                return (int)result;
            }
        }
    }

    return -1;
}

/**
 * @brief 查找第一个清零位（从低位开始）
 * @param bitmap 位图
 * @param nbits 位图总位数
 * @return 位号，未找到返回-1
 */
int find_first_zero_bit(const uint64_t *bitmap, uint32_t nbits) {
    uint32_t nlongs = BITS_TO_LONGS(nbits);

    for (uint32_t i = 0; i < nlongs; i++) {
        if (bitmap[i] != ~0ULL) {
            /* 反转位图查找第一个置位 */
            uint64_t inverted = ~bitmap[i];
            uint32_t bit = (uint32_t)__builtin_clzll(inverted);
            uint32_t result = i * BITS_PER_LONG + (BITS_PER_LONG - 1 - bit);

            /* 检查是否超出范围 */
            if (result < nbits) {
                return (int)result;
            }
        }
    }

    return -1;
}

/**
 * @brief 查找最后一个置位（从高位开始）
 * @param bitmap 位图
 * @param nbits 位图总位数
 * @return 位号，未找到返回-1
 */
int find_last_set_bit(const uint64_t *bitmap, uint32_t nbits) {
    uint32_t nlongs = BITS_TO_LONGS(nbits);

    for (int i = (int)nlongs - 1; i >= 0; i--) {
        if (bitmap[i] != 0ULL) {
            /* 使用CTZ查找最后一个置位 */
            uint32_t bit = (uint32_t)__builtin_ctzll(bitmap[i]);
            uint32_t result = i * BITS_PER_LONG + bit;

            /* 检查是否超出范围 */
            if (result < nbits) {
                return (int)result;
            }
        }
    }

    return -1;
}

/**
 * @brief 查找下一个置位
 * @param bitmap 位图
 * @param nbits 位图总位数
 * @param start 起始位号
 * @return 位号，未找到返回-1
 */
int find_next_set_bit(const uint64_t *bitmap, uint32_t nbits, uint32_t start) {
    if (start >= nbits) {
        return -1;
    }

    uint32_t word = start / BITS_PER_LONG;
    uint32_t bit = start % BITS_PER_LONG;
    uint64_t mask = bitmap[word] >> bit;

    /* 检查当前字 */
    if (mask != 0ULL) {
        uint32_t offset = (uint32_t)__builtin_ctzll(mask);
        uint32_t result = start + offset;

        if (result < nbits) {
            return (int)result;
        }
    }

    /* 检查后续字 */
    for (uint32_t i = word + 1; i < BITS_TO_LONGS(nbits); i++) {
        if (bitmap[i] != 0ULL) {
            uint32_t bit_offset = (uint32_t)__builtin_ctzll(bitmap[i]);
            uint32_t result = i * BITS_PER_LONG + bit_offset;

            if (result < nbits) {
                return (int)result;
            }
        }
    }

    return -1;
}

/**
 * @brief 查找下一个清零位
 * @param bitmap 位图
 * @param nbits 位图总位数
 * @param start 起始位号
 * @return 位号，未找到返回-1
 */
int find_next_zero_bit(const uint64_t *bitmap, uint32_t nbits, uint32_t start) {
    if (start >= nbits) {
        return -1;
    }

    uint32_t word = start / BITS_PER_LONG;
    uint32_t bit = start % BITS_PER_LONG;
    uint64_t mask = ~(bitmap[word] >> bit);

    /* 检查当前字 */
    if (mask != 0ULL) {
        uint32_t offset = (uint32_t)__builtin_ctzll(mask);
        uint32_t result = start + offset;

        if (result < nbits) {
            return (int)result;
        }
    }

    /* 检查后续字 */
    for (uint32_t i = word + 1; i < BITS_TO_LONGS(nbits); i++) {
        if (bitmap[i] != ~0ULL) {
            uint64_t inverted = ~bitmap[i];
            uint32_t bit_offset = (uint32_t)__builtin_ctzll(inverted);
            uint32_t result = i * BITS_PER_LONG + bit_offset;

            if (result < nbits) {
                return (int)result;
            }
        }
    }

    return -1;
}

/**
 * @brief 统计置位数
 * @param bitmap 位图
 * @param nbits 位图总位数
 * @return 置位数
 */
uint32_t bitmap_count_bits(const uint64_t *bitmap, uint32_t nbits) {
    uint32_t count = 0U;
    uint32_t nlongs = BITS_TO_LONGS(nbits);

    for (uint32_t i = 0; i < nlongs; i++) {
        /* 使用popcount统计置位数 */
        count += (uint32_t)__builtin_popcountll(bitmap[i]);
    }

    return count;
}

/**
 * @brief 位图与运算
 */
void bitmap_and(uint64_t *dst, const uint64_t *src1,
                const uint64_t *src2, uint32_t nbits) {
    uint32_t nlongs = BITS_TO_LONGS(nbits);

    for (uint32_t i = 0; i < nlongs; i++) {
        dst[i] = src1[i] & src2[i];
    }
}

/**
 * @brief 位图或运算
 */
void bitmap_or(uint64_t *dst, const uint64_t *src1,
               const uint64_t *src2, uint32_t nbits) {
    uint32_t nlongs = BITS_TO_LONGS(nbits);

    for (uint32_t i = 0; i < nlongs; i++) {
        dst[i] = src1[i] | src2[i];
    }
}

/**
 * @brief 位图异或运算
 */
void bitmap_xor(uint64_t *dst, const uint64_t *src1,
                const uint64_t *src2, uint32_t nbits) {
    uint32_t nlongs = BITS_TO_LONGS(nbits);

    for (uint32_t i = 0; i < nlongs; i++) {
        dst[i] = src1[i] ^ src2[i];
    }
}

/**
 * @brief 位图非运算
 */
void bitmap_not(uint64_t *dst, const uint64_t *src, uint32_t nbits) {
    uint32_t nlongs = BITS_TO_LONGS(nbits);

    for (uint32_t i = 0; i < nlongs; i++) {
        dst[i] = ~src[i];
    }
}

/**
 * @brief 比较位图
 */
int bitmap_equal(const uint64_t *src1, const uint64_t *src2, uint32_t nbits) {
    uint32_t nlongs = BITS_TO_LONGS(nbits);

    for (uint32_t i = 0; i < nlongs; i++) {
        if (src1[i] != src2[i]) {
            return 0;
        }
    }

    return 1;
}

/**
 * @brief 位图是否为空
 */
int bitmap_empty(const uint64_t *bitmap, uint32_t nbits) {
    return find_first_set_bit(bitmap, nbits) < 0;
}

/**
 * @brief 位图是否全满
 */
int bitmap_full(const uint64_t *bitmap, uint32_t nbits) {
    return find_first_zero_bit(bitmap, nbits) < 0;
}
