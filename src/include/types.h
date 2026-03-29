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
 * @brief POSIX 标准错误码定义
 *
 * @note 遵循 POSIX.1-2008 标准
 * @note 与 Linux kernel 错误码兼容
 * @note 负数表示错误，正数和零表示成功
 *
 * 常用 POSIX 错误码：
 * - EPERM (1): Operation not permitted
 * - EINVAL (22): Invalid argument
 * - EAGAIN (11): Resource temporarily unavailable (would block)
 * - ETIMEDOUT (110): Connection timed out
 * - EOVERFLOW (75): Value too large for defined data type
 * - ENOMEM (12): Out of memory
 * - EBUSY (16): Device or resource busy
 * - ENOTSUP (95): Not supported (Linux: EOPNOTSUPP)
 */
#define EPERM 1U       /**< Operation not permitted */
#define ENOENT 2U      /**< No such file or directory */
#define EINVAL 22U     /**< Invalid argument */
#define EAGAIN 11U     /**< Resource temporarily unavailable */
#define ETIMEDOUT 110U /**< Connection timed out */
#define EOVERFLOW 75U  /**< Value too large */
#define ENOMEM 12U     /**< Out of memory */
#define EBUSY 16U      /**< Device or resource busy */
#define ENOTSUP 95U    /**< Not supported */

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
#define true 1U
#endif

#ifndef false
#define false 0U
#endif

#ifdef __cplusplus
}
#endif

#endif /* TYPES_H */
