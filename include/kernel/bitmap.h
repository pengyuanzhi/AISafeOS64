/**
 * @file    bitmap.h
 * @brief   256位优先级位图操作
 * @author  AISafe64 Team
 * @date    2026-03-31
 * @version 2.0
 *
 * @details 本文件实现了基于 4 个 uint64_t 的 256 位位图数据结构，
 *          专门用于 256 级优先级调度器的 O(1) 优先级查找。
 *
 *          位图布局：
 *          - bits[0]: 优先级   0 -  63
 *          - bits[1]: 优先级  64 - 127
 *          - bits[2]: 优先级 128 - 191
 *          - bits[3]: 优先级 192 - 255
 *
 *          特性：
 *          - 利用 __builtin_clzll / __builtin_ctzll 实现 O(1) 位查找
 *          - 支持 ARM64 CLZ/CTZ 硬件指令加速
 *          - 所有操作均为 static inline，零函数调用开销
 *
 * @note    MISRA-C:2012 合规
 * @warning bit 参数必须小于 256，否则行为未定义
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_BITMAP_H
#define KERNEL_BITMAP_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief 256位优先级总数
 */
#define BITMAP256_BITS      256U

/**
 * @brief 每个 uint64_t 包含的位数
 */
#define BITMAP256_BITS_PER_WORD  64U

/**
 * @brief 位图数组元素个数
 */
#define BITMAP256_WORDS      4U

/**
 * @brief 256位位图结构
 *
 * @details 使用 4 个 uint64_t 组成 256 位的位图。
 *          每一位对应一个优先级，置位表示该优先级有就绪任务。
 *
 *          位映射关系：
 *          - bits[0] 的第 0 位对应优先级 0（最低）
 *          - bits[3] 的第 63 位对应优先级 255（最高）
 */
typedef struct
{
    uint64_t bits[BITMAP256_WORDS]; /**< @brief 位图数组，共 256 位 */
} bitmap256_t;

/**
 * @def BITMAP256_INIT
 * @brief 256位位图静态全零初始化宏
 *
 * @details 将所有位初始化为 0，表示没有任何优先级就绪。
 *
 * @par 示例
 * @code
 * bitmap256_t ready_map = BITMAP256_INIT();
 * @endcode
 */
#define BITMAP256_INIT() { { 0ULL, 0ULL, 0ULL, 0ULL } }

/**
 * @brief 运行时将位图全部清零
 *
 * @details 将位图中所有位清零，等同于初始化为空状态。
 *
 * @param[in] bm 指向位图结构的指针
 */
static inline void bitmap256_clear_all(bitmap256_t *bm)
{
    if (bm == NULL)
    {
        return;
    }

    bm->bits[0U] = 0ULL;
    bm->bits[1U] = 0ULL;
    bm->bits[2U] = 0ULL;
    bm->bits[3U] = 0ULL;
}

/**
 * @brief 设置位图中的指定位
 *
 * @details 将位图中第 bit 位设置为 1。bit 值对应优先级编号（0-255）。
 *          值越大表示优先级越高。
 *
 * @param[in] bm  指向位图结构的指针
 * @param[in] bit 要设置的位索引（0-255）
 *
 * @note bit 必须 < 256，否则不会执行操作
 *
 * @par 示例
 * @code
 * bitmap256_set(&ready_map, 128U);
 * @endcode
 */
static inline void bitmap256_set(bitmap256_t *bm, uint32_t bit)
{
    uint32_t word;
    uint32_t offset;

    if (bm == NULL)
    {
        return;
    }

    if (bit >= BITMAP256_BITS)
    {
        return;
    }

    word = bit / BITMAP256_BITS_PER_WORD;
    offset = bit % BITMAP256_BITS_PER_WORD;

    bm->bits[word] |= (1ULL << offset);
}

/**
 * @brief 清除位图中的指定位
 *
 * @details 将位图中第 bit 位清除为 0。
 *
 * @param[in] bm  指向位图结构的指针
 * @param[in] bit 要清除的位索引（0-255）
 *
 * @note bit 必须 < 256，否则不会执行操作
 *
 * @par 示例
 * @code
 * bitmap256_clear(&ready_map, 128U);
 * @endcode
 */
static inline void bitmap256_clear(bitmap256_t *bm, uint32_t bit)
{
    uint32_t word;
    uint32_t offset;

    if (bm == NULL)
    {
        return;
    }

    if (bit >= BITMAP256_BITS)
    {
        return;
    }

    word = bit / BITMAP256_BITS_PER_WORD;
    offset = bit % BITMAP256_BITS_PER_WORD;

    bm->bits[word] &= ~(1ULL << offset);
}

/**
 * @brief 测试位图中的指定位是否被设置
 *
 * @details 检查位图中第 bit 位是否为 1。
 *
 * @param[in] bm  指向位图结构的指针（不会修改）
 * @param[in] bit 要测试的位索引（0-255）
 *
 * @return 非0表示该位已被设置，0表示该位未设置或参数无效
 *
 * @note bit 必须 < 256，否则返回 0
 */
static inline int bitmap256_test(const bitmap256_t *bm, uint32_t bit)
{
    uint32_t word;
    uint32_t offset;

    if (bm == NULL)
    {
        return 0;
    }

    if (bit >= BITMAP256_BITS)
    {
        return 0;
    }

    word = bit / BITMAP256_BITS_PER_WORD;
    offset = bit % BITMAP256_BITS_PER_WORD;

    return ((bm->bits[word] & (1ULL << offset)) != 0ULL) ? 1 : 0;
}

