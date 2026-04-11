/**
 * @file    atexit.c
 * @brief   atexit 实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 注册退出处理函数（最多 32 个）
 *          exit() 按注册相反顺序调用
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdlib.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 最大注册函数数量 */
#define ATEXIT_MAX_FUNCS 32

/* ========================================================================
 * 静态变量
 * ======================================================================== */

/** @brief 注册函数数组 */
static atexit_fn s_atexit_funcs[ATEXIT_MAX_FUNCS];

/** @brief 已注册函数数量 */
static int s_atexit_count = 0;

/* ========================================================================
 * 公共接口
 * ======================================================================== */

/**
 * @brief 注册退出处理函数
 * @param func 处理函数指针
 * @return 成功返回 0，失败返回非零
 */
int atexit(void (*func)(void))
{
    if (s_atexit_count >= ATEXIT_MAX_FUNCS)
    {
        return 1;
    }

    s_atexit_funcs[s_atexit_count] = func;
    s_atexit_count++;

    return 0;
}

/**
 * @brief 获取已注册的 atexit 函数
 * @param index 索引（从 0 开始）
 * @return 函数指针，越界返回 NULL
 */
atexit_fn atexit_get_func(int index)
{
    if ((index < 0) || (index >= s_atexit_count))
    {
        return NULL;
    }
    return s_atexit_funcs[index];
}

/**
 * @brief 获取已注册函数数量
 * @return 已注册函数数量
 */
int atexit_get_count(void)
{
    return s_atexit_count;
}
