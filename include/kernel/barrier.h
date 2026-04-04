/**
 * @file    barrier.h
 * @brief   ARMv8-A 内存屏障与原子操作
 * @author  AISafe64 Team
 * @date    2026-03-31
 * @version 2.0
 *
 * @details 微内核 RTOS 内存屏障与原子操作原语
 *          - 数据内存屏障（DMB）：确保内存访问顺序
 *          - 指令同步屏障（ISB）：刷新指令流水线
 *          - 事件等待/发送（WFE/SEV/SEVL）：低功耗自旋优化
 *          - 32位/64位原子 CAS、inc、dec、add、sub、exchange
 *          - 原子 load-acquire / store-release 内存序操作
 *          - 使用 ARMv8-A LDXR/STXR 独占访问指令实现
 *
 * @note    MISRA-C:2012 合规
 * @note    仅适用于 ARMv8-A (AArch64) 架构
 * @warning 原子操作函数参数不得为 NULL
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_BARRIER_H
#define KERNEL_BARRIER_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * 内存屏障宏定义
 * ============================================================================ */

/**
 * @brief 数据内存屏障（Data Memory Barrier）
 *
 * @details 通用 DMB 宏，接受 ARMv8-A 屏障选项参数
 *          常用选项：
 *          - sy:   完全系统屏障（最强）
 *          - ish:  内部可共享域（Inner Shareable）
 *          - ishst: 内部可共享域仅存储
 *          - ishld: 内部可共享域仅加载
 *          - nsh:  非可共享域
 *          - osh:  外部可共享域
 *
 * @param option 屏障选项（不带引号的 ARM 汇编选项名）
 */
#define ARM64_DMB(option)   __asm__ volatile("dmb " #option ::: "memory")

/**
 * @brief 完全系统数据内存屏障
 *
 * @details 最强的 DMB 屏障，确保此前所有内存访问在屏障后的访问之前完成
 *          等效于 ARM64_DMB(sy)
 */
#define barrier()           ARM64_DMB(sy)

/**
 * @brief 内部可共享域仅存储屏障
 *
 * @details 确保此前的存储操作在后续存储操作之前可观测
 *          适用于释放语义（release semantics）
 */
#define barrier_store()     ARM64_DMB(ishst)

/**
 * @brief 内部可共享域仅加载屏障
 *
 * @details 确保此前的加载操作在后续加载操作之前完成
 *          适用于获取语义（acquire semantics）
 */
#define barrier_load()      ARM64_DMB(ishld)

/**
 * @brief 指令同步屏障
 *
 * @details 刷新处理器流水线，确保此前的上下文修改（如页表、系统寄存器）
 *          对后续指令可见
 */
#define barrier_inst()      __asm__ volatile("isb" ::: "memory")

/**
 * @brief 完整内存屏障
 *
 * @details 组合 DMB + ISB，提供最强的内存和指令同步保证
 *          用于系统寄存器写入、上下文切换等关键路径
 */
#define full_barrier()                                          \
    do                                                          \
    {                                                           \
        __asm__ volatile("dmb sy" ::: "memory");                \
        __asm__ volatile("isb" ::: "memory");                   \
    } while (0)

/**
 * @brief CPU 退让提示
 *
 * @details 发出 YIELD 指令，提示处理器当前处于自旋等待状态
 *          超线程处理器可借此让出执行资源给其他硬件线程
 *          应在 CAS 循环失败路径中调用以降低功耗和总线压力
 */
#define cpu_relax()        __asm__ volatile("yield" ::: "memory")

/**
 * @brief 等待事件（Wait For Event）
 *
 * @details 将当前核心置于低功耗状态，直到收到事件通知
 *          可由 SEV/SEVL 或中断唤醒，常用于自旋锁优化
 */
#define WFE()              __asm__ volatile("wfe" ::: "memory")

/**
 * @brief 发送事件（Send Event）
 *
 * @details 向所有核心广播事件信号，唤醒正在 WFE 等待的核心
 */
#define SEV()              __asm__ volatile("sev" ::: "memory")

/**
 * @brief 发送本地事件（Send Event Local）
 *
 * @details 仅向当前核心发送事件信号，用于自等待场景
 *          典型用法：SEVL; WFE 组合确保首次不丢失事件
 */
#define SEVL()             __asm__ volatile("sevl" ::: "memory")

/**
 * @brief 编译器内存屏障
 *
 * @details 阻止编译器将此前的内存访问重排到此屏障之后（反之亦然）
 *          不产生任何硬件指令，仅影响编译器优化
 */
