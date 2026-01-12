/**
 * @file bitmap.h
 * @brief AISafe64 RTOS - 位图操作
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 位图操作接口
 *          - 设置/清除位
 *          - 查找第一位
 *          - 位运算
 *
 * @note MISRA-C:2012合规
 *
 * @copyright Copyright (c) 2025 AISafe64 Team
 */

#ifndef BITMAP_H
#define BITMAP_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief 位图字大小（位）
 */
#define BITS_PER_LONG (sizeof(uint64_t) * 8)

/**
 * @brief 位图字对齐
 */
#define BITS_TO_LONGS(bits) (((bits) + BITS_PER_LONG - 1) / BITS_PER_LONG)

/**
 * @brief 声明位图
 */
#define DECLARE_BITMAP(name, bits) uint64_t name[BITS_TO_LONGS(bits)]

/**
 * @brief 定义位图
 */
#define DEFINE_BITMAP(name, bits) uint64_t name[BITS_TO_LONGS(bits)] = {0}

    /**
     * @brief 设置位
     * @param bitmap 位图
     * @param nr 位号
     */
    static inline void set_bit(uint64_t *bitmap, uint32_t nr)
    {
        uint64_t word = nr / BITS_PER_LONG;
        uint64_t bit = nr % BITS_PER_LONG;
        bitmap[word] |= (1UL << bit);
    }

    /**
     * @brief 清除位
     * @param bitmap 位图
     * @param nr 位号
     */
    static inline void clear_bit(uint64_t *bitmap, uint32_t nr)
    {
        uint64_t word = nr / BITS_PER_LONG;
        uint64_t bit = nr % BITS_PER_LONG;
        bitmap[word] &= ~(1UL << bit);
    }

    /**
     * @brief 改变位
     * @param bitmap 位图
     * @param nr 位号
     */
    static inline void change_bit(uint64_t *bitmap, uint32_t nr)
    {
        uint64_t word = nr / BITS_PER_LONG;
        uint64_t bit = nr % BITS_PER_LONG;
        bitmap[word] ^= (1UL << bit);
    }

    /**
     * @brief 测试位
     * @param bitmap 位图
     * @param nr 位号
     * @return 位值（0或1）
     */
    static inline int test_bit(const uint64_t *bitmap, uint32_t nr)
    {
        uint64_t word = nr / BITS_PER_LONG;
        uint64_t bit = nr % BITS_PER_LONG;
        return (int)((bitmap[word] >> bit) & 1UL);
    }

    /**
     * @brief 测试并设置位
     * @param bitmap 位图
     * @param nr 位号
     * @return 原始位值
     */
    static inline int test_and_set_bit(uint64_t *bitmap, uint32_t nr)
    {
        uint64_t word = nr / BITS_PER_LONG;
        uint64_t bit = nr % BITS_PER_LONG;
        uint64_t old = bitmap[word];
        bitmap[word] |= (1UL << bit);
        return (int)((old >> bit) & 1UL);
    }

    /**
     * @brief 测试并清除位
     * @param bitmap 位图
     * @param nr 位号
     * @return 原始位值
     */
    static inline int test_and_clear_bit(uint64_t *bitmap, uint32_t nr)
    {
        uint64_t word = nr / BITS_PER_LONG;
        uint64_t bit = nr % BITS_PER_LONG;
        uint64_t old = bitmap[word];
        bitmap[word] &= ~(1UL << bit);
        return (int)((old >> bit) & 1UL);
    }

    /**
     * @brief 查找第一个置位（从低位开始）
     * @param bitmap 位图
     * @param nbits 位图总位数
     * @return 位号，未找到返回-1
     */
    int find_first_set_bit(const uint64_t *bitmap, uint32_t nbits);

    /**
     * @brief 查找第一个清零位（从低位开始）
     * @param bitmap 位图
     * @param nbits 位图总位数
     * @return 位号，未找到返回-1
     */
    int find_first_zero_bit(const uint64_t *bitmap, uint32_t nbits);

    /**
     * @brief 查找最后一个置位（从高位开始）
     * @param bitmap 位图
     * @param nbits 位图总位数
     * @return 位号，未找到返回-1
     */
    int find_last_set_bit(const uint64_t *bitmap, uint32_t nbits);

    /**
     * @brief 查找下一个置位
     * @param bitmap 位图
     * @param nbits 位图总位数
     * @param start 起始位号
     * @return 位号，未找到返回-1
     */
    int find_next_set_bit(const uint64_t *bitmap, uint32_t nbits, uint32_t start);

    /**
     * @brief 查找下一个清零位
     * @param bitmap 位图
     * @param nbits 位图总位数
     * @param start 起始位号
     * @return 位号，未找到返回-1
     */
    int find_next_zero_bit(const uint64_t *bitmap, uint32_t nbits, uint32_t start);

    /**
     * @brief 统计置位数
     * @param bitmap 位图
     * @param nbits 位图总位数
     * @return 置位数
     */
    uint32_t bitmap_count_bits(const uint64_t *bitmap, uint32_t nbits);

    /**
     * @brief 位图与运算
     * @param dst 目标位图
     * @param src1 源位图1
     * @param src2 源位图2
     * @param nbits 位图总位数
     */
    void bitmap_and(uint64_t *dst, const uint64_t *src1, const uint64_t *src2, uint32_t nbits);

    /**
     * @brief 位图或运算
     * @param dst 目标位图
     * @param src1 源位图1
     * @param src2 源位图2
     * @param nbits 位图总位数
     */
    void bitmap_or(uint64_t *dst, const uint64_t *src1, const uint64_t *src2, uint32_t nbits);

    /**
     * @brief 位图异或运算
     * @param dst 目标位图
     * @param src1 源位图1
     * @param src2 源位图2
     * @param nbits 位图总位数
     */
    void bitmap_xor(uint64_t *dst, const uint64_t *src1, const uint64_t *src2, uint32_t nbits);

    /**
     * @brief 位图非运算
     * @param dst 目标位图
     * @param src 源位图
     * @param nbits 位图总位数
     */
    void bitmap_not(uint64_t *dst, const uint64_t *src, uint32_t nbits);

    /**
     * @brief 清空位图
     * @param bitmap 位图
     * @param nbits 位图总位数
     */
    static inline void bitmap_zero(uint64_t *bitmap, uint32_t nbits)
    {
        uint32_t nlongs = BITS_TO_LONGS(nbits);
        for (uint32_t i = 0; i < nlongs; i++)
        {
            bitmap[i] = 0ULL;
        }
    }

    /**
     * @brief 填充位图（全部置位）
     * @param bitmap 位图
     * @param nbits 位图总位数
     */
    static inline void bitmap_fill(uint64_t *bitmap, uint32_t nbits)
    {
        uint32_t nlongs = BITS_TO_LONGS(nbits);
        for (uint32_t i = 0; i < nlongs; i++)
        {
            bitmap[i] = ~0ULL;
        }
    }

    /**
     * @brief 拷贝位图
     * @param dst 目标位图
     * @param src 源位图
     * @param nbits 位图总位数
     */
    static inline void bitmap_copy(uint64_t *dst, const uint64_t *src, uint32_t nbits)
    {
        uint32_t nlongs = BITS_TO_LONGS(nbits);
        for (uint32_t i = 0; i < nlongs; i++)
        {
            dst[i] = src[i];
        }
    }

    /**
     * @brief 比较位图
     * @param src1 源位图1
     * @param src2 源位图2
     * @param nbits 位图总位数
     * @return 0表示相等，非0表示不等
     */
    int bitmap_equal(const uint64_t *src1, const uint64_t *src2, uint32_t nbits);

    /**
     * @brief 位图是否为空
     * @param bitmap 位图
     * @param nbits 位图总位数
     * @return 1表示空，0表示非空
     */
    int bitmap_empty(const uint64_t *bitmap, uint32_t nbits);

    /**
     * @brief 位图是否全满
     * @param bitmap 位图
     * @param nbits 位图总位数
     * @return 1表示全满，0表示非全满
     */
    int bitmap_full(const uint64_t *bitmap, uint32_t nbits);

#ifdef __cplusplus
}
#endif

#endif /* BITMAP_H */
