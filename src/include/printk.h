/**
 * @file printk.h
 * @brief AISafe64 RTOS - 内核打印头文件
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 内核日志输出接口
 */

#ifndef PRINTK_H
#define PRINTK_H

#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 内核打印初始化
 * @return 成功返回0，失败返回负错误码
 */
int printk_init(void);

/**
 * @brief 内核格式化打印函数
 * @param fmt 格式化字符串
 */
void printk(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* PRINTK_H */