#define COMPILER_BARRIER() __asm__ volatile("" ::: "memory")

/* ============================================================================
 * 32位原子操作（使用 LDXR/STXR 独占访问）
 * ============================================================================ */

/**
 * @brief 32位原子比较并交换（Compare-And-Swap）
 *
 * @details 如果 *addr 的当前值等于 expected，则将其替换为 desired
 *          使用 LDXR/STXR 独占访问指令实现，保证原子性
 *
 * @param addr     目标地址指针（不得为 NULL）
 * @param expected 期望的当前值
 * @param desired  要写入的新值
 *
 * @return 成功交换返回 true，否则返回 false
 */
static inline bool atomic_cas_u32(volatile uint32_t *addr,
                                  uint32_t expected,
                                  uint32_t desired)
{
    uint32_t old_val;
    uint32_t result;

    __asm__ volatile(
        "1: ldxr %w0, [%2]\n"
        "   cmp  %w0, %w3\n"
        "   b.ne 2f\n"
        "   stxr %w1, %w4, [%2]\n"
        "   cbnz %w1, 1b\n"
        "2:\n"
        : "=&r"(old_val), "=&r"(result)
        : "r"(addr), "r"(expected), "r"(desired)
        : "cc", "memory"
    );

    return (old_val == expected);
}

/**
 * @brief 32位原子递增（返回旧值）
 *
 * @details 原子地将 *addr 的值加 1 并返回操作前的旧值
 *
 * @param addr 目标地址指针（不得为 NULL）
 *
 * @return 操作前的旧值
 */
static inline uint32_t atomic_inc_u32(volatile uint32_t *addr)
{
    uint32_t old_val;
    uint32_t new_val;
    uint32_t result;

    do
    {
        __asm__ volatile("ldxr %w0, [%1]"
                         : "=&r"(old_val)
                         : "r"(addr)
                         : "memory");
        new_val = old_val + 1U;
        __asm__ volatile("stxr %w0, %w1, [%2]"
                         : "=&r"(result)
                         : "r"(new_val), "r"(addr)
                         : "memory");
        if (result != 0U)
        {
            cpu_relax();
        }
    } while (result != 0U);

    return old_val;
}

/**
 * @brief 32位原子递减（返回旧值）
 *
 * @details 原子地将 *addr 的值减 1 并返回操作前的旧值
 *
 * @param addr 目标地址指针（不得为 NULL）
 *
 * @return 操作前的旧值
 */
static inline uint32_t atomic_dec_u32(volatile uint32_t *addr)
{
    uint32_t old_val;
    uint32_t new_val;
    uint32_t result;

    do
    {
        __asm__ volatile("ldxr %w0, [%1]"
                         : "=&r"(old_val)
                         : "r"(addr)
                         : "memory");
        new_val = old_val - 1U;
        __asm__ volatile("stxr %w0, %w1, [%2]"
                         : "=&r"(result)
                         : "r"(new_val), "r"(addr)
                         : "memory");
        if (result != 0U)
        {
            cpu_relax();
        }
    } while (result != 0U);

    return old_val;
}

/**
 * @brief 32位原子加法（返回旧值）
 *
 * @details 原子地将 *addr 的值加上 value 并返回操作前的旧值
 *
 * @param addr  目标地址指针（不得为 NULL）
 * @param value 要加的值
 *
 * @return 操作前的旧值
 */
static inline uint32_t atomic_add_u32(volatile uint32_t *addr, uint32_t value)
{
    uint32_t old_val;
    uint32_t new_val;
    uint32_t result;

    do
    {
        __asm__ volatile("ldxr %w0, [%1]"
                         : "=&r"(old_val)
                         : "r"(addr)
                         : "memory");
        new_val = old_val + value;
        __asm__ volatile("stxr %w0, %w1, [%2]"
                         : "=&r"(result)
                         : "r"(new_val), "r"(addr)
                         : "memory");
        if (result != 0U)
        {
            cpu_relax();
        }
    } while (result != 0U);

    return old_val;
}

/**
 * @brief 32位原子减法（返回旧值）
 *
 * @details 原子地将 *addr 的值减去 value 并返回操作前的旧值
 *
 * @param addr  目标地址指针（不得为 NULL）
 * @param value 要减的值
 *
 * @return 操作前的旧值
 */
