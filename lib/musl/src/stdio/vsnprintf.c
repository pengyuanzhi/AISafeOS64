/**
 * @file    vsnprintf.c
 * @brief   核心格式化引擎实现（va_list 版本）
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 实现可变参数格式化输出的核心引擎：
 *          - 支持格式符：%d, %u, %ld, %lu, %x, %X, %p, %s, %c, %%
 *          - 支持修饰符：宽度, 精度, 左对齐(-), 前导零(0)
 *          - 使用回调函数写入字符，支持缓冲区和 size 限制
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

/* ========================================================================
 * 内部类型定义
 * ======================================================================== */

/** @brief 字符输出回调函数类型 */
typedef void (*emit_fn)(char ch, void *ctx);

/** @brief snprintf 上下文 */
typedef struct
{
    char *buf;      /**< @brief 目标缓冲区 */
    size_t size;    /**< @brief 缓冲区大小 */
    size_t pos;     /**< @brief 当前写入位置 */
} snprintf_ctx_t;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief snprintf 字符输出回调
 * @param ch 要输出的字符
 * @param ctx 回调上下文（snprintf_ctx_t 指针）
 */
static void emit_to_buf(char ch, void *ctx)
{
    snprintf_ctx_t *s = (snprintf_ctx_t *)ctx;

    if (s->pos < s->size)
    {
        s->buf[s->pos] = ch;
    }
    s->pos++;
}

/**
 * @brief 输出填充字符
 * @param emit 字符输出回调
 * @param ctx 回调上下文
 * @param ch 填充字符
 * @param count 填充次数
 */
static void pad_output(emit_fn emit, void *ctx, char ch, int count)
{
    int i;
    for (i = 0; i < count; i++)
    {
        emit(ch, ctx);
    }
}

/**
 * @brief 输出无符号整数的字符串表示
 * @param emit 字符输出回调
 * @param ctx 回调上下文
 * @param num 无符号整数值
 * @param base 进制（8, 10, 16）
 * @param uppercase 十六进制是否使用大写字母
 * @return 输出的字符数
 */
static int emit_unsigned(emit_fn emit, void *ctx,
                         unsigned long num, int base, int uppercase)
{
    char digits[20];
    int len = 0;
    int i;
    const char *hex_lower = "0123456789abcdef";
    const char *hex_upper = "0123456789ABCDEF";
    const char *hex_ch = uppercase ? hex_upper : hex_lower;

    /* 特殊情况：值为 0 */
    if (num == 0UL)
    {
        emit('0', ctx);
        return 1;
    }

    /* 提取各位数字 */
    while (num > 0UL)
    {
        digits[len] = hex_ch[num % (unsigned long)base];
        num = num / (unsigned long)base;
        len++;
    }

    /* 逆序输出 */
    for (i = len - 1; i >= 0; i--)
    {
        emit(digits[i], ctx);
    }

    return len;
}

/* ========================================================================
 * 核心格式化引擎
 * ======================================================================== */

/**
 * @brief 核心格式化引擎（va_list 版本）
 * @param emit 字符输出回调函数
 * @param ctx 回调上下文
 * @param fmt 格式字符串
 * @param ap 可变参数列表
 * @return 总共写入的字符数（不含终止符）
 */