/**
 * @brief 查找位图中最高位的被设置位置
 *
 * @details 从高优先级向低优先级搜索，返回第一个被设置的位索引。
 *          利用 __builtin_clzll 实现 O(1) 查找。
 *
 *          搜索顺序：bits[3] -> bits[2] -> bits[1] -> bits[0]
 *          即优先级 255 -> 0
 *
 * @param[in] bm 指向位图结构的指针（不会修改）
 *
 * @return 最高位的索引（0-255），如果位图为空则返回 256
 *
 * @note ARM64 上 __builtin_clzll 编译为 CLZ 指令，单周期完成
 *
 * @par 示例
 * @code
 * uint32_t prio = bitmap256_find_highest(&ready_map);
 * if (prio < 256U)
 * {
 *     TCB_t *task = dequeue_priority(prio);
 * }
 * @endcode
 */
static inline uint32_t bitmap256_find_highest(const bitmap256_t *bm)
{
    int leading_zeros;
    uint32_t word_idx;

    if (bm == NULL)
    {
        return BITMAP256_BITS;
    }

    /* 从最高字 bits[3] 开始搜索 */
    for (word_idx = BITMAP256_WORDS; word_idx > 0U; word_idx--)
    {
        uint32_t idx = word_idx - 1U;

        if (bm->bits[idx] != 0ULL)
        {
            leading_zeros = __builtin_clzll(bm->bits[idx]);
            return (idx * BITMAP256_BITS_PER_WORD) +
                   (uint32_t)(BITMAP256_BITS_PER_WORD - 1U) -
                   (uint32_t)leading_zeros;
        }
    }

    return BITMAP256_BITS;
}

/**
 * @brief 查找位图中最低位的被设置位置
 *
 * @details 从低优先级向高优先级搜索，返回第一个被设置的位索引。
 *          利用 __builtin_ctzll 实现 O(1) 查找。
 *
 *          搜索顺序：bits[0] -> bits[1] -> bits[2] -> bits[3]
 *          即优先级 0 -> 255
 *
 * @param[in] bm 指向位图结构的指针（不会修改）
 *
 * @return 最低位的索引（0-255），如果位图为空则返回 256
 *
 * @note ARM64 上 __builtin_ctzll 编译为 RBIT + CLZ 指令序列
 */
static inline uint32_t bitmap256_find_lowest(const bitmap256_t *bm)
{
    uint32_t word_idx;

    if (bm == NULL)
    {
        return BITMAP256_BITS;
    }

    /* 从最低字 bits[0] 开始搜索 */
    for (word_idx = 0U; word_idx < BITMAP256_WORDS; word_idx++)
    {
        if (bm->bits[word_idx] != 0ULL)
        {
            int trailing_zeros = __builtin_ctzll(bm->bits[word_idx]);
            return (word_idx * BITMAP256_BITS_PER_WORD) +
                   (uint32_t)trailing_zeros;
        }
    }

    return BITMAP256_BITS;
}

/**
 * @brief 判断位图是否为空
 *
 * @details 检查位图中是否有任何位被设置。
 *          当所有位均为 0 时返回非0值。
 *
 * @param[in] bm 指向位图结构的指针（不会修改）
 *
 * @return 非0表示位图为空（无就绪任务），0表示位图非空
 */
static inline int bitmap256_empty(const bitmap256_t *bm)
{
    if (bm == NULL)
    {
        return 1;
    }

    return ((bm->bits[0U] | bm->bits[1U] | bm->bits[2U] | bm->bits[3U]) == 0ULL)
           ? 1 : 0;
}

/**
 * @brief 计算位图中被设置的位数（置位计数）
 *
 * @details 使用 __builtin_popcountll 统计每个 uint64_t 中置位的位数，
 *          然后汇总得到总计数。
 *
 * @param[in] bm 指向位图结构的指针（不会修改）
 *
 * @return 位图中被设置为 1 的位的总数
 *
 * @note ARM64 上 __builtin_popcountll 编译为 CNT 指令
 */
static inline uint32_t bitmap256_count(const bitmap256_t *bm)
{
    uint32_t count;

    if (bm == NULL)
    {
        return 0U;
    }

    count = (uint32_t)__builtin_popcountll(bm->bits[0U]) +
            (uint32_t)__builtin_popcountll(bm->bits[1U]) +
            (uint32_t)__builtin_popcountll(bm->bits[2U]) +
            (uint32_t)__builtin_popcountll(bm->bits[3U]);

    return count;
}

/**
 * @brief 合并两个位图（按位或运算）
 *
 * @details 将 src 位图中的位合并到 dst 位图中，使用按位或运算。
 *          合并后 dst 中某位为 1 当且仅当 dst 或 src 中对应位为 1。
 *          src 位图不会被修改。
 *
 * @param[in,out] dst 目标位图，合并结果存放在此
 * @param[in]     src 源位图（不会被修改）
 *
 * @note 通常用于多核就绪队列的合并：
 *       每个 CPU 维护独立的就绪位图，调度时合并所有位图
 *
 * @par 示例
 * @code
 * bitmap256_merge(&global_ready, &per_cpu_ready[cpu_id]);
 * @endcode
 */
static inline void bitmap256_merge(bitmap256_t *dst, const bitmap256_t *src)
{
    if ((dst == NULL) || (src == NULL))
    {
        return;
    }

    dst->bits[0U] |= src->bits[0U];
    dst->bits[1U] |= src->bits[1U];
    dst->bits[2U] |= src->bits[2U];
    dst->bits[3U] |= src->bits[3U];
}

#endif /* KERNEL_BITMAP_H */
