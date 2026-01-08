/**
 * @file uart.h
 * @brief AISafe64 RTOS - UART驱动头文件
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details PL011 UART驱动接口
 */

#ifndef UART_H
#define UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化UART
 * @return 成功返回0，失败返回负错误码
 */
int uart_init(void);

/**
 * @brief 发送一个字符到UART
 * @param ch 字符
 */
void uart_putc(char ch);

/**
 * @brief 从UART接收一个字符
 * @return 接收的字符
 */
char uart_getc(void);

/**
 * @brief 发送字符串到UART
 * @param str 字符串指针
 */
void uart_puts(const char *str);

#ifdef __cplusplus
}
#endif

#endif /* UART_H */