static inline uint32_t atomic_sub_u32(volatile uint32_t *addr, uint32_t value)
{
    uint32_t old_val;
    uint32_t new_val;
    uint32_t result;

    do
    {
        __asm__ volatile("ldxr %w0, [%1]"
                         : "=&r"(old_val)
                         : "r"(addr)
                         : "memory");
        new_val = old_val - value;
        __asm__ volatile("stxr %w0, %w1, [%2]"
                         : "=&r"(result)
                         : "r"(new_val), "r"(addr)
                         : "memory");
        if (result != 0U)
        {
            cpu_relax();
        }
    } while (result != 0U);

    return old_val;
}

/**
 * @brief 32位原子交换（返回旧值）
 *
 * @details 原子地将 value 写入 *addr 并返回操作前的旧值
 *
 * @param addr  目标地址指针（不得为 NULL）
 * @param value 要写入的新值
 *
 * @return 操作前的旧值
 */
static inline uint32_t atomic_xchg_u32(volatile uint32_t *addr, uint32_t value)
{
    uint32_t old_val;
    uint32_t result;

    do
    {
        __asm__ volatile("ldxr %w0, [%1]"
                         : "=&r"(old_val)
                         : "r"(addr)
                         : "memory");
        __asm__ volatile("stxr %w0, %w1, [%2]"
                         : "=&r"(result)
                         : "r"(value), "r"(addr)
                         : "memory");
        if (result != 0U)
        {
            cpu_relax();
        }
    } while (result != 0U);

    return old_val;
}

/**
 * @brief 32位原子 load-acquire（获取语义）
 *
 * @details 从 *addr 加载值，并插入获取屏障
 *          确保此加载之后的内存访问不会被重排到此加载之前
 *
 * @param addr 目标地址指针（不得为 NULL）
 *
 * @return 加载的值
 */
static inline uint32_t atomic_load_acquire_u32(const volatile uint32_t *addr)
{
    uint32_t value;

    value = *addr;
    barrier_load();

    return value;
}

/**
 * @brief 32位原子 store-release（释放语义）
 *
 * @details 将 value 写入 *addr，并插入释放屏障
 *          确保此存储之前的内存访问不会被重排到此存储之后
 *
 * @param addr  目标地址指针（不得为 NULL）
 * @param value 要写入的值
 */
static inline void atomic_store_release_u32(volatile uint32_t *addr, uint32_t value)
{
    barrier_store();
    *addr = value;
}

/* ============================================================================
 * 64位原子操作（使用 LDXR/STXR 独占访问）
 * ============================================================================ */

/**
 * @brief 64位原子比较并交换（Compare-And-Swap）
 *
 * @details 如果 *addr 的当前值等于 expected，则将其替换为 desired
 *          使用 LDXR/STXR 64位独占访问指令实现
 *
 * @param addr     目标地址指针（不得为 NULL）
 * @param expected 期望的当前值
 * @param desired  要写入的新值
 *
 * @return 成功交换返回 true，否则返回 false
 */
static inline bool atomic_cas_u64(volatile uint64_t *addr,
                                  uint64_t expected,
                                  uint64_t desired)
{
    uint64_t old_val;
    uint64_t result;

    __asm__ volatile(
        "1: ldxr %0, [%2]\n"
        "   cmp  %0, %3\n"
        "   b.ne 2f\n"
        "   stxr %w1, %4, [%2]\n"
        "   cbnz %w1, 1b\n"
        "2:\n"
        : "=&r"(old_val), "=&r"(result)
        : "r"(addr), "r"(expected), "r"(desired)
        : "cc", "memory"
    );

    return (old_val == expected);
}

/**
 * @brief 64位原子递增（返回旧值）
 *
 * @details 原子地将 *addr 的值加 1 并返回操作前的旧值
 *
 * @param addr 目标地址指针（不得为 NULL）
 *
 * @return 操作前的旧值
 */
static inline uint64_t atomic_inc_u64(volatile uint64_t *addr)
{
    uint64_t old_val;
    uint64_t new_val;
    uint64_t result;

    do
    {
        __asm__ volatile("ldxr %0, [%1]"
                         : "=&r"(old_val)
                         : "r"(addr)
                         : "memory");
        new_val = old_val + 1ULL;
        __asm__ volatile("stxr %w0, %1, [%2]"
                         : "=&r"(result)
                         : "r"(new_val), "r"(addr)
                         : "memory");
        if (result != 0U)
        {
            cpu_relax();
        }
    } while (result != 0U);

    return old_val;
}

