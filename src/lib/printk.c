/**
 * @file printk.c
 * @brief AISafe64 RTOS - 内核打印函数
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 内核日志输出函数
 *          - 支持格式化输出
 *          - 通过UART输出
 *          - 用于早期调试
 *
 * @note MISRA-C:2012合规
 * @note 后续扩展支持日志级别和多个输出目标
 */

#include "printk.h"
#include "uart.h"
#include "types.h"

/**
 * @brief 内核打印初始化
 * @return 成功返回0，失败返回负错误码
 *
 * @details 初始化UART驱动
 */
int printk_init(void)
{
    extern int uart_init(void);
    return uart_init();
}

/**
 * @brief 内核格式化打印函数
 * @param fmt 格式化字符串
 *
 * @details 支持格式：
 *          - %c: 字符
 *          - %s: 字符串
 *          - %d: 有符号十进制整数
 *          - %u: 无符号十进制整数
 *          - %x: 无符号十六进制整数（小写）
 *          - %p: 指针
 *          - %%: 百分号
 *
 * @note 简化实现，不支持浮点和字段宽度
 */
void printk(const char *fmt, ...)
{
    va_list args;
    const char *p;
    bool fmt_spec;
    char ch;
    int32_t val;
    uint32_t uval;
    char buffer[16];
    uint32_t i;
    uint32_t div;
    char hex_char;

    va_start(args, fmt);

    p = fmt;
    fmt_spec = false;

    while (*p != '\0') {
        if (*p == '%') {
            p++;
            if (*p == '\0') {
                break; /* 格式字符串以%结尾 */
            }

            switch (*p) {
                case 'c': /* 字符 */
                    ch = (char)va_arg(args, int32_t);
                    uart_putc(ch);
                    break;

                case 's': /* 字符串 */
                {
                    const char *str = va_arg(args, const char *);
                    if (str == NULL) {
                        uart_puts("(null)");
                    } else {
                        uart_puts(str);
                    }
                } break;

                case 'd': /* 有符号十进制整数 */
                    val = va_arg(args, int32_t);
                    if (val < 0) {
                        uart_putc('-');
                        val = -val;
                    }
                    /* 转换为字符串 */
                    i = 0U;
                    do {
                        buffer[i++] = (char)('0' + (val % 10));
                        val /= 10;
                    } while (val > 0);
                    /* 反向输出 */
                    while (i > 0U) {
                        uart_putc(buffer[--i]);
                    }
                    break;

                case 'u': /* 无符号十进制整数 */
                    uval = va_arg(args, uint32_t);
                    /* 转换为字符串 */
                    i = 0U;
                    do {
                        buffer[i++] = (char)('0' + (uval % 10U));
                        uval /= 10U;
                    } while (uval > 0U);
                    /* 反向输出 */
                    while (i > 0U) {
                        uart_putc(buffer[--i]);
                    }
                    break;

                case 'x': /* 无符号十六进制整数（小写） */
                    uval = va_arg(args, uint32_t);
                    /* 转换为字符串 */
                    i = 0U;
                    if (uval == 0U) {
                        uart_putc('0');
                    } else {
                        while (uval > 0U) {
                            uint32_t nibble = uval & 0xFU;
                            if (nibble < 10U) {
                                hex_char = (char)('0' + nibble);
                            } else {
                                hex_char = (char)('a' + (nibble - 10U));
                            }
                            buffer[i++] = hex_char;
                            uval >>= 4U;
                        }
                        /* 反向输出 */
                        while (i > 0U) {
                            uart_putc(buffer[--i]);
                        }
                    }
                    uart_puts("0x");
                    break;

                case 'p': /* 指针 */
                    uval = (uint32_t)va_arg(args, void *);
                    uart_puts("0x");
                    /* 转换为字符串（8位十六进制） */
                    for (i = 0U; i < 8U; i++) {
                        uint32_t shift = 28U - (i * 4U);
                        uint32_t nibble = (uval >> shift) & 0xFU;
                        if (nibble < 10U) {
                            hex_char = (char)('0' + nibble);
                        } else {
                            hex_char = (char)('a' + (nibble - 10U));
                        }
                        uart_putc(hex_char);
                    }
                    break;

                case '%': /* 百分号 */
                    uart_putc('%');
                    break;

                default: /* 未知格式 */
                    uart_putc('%');
                    uart_putc(*p);
                    break;
            }

            p++;
        } else {
            uart_putc(*p);
            p++;
        }
    }

    va_end(args);
}
