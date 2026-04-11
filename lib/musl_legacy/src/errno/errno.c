/**
 * @file    errno.c
 * @brief   errno 线程局部存储实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 提供 errno 的存储位置。
 *          单线程环境下使用全局静态变量。
 *          多线程环境下应改为线程局部存储（TLS）。
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/**
 * @brief errno 值存储
 *
 * @details 单线程环境使用全局变量。
 *          初始值为 0，表示无错误。
 */
static int s_errno_value = 0;

/**
 * @brief 获取 errno 存储位置
 *
 * @details 返回指向当前线程 errno 值的指针。
 *          在单线程环境下返回全局变量地址。
 *
 * @return 指向 errno 值的指针
 */
int *__errno_location(void)
{
    return &s_errno_value;
}
