/**
 * @file types.h
 * @brief AISafe64 RTOS - 基本数据类型定义
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 标准数据类型定义
 *          - 遵循MISRA-C:2012规范
 *          - 使用标准整数类型
 */

#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 系统错误码类型
     */
    typedef int32_t ErrorCode_t;

    /**
     * @brief 有符号大小类型 (用于返回字节数或错误码)
     */
    typedef int64_t ssize_t;

/**
 * @brief 通用错误码定义
 */
#define ERROR_SUCCESS 0
#define ERROR_FAIL (-1)
#define ERROR_INVALID_PARAM (-2)
#define ERROR_OUT_OF_MEMORY (-3)
#define ERROR_TIMEOUT (-4)
#define ERROR_BUSY (-5)
#define ERROR_NOT_SUPPORTED (-6)
#define ERROR_NOT_FOUND (-7)
#define ERROR_INVALID_STATE (-8)
#define ERROR_WOULD_BLOCK (-9)
#define ERROR_OVERFLOW (-10)

/**
 * @brief NULL指针定义
 */
#ifndef NULL
#define NULL ((void *)0)
#endif

/**
 * @brief true/false定义
 */
#ifndef true
#define true 1
#endif

#ifndef false
#define false 0
#endif

#ifdef __cplusplus
}
#endif

#endif /* TYPES_H */
