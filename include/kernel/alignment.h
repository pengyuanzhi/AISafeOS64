/**
 * @file    alignment.h
 * @brief   数据对齐工具
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 *
 * @details 提供数据对齐的工具函数：
 *          - 对齐检查
 *          - 对齐分配
 *          - 对齐辅助函数
 *
 * @note MISRA C:2012 合规
 * @note 对应阶段 1.6 - 缓存优化
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_ALIGNMENT_H
#define KERNEL_ALIGNMENT_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <stdint.h>
#include <stddef.h>

/* ========================================================================
 * 缓存行大小定义
 * ======================================================================== */

/** @brief ARM64 缓存行大小（通常为 64 字节） */
#define CACHE_LINE_SIZE        64U

/** @brief 对齐到 cache line */
#define CACHE_ALIGN(x)         __attribute__((aligned(CACHE_LINE_SIZE)))

/** @brief 2 的幂次方对齐 */
#define ALIGN_POWER_OF_2(x)    (1U << (x))

/** @brief 对齐到 power of 2 */
#define ALIGN_POWER_OF_2_MASK(x)  ((1U << (x)) - 1U)

/** @brief 8 字节对齐（64位系统默认对齐） */
#define ALIGN_8                 8U

/** @brief 16 字节对齐（通常 cache line 大小） */
#define ALIGN_16                16U

/** @brief 32 字节对齐（性能优化对齐） */
#define ALIGN_32                32U

/* ========================================================================
 * 对齐检查
 * ======================================================================== */

/**
 * @brief 检查指针是否对齐到指定对齐值
 *
 * @param ptr   指针
 * @param align 对齐值（必须是 power of 2）
 *
 * @return true 表示对齐，false 表示未对齐
 */
static inline bool is_aligned_to(const void *ptr, uint64_t align)
{
    return ((uint64_t)ptr & (align - 1U)) == 0U;
}

/**
 * @brief 检查指针是否对齐到 8 字节
 *
 * @param ptr 指针
 *
 * @return true 表示对齐，false 表示未对齐
 */
static inline bool is_aligned_8(const void *ptr)
{
    return is_aligned_to(ptr, ALIGN_8);
}

/**
 * @brief 检查指针是否对齐到 16 字节
 *
 * @param ptr 指针
 *
 * @return true 表示对齐，false 表示未对齐
 */
static inline bool is_aligned_16(const void *ptr)
{
    return is_aligned_to(ptr, ALIGN_16);
}

/**
 * @brief 检查指针是否对齐到 32 字节
 *
 * @param ptr 指针
 *
 * @return true 表示对齐，false 表示未对齐
 */
static inline bool is_aligned_32(const void *ptr)
{
    return is_aligned_to(ptr, ALIGN_32);
}

/**
 * @brief 检查指针是否对齐到 64 字节（cache line）
 *
 * @param ptr 指针
 *
 * @return true 表示对齐，false 表示未对齐
 */
static inline bool is_aligned_cache_line(const void *ptr)
{
    return is_aligned_to(ptr, CACHE_LINE_SIZE);
}

/* ========================================================================
 * 对齐辅助函数
 * ======================================================================== */

/**
 * @brief 向上对齐到指定值
 *
 * @details 示例：
 *          align_up(10, 4) = 12
 *          align_up(16, 8) = 16
 *          align_up(20, 8) = 24
 *
 * @param value  值
 * @param align  对齐值（必须是 power of 2）
 *
 * @return 向上对齐后的值
 */
static inline uint64_t align_up(uint64_t value, uint64_t align)
{
    return ((value + (align - 1U)) & (~(align - 1U)));
}

/**
 * @brief 向下对齐到指定值
 *
 * @param value  值
 * @param align  对齐值（必须是 power of 2）
 *
 * @return 向下对齐后的值
 */
static inline uint64_t align_down(uint64_t value, uint64_t align)
{
    return (value & (~(align - 1U)));
}

/**
 * @brief 计算填充到 cache line 的大小
 *
 * @param size  当前大小
 *
 * @return 填充到 cache line 后的大小
 */
static inline uint64_t align_to_cache_line(uint64_t size)
{
    return ((size + (CACHE_LINE_SIZE - 1U)) & (~(CACHE_LINE_SIZE - 1U)));
}

/**
 * @brief 计算缓存行数量
 *
 * @param size  数据大小
 *
 * @return 需要的缓存行数量
 */
static inline uint64_t cache_lines(uint64_t size)
{
    return ((size + (CACHE_LINE_SIZE - 1U)) / CACHE_LINE_SIZE);
}

/* ========================================================================
 * 缓存行对齐结构体辅助
 * ======================================================================== */

/**
 * @brief 缓存行对齐结构体包装
 *
 * @details 用于在运行时对齐结构体
 *
 * @param struct_type 结构体类型
 * @param struct_name 结构体名称
 * @param ... 结构体成员列表
 */
#define CACHE_ALIGN_STRUCT(struct_type, struct_name, ...) \
    struct struct_name { \
        __VA_ARGS__ \
        char _pad[CACHE_LINE_SIZE - sizeof(struct(struct_name)) % CACHE_LINE_SIZE]; \
    }

/**
 * @brief 2 的幂次方对齐结构体包装
 *
 * @param struct_type 结构体类型
 * @param struct_name 结构体名称
 * @param align 对齐大小（必须是 power of 2）
 * @param ... 结构体成员列表
 */
#define ALIGN_POWER_OF_2_STRUCT(struct_type, struct_name, align, ...) \
    struct struct_name { \
        __VA_ARGS__ \
        char _pad[(align) - sizeof(struct(struct_name)) % (align)]; \
    }

#endif /* KERNEL_ALIGNMENT_H */
