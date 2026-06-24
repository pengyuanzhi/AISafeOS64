/**
 * @file    kmalloc.h
 * @brief   内核通用内存分配器接口
 * @author  AISafe64 Team
 * @date    2026-06-10
 * @version 1.0
 *
 * @details 内核通用动态内存分配器：
 *          - kmalloc: 通用内存分配
 *          - kzalloc: 清零内存分配
 *          - kfree: 内存释放
 *          - kfree_secure: 安全释放（清零后释放）
 *          - kmalloc_init: 分配器初始化
 *          - kmalloc_get_stats: 分配统计
 *
 * @note    MISRA-C:2012 合规
 * @note    分配结果保证 16 字节对齐（ARM64 ABI）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_MM_KMALLOC_H
#define KERNEL_MM_KMALLOC_H

#include <stddef.h>
#include <stdint.h>

/* ========================================================================
 * 分配统计结构
 * ======================================================================== */

/**
 * @brief 内存分配统计信息
 */
typedef struct
{
    size_t total_allocs;     /**< @brief 累计分配次数 */
    size_t total_frees;      /**< @brief 累计释放次数 */
    size_t current_allocs;   /**< @brief 当前活跃分配数 */
    size_t total_bytes;      /**< @brief 累计分配字节数 */
    size_t current_bytes;    /**< @brief 当前已分配字节数 */
    size_t peak_bytes;       /**< @brief 峰值已分配字节数 */
} kmalloc_stats_t;

/* ========================================================================
 * 分配器配置
 * ======================================================================== */

/**
 * @def KMALLOC_HEAP_SIZE
 * @brief 内核堆大小（字节）
 *
 * @details 默认 4MB，可通过 Kconfig 配置
 */
#ifndef KMALLOC_HEAP_SIZE
#define KMALLOC_HEAP_SIZE       (2U * 1024U * 1024U)  /* 2MB - 与链接脚本 __heap_start/__heap_end 匹配 */
#endif

/**
 * @def KMALLOC_ALIGN
 * @brief 分配对齐要求（字节）
 */
#define KMALLOC_ALIGN           16U

/**
 * @def KMALLOC_MIN_SIZE
 * @brief 最小分配大小（字节）
 */
#define KMALLOC_MIN_SIZE        16U

/**
 * @def KMALLOC_MAX_SIZE
 * @brief 最大单次分配大小（字节）
 */
#define KMALLOC_MAX_SIZE        (1024U * 1024U)

/**
 * @def KMALLOC_MAGIC
 * @brief 分配块魔数（用于检测损坏/双重释放）
 */
#define KMALLOC_MAGIC           0x4B4D414CU  /* "KMAL" */

/* ========================================================================
 * 公共接口函数
 * ======================================================================== */

/**
 * @brief 初始化内核内存分配器
 *
 * @details 初始化内核堆，必须在首次 kmalloc 调用前执行
 *          使用编译时指定的堆区域（KMALLOC_HEAP_SIZE）
 *
 * @return 0 成功
 * @return -ENOMEM 内存不足
 *
 * @note 在内核启动阶段调用一次
 */
int32_t kmalloc_init(void);

/**
 * @brief 分配内核内存
 *
 * @param size 请求分配的字节数
 *
 * @return 分配的内存指针（16 字节对齐）
 * @return NULL 分配失败（size 为 0 或内存不足）
 *
 * @note 分配的内存内容未初始化
 * @note 调用者应检查返回值是否为 NULL
 *
 * @par 示例
 * @code
 * void *buf = kmalloc(256);
 * if (buf != NULL)
 * {
 *     (void)memset(buf, 0, 256);
 *     // 使用 buf ...
 *     kfree(buf);
 * }
 * @endcode
 */
void *kmalloc(size_t size);

/**
 * @brief 分配清零的内核内存
 *
 * @param size 请求分配的字节数
 *
 * @return 分配的内存指针（16 字节对齐，内容全零）
 * @return NULL 分配失败
 *
 * @note 等价于 kmalloc + memset(buf, 0, size)
 */
void *kzalloc(size_t size);

/**
 * @brief 释放内核内存
 *
 * @param ptr 要释放的内存指针（由 kmalloc/kzalloc 返回）
 *
 * @note 释放 NULL 指针是安全的（无操作）
 * @note 重复释放同一指针是未定义行为
 */
void kfree(void *ptr);

/**
 * @brief 安全释放内核内存（清零后释放）
 *
 * @param ptr  要释放的内存指针
 * @param size 原始分配大小（字节）
 *
 * @details 先将内存内容清零，再释放。适用于包含敏感数据的缓冲区。
 *          释放 NULL 指针是安全的（无操作）。
 *
 * @note size 应与分配时的大小一致
 */
void kfree_secure(void *ptr, size_t size);

/**
 * @brief 获取内存分配统计信息
 *
 * @param stats 统计信息输出结构（调用者分配）
 *
 * @return 0 成功
 * @return -EINVAL stats 为 NULL
 */
int32_t kmalloc_get_stats(kmalloc_stats_t *stats);

#endif /* KERNEL_MM_KMALLOC_H */
