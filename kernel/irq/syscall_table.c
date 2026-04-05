/**
 * @file    syscall_table.c
 * @brief   系统调用表实现（表驱动分发机制）
 * @author  AISafe64 Team
 * @date    2026-04-05
 * @version 1.0
 *
 * @details 本文件实现了系统调用表驱动的分发机制：
 *          - 系统调用表初始化
 *          - 处理函数注册与查找
 *          - 按系统调用号分发到对应处理函数
 *
 *          设计原则：
 *          - O(1) 时间复杂度的分发（数组直接索引）
 *          - 支持动态注册处理函数
 *          - 参数校验和错误处理
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: API-001~004
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */

#include <kernel/syscall.h>
#include <kernel/types.h>
#include <kernel/errno.h>
#include <stdint.h>
#include <stddef.h>

/* ========================================================================
 * 内部错误码（若 errno.h 中未定义则提供兼容定义）
 * ======================================================================== */

#ifndef ENOSYS
#define ENOSYS  38U
#endif

#ifndef EEXIST
#define EEXIST  17U
#endif

/* ========================================================================
 * 系统调用表（静态数组）
 * ======================================================================== */

/**
 * @brief 系统调用处理函数表
 *
 * @details 以系统调用号为索引的函数指针数组。
 *          NULL 表示该系统调用号未注册。
 *          初始化时所有槽位为 NULL。
 */
static syscall_handler_fn s_syscall_table[SYSCALL_TABLE_MAX];

/**
 * @brief 已注册的系统调用数量
 */
static uint32_t s_syscall_count;

/* ========================================================================
 * 公共 API 实现
 * ======================================================================== */

/**
 * @brief 初始化系统调用表
 *
 * @details 将所有槽位清零为 NULL，重置计数器。
 */
void syscall_table_init(void)
{
    uint32_t i;

    for (i = 0U; i < SYSCALL_TABLE_MAX; i++)
    {
        s_syscall_table[i] = NULL;
    }

    s_syscall_count = 0U;
}

/**
 * @brief 注册系统调用处理函数
 *
 * @param nr      系统调用号
 * @param handler 处理函数指针
 *
 * @return 成功返回 0，失败返回负错误码
 */
int32_t syscall_register(uint32_t nr, syscall_handler_fn handler)
{
    /* 参数校验：处理函数不能为 NULL */
    if (handler == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 参数校验：系统调用号不能越界 */
    if (nr >= SYSCALL_TABLE_MAX)
    {
        return -(int32_t)EINVAL;
    }

    /* 检查是否已注册 */
    if (s_syscall_table[nr] != NULL)
    {
        return -(int32_t)EEXIST;
    }

    /* 注册处理函数 */
    s_syscall_table[nr] = handler;

    /* 更新计数（溢出保护：理论上不会超过 SYSCALL_TABLE_MAX） */
    if (s_syscall_count < SYSCALL_TABLE_MAX)
    {
        s_syscall_count++;
    }

    return 0;
}

/**
 * @brief 分发系统调用
 *
 * @param frame 系统调用帧指针
 *
 * @return 处理函数返回值或错误码
 */
int64_t syscall_table_dispatch(syscall_frame_t *frame)
{
    uint32_t nr;
    syscall_handler_fn handler;

    /* 参数校验：frame 不能为 NULL */
    if (frame == NULL)
    {
        return -(int64_t)EINVAL;
    }

    /* 提取系统调用号 */
    nr = (uint32_t)(frame->x8 & 0xFFFFFFFFULL);

    /* 越界检查 */
    if (nr >= SYSCALL_TABLE_MAX)
    {
        return -(int64_t)ENOSYS;
    }

    /* 查找处理函数 */
    handler = s_syscall_table[nr];
    if (handler == NULL)
    {
        return -(int64_t)ENOSYS;
    }

    /* 调用处理函数 */
    return handler(frame);
}

/**
 * @brief 获取已注册的系统调用数量
 *
 * @return 已注册数量
 */
uint32_t syscall_table_count(void)
{
    return s_syscall_count;
}
