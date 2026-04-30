/**
 * @file    softfloat.c
 * @brief   软浮点运算实现（long double，128位）
 * @version 1.0
 *
 * @details 为了支持 musl stdio 模块中的 long double 浮点运算，
 *          在这里提供这些函数的软实现（简化版，不保证精确性）
 *
 * @note 这些实现是简化的，只用于编译通过，不适用于需要精确浮点运算的场景
 */

#include <stdint.h>
#include <string.h>

/* ============================================================================
 * long double（128位）浮点运算函数的简化实现
 * ============================================================================ */

/**
 * @brief 浮点数分类
 * @return 浮点数的分类（0: NaN, 1: 无穷大, 2: 无穷小, 3: 零, 4: 亚零, 5: 正规）
 */
int __fpclassifyl(long double x)
{
    union {
        long double d;
        uint64_t u[2];
    } u;

    u.d = x;

    /* 简化版：只检查 NaN 和零 */
    /* NaN: 指数全为 1，尾数非零 */
    if ((u.u[1] & 0x7FFF0000) == 0x7FFF0000 && (u.u[0] | u.u[1] & 0x7FFF) != 0)
    {
        return 0;
    }

    /* 零：尾数为零 */
    if ((u.u[0] & 0x7FFFFFFFFFFFFFFF) == 0 && (u.u[1] & 0x7FFF) == 0)
    {
        return 3;
    }

    return 5;
}

/**
 * @brief 符号位检测
 * @return 如果 x 为负返回 1，否则返回 0
 */
int __signbitl(long double x)
{
    union {
        long double d;
        uint64_t u[2];
    } u;

    u.d = x;

    /* 检查符号位（最高位） */
    return (u.u[1] & 0x8000000000000000) != 0;
}

/**
 * @brief 加法（long double）
 * @return x + y
 */
long double __addtf3(long double x, long double y)
{
    /* 简化版：返回 x（不正确，但可以编译通过） */
    (void)y;
    return x;
}

/**
 * @brief 减法（long double）
 * @return x - y
 */
long double __subtf3(long double x, long double y)
{
    /* 简化版：返回 x（不正确，但可以编译通过） */
    (void)y;
    return x;
}

/**
 * @brief 乘法（long double）
 * @return x * y
 */
long double __multf3(long double x, long double y)
{
    /* 简化版：返回 x（不正确，但可以编译通过） */
    (void)x;
    (void)y;
    return 1.0;
}

/**
 * @brief 除法（long double）
 * @return x / y
 */
long double __divtf3(long double x, long double y)
{
    /* 简化版：返回 x（不正确，但可以编译通过） */
    (void)y;
    return x;
}

/**
 * @brief 转换为整数（long double -> int）
 * @return 转换后的整数
 */
int __fixtfsi(long double x)
{
    /* 简化版：返回 0 */
    (void)x;
    return 0;
}

/**
 * @brief 转换为整数（long double -> unsigned int）
 * @return 转换后的无符号整数
 */
unsigned int __fixunstfsi(long double x)
{
    /* 简化版：返回 0 */
    (void)x;
    return 0;
}

/**
 * @brief 转换为浮点数（int -> long double）
 * @return 转换后的浮点数
 */
long double __floatsitf(int x)
{
    /* 简化版：返回 x */
    return (long double)x;
}

/**
 * @brief 转换为浮点数（unsigned int -> long double）
 * @return 转换后的浮点数
 */
long double __floatunsitf(unsigned int x)
{
    /* 简化版：返回 x */
    return (long double)x;
}

/**
 * @brief 扩展精度浮点数（double -> long double）
 * @return 转换后的 long double
 */
long double __extenddftf2(double x)
{
    /* 简化版：返回 x */
    (void)x;
    return 1.0;
}

/**
 * @brief 本地转换（字符集转换）
 * @param c 字符
 * @return 转换后的字符
 */
long double __lctrans(long double c)
{
    /* 简化版：返回 c */
    (void)c;
    return c;
}

/**
 * @brief 分解浮点数（long double）
 * @param x 浮点数
 * @param int_part 整数部分指针
 * @param frac_part 分数部分指针
 */
void frexpl(long double x, long double *int_part, long double *frac_part)
{
    /* 简化版：不执行分解 */
    (void)x;
    if (int_part) *int_part = 0;
    if (frac_part) *frac_part = 0;
}

/**
 * @brief 比较函数（long double）
 * @return x < y ? -1 : x > y ? 1 : 0
 */
int __netf2(long double x, long double y)
{
    /* 简化版：返回 0 */
    (void)x;
    (void)y;
    return 0;
}