int vsnprintf_core(emit_fn emit, void *ctx, const char *fmt, va_list ap)
{
    int total = 0;

    while (*fmt != '\0')
    {
        if (*fmt != '%')
        {
            emit(*fmt, ctx);
            total++;
            fmt++;
            continue;
        }

        fmt++; /* 跳过 '%' */

        /* 解析标志 */
        int flag_left = 0;
        int flag_zero = 0;
        int flag_hash = 0;

        for (;;)
        {
            if (*fmt == '-')
            {
                flag_left = 1;
                fmt++;
            }
            else if (*fmt == '0')
            {
                flag_zero = 1;
                fmt++;
            }
            else if (*fmt == '#')
            {
                flag_hash = 1;
                fmt++;
            }
            else
            {
                break;
            }
        }

        /* 解析宽度 */
        int width = 0;
        while ((*fmt >= '0') && (*fmt <= '9'))
        {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* 解析精度 */
        int has_prec = 0;
        int prec = 0;
        if (*fmt == '.')
        {
            has_prec = 1;
            fmt++;
            prec = 0;
            while ((*fmt >= '0') && (*fmt <= '9'))
            {
                prec = prec * 10 + (*fmt - '0');
                fmt++;
            }
        }

        /* 解析长度修饰符 */
        int is_long = 0;
        if (*fmt == 'l')
        {
            is_long = 1;
            fmt++;
        }

        /* 解析格式符 */
        char spec = *fmt;
        if (spec == '\0')
        {
            break;
        }
        fmt++;

        /* 处理 %% */
        if (spec == '%')
        {
            emit('%', ctx);
            total++;
            continue;
        }

        /* 处理 %c */
        if (spec == 'c')
        {
            char ch = (char)va_arg(ap, int);
            if (!flag_left && (width > 1))
            {
                pad_output(emit, ctx, ' ', width - 1);
                total += width - 1;
            }
            emit(ch, ctx);
            total++;
            if (flag_left && (width > 1))
            {
                pad_output(emit, ctx, ' ', width - 1);
                total += width - 1;
            }
            continue;
        }

        /* 处理 %s */
        if (spec == 's')
        {
            const char *str = va_arg(ap, const char *);
            if (str == NULL)
            {
                str = "(null)";
            }

            int slen = (int)strlen(str);
            if (has_prec && (prec < slen))
            {
                slen = prec;
            }

            int pad = (width > slen) ? (width - slen) : 0;

            if (!flag_left)
            {
                pad_output(emit, ctx, ' ', pad);
                total += pad;
            }

            /* 输出字符串内容 */
            int idx;
            for (idx = 0; idx < slen; idx++)
            {
                emit(str[idx], ctx);
            }
            total += slen;

            if (flag_left)
            {
                pad_output(emit, ctx, ' ', pad);
                total += pad;
            }
            continue;
        }

        /* 以下为数值格式：d, u, x, X, p */

        unsigned long uval = 0UL;
        long sval = 0L;
        int is_negative = 0;
        int is_signed = 0;
        int base = 10;
        int upper = 0;
        int is_ptr = 0;

        if (spec == 'p')
        {
            /* %p：输出指针值 */
            uval = (unsigned long)(uintptr_t)va_arg(ap, void *);
            is_ptr = 1;
            base = 16;
            upper = 0;
        }
        else if (spec == 'd')
        {
            is_signed = 1;
            if (is_long != 0)
            {
                sval = va_arg(ap, long);
            }
            else
            {
                sval = (long)va_arg(ap, int);
            }

            if (sval < 0L)
            {
                is_negative = 1;
                uval = (unsigned long)(-sval);
            }
            else
            {
                uval = (unsigned long)sval;
            }
            base = 10;
        }
        else if (spec == 'u')
        {
            if (is_long != 0)
            {
                uval = va_arg(ap, unsigned long);
            }
            else
            {
                uval = (unsigned long)va_arg(ap, unsigned int);
            }
            base = 10;
        }
        else if ((spec == 'x') || (spec == 'X'))
        {
            if (is_long != 0)
            {
                uval = va_arg(ap, unsigned long);
            }
            else
            {
                uval = (unsigned long)va_arg(ap, unsigned int);
            }
            base = 16;
            upper = (spec == 'X') ? 1 : 0;
        }
        else
        {
            /* 未知格式符，原样输出 */
            emit('%', ctx);
            emit(spec, ctx);
            total += 2;
            continue;
        }

        /* 计算数字部分的字符数 */
        /* 先将数值转成临时字符串来计算长度 */
        char num_buf[20];
        int num_len = 0;

        if (uval == 0UL)
        {
            /* 精度为 0 时，值为 0 不输出数字 */
            if (has_prec && (prec == 0))
            {
                num_len = 0;
            }
            else
            {
                num_buf[0] = '0';
                num_len = 1;
            }
        }
        else
        {
            unsigned long tmp = uval;
            while (tmp > 0UL)
            {
                num_buf[num_len] = '0' + (char)(tmp % (unsigned long)base);
                tmp = tmp / (unsigned long)base;
                num_len++;
            }
        }

        /* 前缀 */
        int prefix_len = 0;
        char prefix[3];
        if (is_negative)
        {
            prefix[prefix_len] = '-';
            prefix_len++;
        }
        if (is_ptr)
        {
            prefix[0] = '0';
            prefix[1] = 'x';
            prefix_len = 2;
        }
        if (flag_hash && (base == 16) && (uval != 0UL) && !is_ptr)
        {
            prefix[0] = '0';
            prefix[1] = (upper != 0) ? 'X' : 'x';
            prefix_len = 2;
        }

        /* 精度导致的零填充（仅十进制） */
        int zero_pad = 0;
        if (has_prec && (prec > num_len))
        {
            zero_pad = prec - num_len;
        }

        /* 总数字段宽度 */
        int field_len = prefix_len + zero_pad + num_len;

        /* 宽度填充 */
        int pad_count = (width > field_len) ? (width - field_len) : 0;

        /* 前导零（仅当无精度且无左对齐时） */
        int leading_zero = 0;
        if (!has_prec && flag_zero && !flag_left)
        {
            leading_zero = pad_count;
            pad_count = 0;
        }

        /* 右对齐空格填充 */
        if (!flag_left)
        {
            pad_output(emit, ctx, ' ', pad_count);
            total += pad_count;
        }

        /* 输出前缀 */
        int pi;
        for (pi = 0; pi < prefix_len; pi++)
        {
            emit(prefix[pi], ctx);
        }
        total += prefix_len;

        /* 前导零 */
        pad_output(emit, ctx, '0', leading_zero);
        total += leading_zero;

        /* 精度零填充 */
        pad_output(emit, ctx, '0', zero_pad);
        total += zero_pad;

        /* 输出数字（需要正确处理十六进制字母） */
        if (uval == 0UL)
        {
            if (num_len > 0)
            {
                emit('0', ctx);
                total++;
            }
        }
        else
        {
            const char *hex_ch = (upper != 0) ? "0123456789ABCDEF" : "0123456789abcdef";
            int ni;
            for (ni = num_len - 1; ni >= 0; ni--)
            {
                /* 重新计算正确字符 */
                unsigned long digit_val = uval;
                int k;
                for (k = 0; k < ni; k++)
                {
                    digit_val = digit_val / (unsigned long)base;
                }
                digit_val = digit_val % (unsigned long)base;
                emit(hex_ch[digit_val], ctx);
            }
            total += num_len;
        }

        /* 左对齐空格填充 */
        if (flag_left)
        {
            pad_output(emit, ctx, ' ', pad_count);
            total += pad_count;
        }
    }

    return total;
}

/* ========================================================================
 * vsnprintf 公共接口
 * ======================================================================== */

/**
 * @brief 格式化字符串到缓冲区（va_list 版本，带长度限制）
 * @param buf 目标缓冲区
 * @param size 缓冲区大小
 * @param fmt 格式字符串
 * @param ap 可变参数列表
 * @return 格式化后的字符串长度（不含终止符）
 */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    snprintf_ctx_t ctx;

    ctx.buf = buf;
    ctx.size = (size > 0U) ? (size - 1U) : 0U;
    ctx.pos = 0;

    int ret = vsnprintf_core(emit_to_buf, &ctx, fmt, ap);

    /* 终止符 */
    if (size > 0U)
    {
        size_t term_pos = (ctx.pos < size) ? ctx.pos : (size - 1U);
        buf[term_pos] = '\0';
    }

    return ret;
}