/**
 * @brief 64位原子递减（返回旧值）
 *
 * @details 原子地将 *addr 的值减 1 并返回操作前的旧值
 *
 * @param addr 目标地址指针（不得为 NULL）
 *
 * @return 操作前的旧值
 */
static inline uint64_t atomic_dec_u64(volatile uint64_t *addr)
{
    uint64_t old_val;
    uint64_t new_val;
    uint64_t result;

    do
    {
        __asm__ volatile("ldxr %0, [%1]"
                         : "=&r"(old_val)
                         : "r"(addr)
                         : "memory");
        new_val = old_val - 1ULL;
        __asm__ volatile("stxr %w0, %1, [%2]"
                         : "=&r"(result)
                         : "r"(new_val), "r"(addr)
                         : "memory");
        if (result != 0U)
        {
            cpu_relax();
        }
    } while (result != 0U);

    return old_val;
}

/**
 * @brief 64位原子加法（返回旧值）
 *
 * @details 原子地将 *addr 的值加上 value 并返回操作前的旧值
 *
 * @param addr  目标地址指针（不得为 NULL）
 * @param value 要加的值
 *
 * @return 操作前的旧值
 */
static inline uint64_t atomic_add_u64(volatile uint64_t *addr, uint64_t value)
{
    uint64_t old_val;
    uint64_t new_val;
    uint64_t result;

    do
    {
        __asm__ volatile("ldxr %0, [%1]"
                         : "=&r"(old_val)
                         : "r"(addr)
                         : "memory");
        new_val = old_val + value;
        __asm__ volatile("stxr %w0, %1, [%2]"
                         : "=&r"(result)
                         : "r"(new_val), "r"(addr)
                         : "memory");
        if (result != 0U)
        {
            cpu_relax();
        }
    } while (result != 0U);

    return old_val;
}

/**
 * @brief 64位原子减法（返回旧值）
 *
 * @details 原子地将 *addr 的值减去 value 并返回操作前的旧值
 *
 * @param addr  目标地址指针（不得为 NULL）
 * @param value 要减的值
 *
 * @return 操作前的旧值
 */
static inline uint64_t atomic_sub_u64(volatile uint64_t *addr, uint64_t value)
{
    uint64_t old_val;
    uint64_t new_val;
    uint64_t result;

    do
    {
        __asm__ volatile("ldxr %0, [%1]"
                         : "=&r"(old_val)
                         : "r"(addr)
                         : "memory");
        new_val = old_val - value;
        __asm__ volatile("stxr %w0, %1, [%2]"
                         : "=&r"(result)
                         : "r"(new_val), "r"(addr)
                         : "memory");
        if (result != 0U)
        {
            cpu_relax();
        }
    } while (result != 0U);

    return old_val;
}

/**
 * @brief 64位原子交换（返回旧值）
 *
 * @details 原子地将 value 写入 *addr 并返回操作前的旧值
 *
 * @param addr  目标地址指针（不得为 NULL）
 * @param value 要写入的新值
 *
 * @return 操作前的旧值
 */
static inline uint64_t atomic_xchg_u64(volatile uint64_t *addr, uint64_t value)
{
    uint64_t old_val;
    uint64_t result;

    do
    {
        __asm__ volatile("ldxr %0, [%1]"
                         : "=&r"(old_val)
                         : "r"(addr)
                         : "memory");
        __asm__ volatile("stxr %w0, %1, [%2]"
                         : "=&r"(result)
                         : "r"(value), "r"(addr)
                         : "memory");
        if (result != 0U)
        {
            cpu_relax();
        }
    } while (result != 0U);

    return old_val;
}

/**
 * @brief 64位原子 load-acquire（获取语义）
 *
 * @details 从 *addr 加载值，并插入获取屏障
 *          确保此加载之后的内存访问不会被重排到此加载之前
 *
 * @param addr 目标地址指针（不得为 NULL）
 *
 * @return 加载的值
 */
static inline uint64_t atomic_load_acquire_u64(const volatile uint64_t *addr)
{
    uint64_t value;

    value = *addr;
    barrier_load();

    return value;
}

/**
 * @brief 64位原子 store-release（释放语义）
 *
 * @details 将 value 写入 *addr，并插入释放屏障
 *          确保此存储之前的内存访问不会被重排到此存储之后
 *
 * @param addr  目标地址指针（不得为 NULL）
 * @param value 要写入的值
 */
static inline void atomic_store_release_u64(volatile uint64_t *addr, uint64_t value)
{
    barrier_store();
    *addr = value;
}

#endif /* KERNEL_BARRIER_H */
