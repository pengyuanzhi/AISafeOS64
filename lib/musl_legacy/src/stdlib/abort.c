/**
 * @file    abort.c
 * @brief   abort 实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 异常终止程序，进入死循环
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdlib.h>

/**
 * @brief 异常终止程序
 */
void abort(void)
{
    for (;;) {}
}
