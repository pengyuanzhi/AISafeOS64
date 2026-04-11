/**
 * @file    atoi.c
 * @brief   atoi 实现
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 调用 strtol 实现 atoi
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdlib.h>

/**
 * @brief 字符串转换为整数
 * @param nptr 输入字符串
 * @return 转换后的整数值
 */
int atoi(const char *nptr)
{
    return (int)strtol(nptr, (char **)NULL, 10);
}
