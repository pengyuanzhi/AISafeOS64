/**
 * @file barrier.h
 * @brief AISafe64 RTOS - ARMv8-A 内存屏障和原子操作
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details ARMv8-A架构特定的内存屏障和同步原语
 *          - 数据内存屏障（DMB）
 *          - 数据同步屏障（DSB）
 *          - 指令同步屏障（ISB）
 *          - 事件等待和发送（WFE/SEV/SEVL）
 *          - 独占加载/存储（LDXR/STXR）
 *          - 完整的32位和64位原子操作库
 *
 * @note MISRA-C:2012合规
 * @note 仅适用于ARMv8-A架构
 *
 * @section atomic_ops 原子操作分类
 * @subsection 32bit_ops 32位原子操作
 *   - CAS操作：atomic_cas_u32, atomic_compare_exchange_strong, atomic_cmpxchg
 *   - 读写操作：atomic_read_u32, atomic_write_u32
 *   - 算术操作：atomic_inc/dec/add/sub_u32, atomic_fetch_*_u32, atomic_*_fetch_u32
 *   - 位操作：atomic_and/or/xor_u32
 *   - 交换操作：atomic_xchg_u32
 *
 * @subsection 64bit_ops 64位原子操作
 *   - CAS操作：atomic_cas_u64
 *   - 读写操作：atomic_read_u64, atomic_write_u64
 *   - 算术操作：atomic_inc/dec/add/sub_u64, atomic_*_fetch_u64
 *   - 交换操作：atomic_xchg_u64
 *
 * @subsection bit_ops 位测试操作
 *   - atomic_test_and_set_u32/u64
 *   - atomic_test_and_clear_u32/u64
 *   - atomic_test_and_toggle_u32
 *
 * @subsection flag_ops 标志操作
 *   - atomic_acquire_lock/release_lock
 *   - atomic_set_flag/clear_flag
 *
 * @subsection memory_order 内存序操作
 *   - atomic_load_*_relaxed/acquire_u32
 *   - atomic_store_*_relaxed/release_u32
 *   - atomic_fetch_*_acquire/release_u32
 */

#ifndef BARRIER_H
#define BARRIER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 数据内存屏障（Data Memory Barrier）
 * @details 确保之前的内存访问完成
 *
 * @param option 屏障选项：
 *   - #sy: 完全系统屏障
 *   - #sh: 内部可共享
 *   - #st: 商店-商店
 *   - #ld: 加载-加载
 *   - #ldst: 加载-商店
 */
#define ARM64_DMB(option) __asm__ volatile("dmb " #option ::: "memory")

/**
 * @brief 完全系统数据内存屏障
 * @details 最强的内存屏障，确保所有内存访问完成
 */
#define MEMORY_BARRIER()      ARM64_DMB(sy)

/**
 * @brief 数据同步屏障（Data Synchronization Barrier）
 * @details 确保之前的内存访问和缓存操作完成
 */
#define DATA_SYNC_BARRIER()   __asm__ volatile("dsb sy" ::: "memory")

/**
 * @brief 指令同步屏障（Instruction Synchronization Barrier）
 * @details 刷新指令流水线
 */
#define INSTRUCTION_SYNC_BARRIER() __asm__ volatile("isb" ::: "memory")

/**
 * @brief 等待事件（Wait For Event）
 * @details 进入低功耗状态，直到收到事件
 */
#define WFE() __asm__ volatile("wfe")

/**
 * @brief 发送事件（Send Event）
 * @details 唤醒所有WFE等待的核心
 */
#define SEV() __asm__ volatile("sev")

/**
 * @brief 发送事件本地（Send Event Local）
 * @details 仅唤醒当前核心的WFE等待
 */
#define SEVL() __asm__ volatile("sevl")

/**
 * @brief 编译器屏障
 * @details 防止编译器重排内存访问，但不生成硬件指令
 */
#define COMPILER_BARRIER() __asm__ volatile("" ::: "memory")

/**
 * @brief 独占加载（Load-Exclusive Register）
 * @details 原子操作的第一步，加载并标记地址为独占访问
 *
 * @param dest 目标变量
 * @param addr 地址指针
 */
#define LDXR(dest, addr) \
    __asm__ volatile( \
        "ldxr %w0, [%1]" \
        : "=r"(dest) \
        : "r"(addr) \
        : "memory" \
    )

/**
 * @brief 独占存储（Store-Exclusive Register）
 * @details 原子操作的第二步，仅当地址仍被标记时才存储
 *
 * @param result 结果（0表示成功，非0表示失败）
 * @param src 源数据
 * @param addr 地址指针
 */
#define STXR(result, src, addr) \
    __asm__ volatile( \
        "stxr %w0, %w1, [%2]" \
        : "=&r"(result) \
        : "r"(src), "r"(addr) \
        : "memory" \
    )

/**
 * @brief 独占加载对（Load-Exclusive Pair of Registers）
 * @details 原子加载两个连续的寄存器
 *
 * @param dest0 第一个目标变量
 * @param dest1 第二个目标变量
 * @param addr 地址指针
 */
#define LDXP(dest0, dest1, addr) \
    __asm__ volatile( \
        "ldxp %w0, %w1, [%2]" \
        : "=r"(dest0), "=r"(dest1) \
        : "r"(addr) \
        : "memory" \
    )

/**
 * @brief 独占存储对（Store-Exclusive Pair of Registers）
 * @details 原子存储两个连续的寄存器
 *
 * @param result 结果（0表示成功，非0表示失败）
 * @param src0 第一个源数据
 * @param src1 第二个源数据
 * @param addr 地址指针
 */
#define STXP(result, src0, src1, addr) \
    __asm__ volatile( \
        "stxp %w0, %w1, %w2, [%3]" \
        : "=&r"(result) \
        : "r"(src0), "r"(src1), "r"(addr) \
        : "memory" \
    )

/**
 * @brief 原子比较并交换（Compare-And-Swap）
 * @details 如果*addr == expected，则将desired写入*addr
 *
 * @param addr 地址指针
 * @param expected 期望值
 * @param desired 新值
 * @return 成功返回true，失败返回false
 */
static inline bool atomic_cas_u32(volatile uint32_t *addr,
                                  uint32_t expected,
                                  uint32_t desired)
{
    uint32_t old_val;
    uint32_t result;

    __asm__ volatile(
        "ldxr %w0, [%2]\n"
        "cmp %w0, %w3\n"
        "b.ne 1f\n"
        "stxr %w1, %w4, [%2]\n"
        "1:"
        : "=&r"(old_val), "=&r"(result)
        : "r"(addr), "r"(expected), "r"(desired)
        : "cc", "memory"
    );

    return (old_val == expected) && (result == 0U);
}

/**
 * @brief 原子比较并交换（C11风格接口）
 * @details 如果*addr == expected，则将desired写入*addr
 *
 * @param addr 地址指针
 * @param expected 期望值的指针
 * @param desired 新值
 * @return 成功返回true，失败返回false
 *
 * @note 失败时，expected会被更新为实际读取的值
 */
static inline bool atomic_compare_exchange_strong(volatile uint32_t *addr,
                                                  uint32_t *expected,
                                                  uint32_t desired)
{
    uint32_t old_val;
    uint32_t result;

    __asm__ volatile(
        "ldxr %w0, [%2]\n"
        "cmp %w0, %w3\n"
        "b.ne 1f\n"
        "stxr %w1, %w4, [%2]\n"
        "1:"
        : "=&r"(old_val), "=&r"(result)
        : "r"(addr), "r"(*expected), "r"(desired)
        : "cc", "memory"
    );

    if (old_val != *expected)
    {
        *expected = old_val;
        return false;
    }

    return (result == 0U);
}

/**
 * @brief 原子比较并交换（简化接口）
 * @details 如果*addr == expected，则将desired写入*addr
 *
 * @param addr 地址指针
 * @param expected 期望值
 * @param desired 新值
 * @return 成功返回true，失败返回false
 */
static inline bool atomic_cmpxchg(volatile uint32_t *addr,
                                  uint32_t expected,
                                  uint32_t desired)
{
    return atomic_cas_u32(addr, expected, desired);
}

/**
 * @brief 原子读取
 * @details 带内存屏障的读取操作
 *
 * @param addr 地址指针
 * @return 读取的值
 */
static inline uint32_t atomic_read_u32(const volatile uint32_t *addr)
{
    uint32_t value;
    LDXR(value, addr);
    MEMORY_BARRIER();
    return value;
}

/**
 * @brief 原子写入
 * @details 带内存屏障的写入操作
 *
 * @param addr 地址指针
 * @param value 要写入的值
 */
static inline void atomic_write_u32(volatile uint32_t *addr, uint32_t value)
{
    MEMORY_BARRIER();
    *addr = value;
    MEMORY_BARRIER();
}

/**
 * @brief 原子递增
 * @details 原子地将值加1
 *
 * @param addr 地址指针
 * @return 旧值
 */
static inline uint32_t atomic_inc_u32(volatile uint32_t *addr)
{
    uint32_t old_val;
    uint32_t new_val;
    uint32_t result;

    do
    {
        LDXR(old_val, addr);
        new_val = old_val + 1U;
        STXR(result, new_val, addr);
    }
    while (result != 0U);

    return old_val;
}

/**
 * @brief 原子递增（Fetch-Increment）
 * @details 原子地递增并返回旧值（标准C11风格的接口）
 *
 * @param addr 地址指针
 * @return 操作前的旧值
 */
static inline uint32_t atomic_fetch_inc(volatile uint32_t *addr)
{
    return atomic_inc_u32(addr);
}

/**
 * @brief 原子递减
 * @details 原子地将值减1
 *
 * @param addr 地址指针
 * @return 旧值
 */
static inline uint32_t atomic_dec_u32(volatile uint32_t *addr)
{
    uint32_t old_val;
    uint32_t new_val;
    uint32_t result;

    do
    {
        LDXR(old_val, addr);
        new_val = old_val - 1U;
        STXR(result, new_val, addr);
    }
    while (result != 0U);

    return old_val;
}

/**
 * @brief 原子加
 * @details 原子地将值加上指定数
 *
 * @param addr 地址指针
 * @param value 要加的值
 * @return 旧值
 */
static inline uint32_t atomic_add_u32(volatile uint32_t *addr, uint32_t value)
{
    uint32_t old_val;
    uint32_t new_val;
    uint32_t result;

    do
    {
        LDXR(old_val, addr);
        new_val = old_val + value;
        STXR(result, new_val, addr);
    }
    while (result != 0U);

    return old_val;
}

/**
 * @brief 原子取加（Fetch-and-Add）
 * @details 原子地加并返回旧值（标准C11风格的接口）
 *
 * @param addr 地址指针
 * @param value 要加的值
 * @return 操作前的旧值
 */
static inline uint32_t atomic_fetch_add(volatile uint32_t *addr, uint32_t value)
{
    return atomic_add_u32(addr, value);
}

/**
 * @brief 原子减
 * @details 原子地将值减去指定数
 *
 * @param addr 地址指针
 * @param value 要减的值
 * @return 旧值
 */
static inline uint32_t atomic_sub_u32(volatile uint32_t *addr, uint32_t value)
{
    uint32_t old_val;
    uint32_t new_val;
    uint32_t result;

    do
    {
        LDXR(old_val, addr);
        new_val = old_val - value;
        STXR(result, new_val, addr);
    }
    while (result != 0U);

    return old_val;
}

/**
 * @brief 原子按位与
 * @details 原子地对值进行按位与操作
 *
 * @param addr 地址指针
 * @param value 要与的值
 * @return 旧值
 */
static inline uint32_t atomic_and_u32(volatile uint32_t *addr, uint32_t value)
{
    uint32_t old_val;
    uint32_t new_val;
    uint32_t result;

    do
    {
        LDXR(old_val, addr);
        new_val = old_val & value;
        STXR(result, new_val, addr);
    }
    while (result != 0U);

    return old_val;
}

/**
 * @brief 原子按位或
 * @details 原子地对值进行按位或操作
 *
 * @param addr 地址指针
 * @param value 要或的值
 * @return 旧值
 */
static inline uint32_t atomic_or_u32(volatile uint32_t *addr, uint32_t value)
{
    uint32_t old_val;
    uint32_t new_val;
    uint32_t result;

    do
    {
        LDXR(old_val, addr);
        new_val = old_val | value;
        STXR(result, new_val, addr);
    }
    while (result != 0U);

    return old_val;
}

/**
 * @brief 原子按位异或
 * @details 原子地对值进行按位异或操作
 *
 * @param addr 地址指针
 * @param value 要异或的值
 * @return 旧值
 */
static inline uint32_t atomic_xor_u32(volatile uint32_t *addr, uint32_t value)
{
    uint32_t old_val;
    uint32_t new_val;
    uint32_t result;

    do
    {
        LDXR(old_val, addr);
        new_val = old_val ^ value;
        STXR(result, new_val, addr);
    }
    while (result != 0U);

    return old_val;
}

/**
 * @brief 原子交换
 * @details 原子地将新值写入并返回旧值
 *
 * @param addr 地址指针
 * @param value 新值
 * @return 旧值
 */
static inline uint32_t atomic_xchg_u32(volatile uint32_t *addr, uint32_t value)
{
    uint32_t old_val;
    uint32_t result;

    do
    {
        LDXR(old_val, addr);
        STXR(result, value, addr);
    }
    while (result != 0U);

    return old_val;
}

/* ============================================================================
 * 64位原子操作
 * ============================================================================ */

/**
 * @brief 64位原子比较并交换
 * @details 如果*addr == expected，则将desired写入*addr
 *
 * @param addr 地址指针
 * @param expected 期望值
 * @param desired 新值
 * @return 成功返回true，失败返回false
 */
static inline bool atomic_cas_u64(volatile uint64_t *addr,
                                  uint64_t expected,
                                  uint64_t desired)
{
    uint64_t old_val;
    uint64_t result;

    __asm__ volatile(
        "ldxr %0, [%2]\n"
        "cmp %0, %3\n"
        "b.ne 1f\n"
        "stxr %w1, %4, [%2]\n"
        "1:"
        : "=&r"(old_val), "=&r"(result)
        : "r"(addr), "r"(expected), "r"(desired)
        : "cc", "memory"
    );

    return (old_val == expected) && (result == 0U);
}

/**
 * @brief 64位原子读取
 * @details 带内存屏障的64位读取操作
 *
 * @param addr 地址指针
 * @return 读取的值
 */
static inline uint64_t atomic_read_u64(const volatile uint64_t *addr)
{
    uint64_t value;
    __asm__ volatile(
        "ldxr %0, [%1]"
        : "=r"(value)
        : "r"(addr)
        : "memory"
    );
    MEMORY_BARRIER();
    return value;
}

/**
 * @brief 64位原子写入
 * @details 带内存屏障的64位写入操作
 *
 * @param addr 地址指针
 * @param value 要写入的值
 */
static inline void atomic_write_u64(volatile uint64_t *addr, uint64_t value)
{
    MEMORY_BARRIER();
    *addr = value;
    MEMORY_BARRIER();
}

/**
 * @brief 64位原子递增
 * @details 原子地将64位值加1
 *
 * @param addr 地址指针
 * @return 旧值
 */
static inline uint64_t atomic_inc_u64(volatile uint64_t *addr)
{
    uint64_t old_val;
    uint64_t new_val;
    uint64_t result;

    do
    {
        __asm__ volatile(
            "ldxr %0, [%2]"
            : "=&r"(old_val)
            : "r"(addr)
            : "memory"
        );
        new_val = old_val + 1ULL;
        __asm__ volatile(
            "stxr %w0, %1, [%2]"
            : "=&r"(result)
            : "r"(new_val), "r"(addr)
            : "memory"
        );
    }
    while (result != 0U);

    return old_val;
}

/**
 * @brief 64位原子递减
 * @details 原子地将64位值减1
 *
 * @param addr 地址指针
 * @return 旧值
 */
static inline uint64_t atomic_dec_u64(volatile uint64_t *addr)
{
    uint64_t old_val;
    uint64_t new_val;
    uint64_t result;

    do
    {
        __asm__ volatile(
            "ldxr %0, [%2]"
            : "=&r"(old_val)
            : "r"(addr)
            : "memory"
        );
        new_val = old_val - 1ULL;
        __asm__ volatile(
            "stxr %w0, %1, [%2]"
            : "=&r"(result)
            : "r"(new_val), "r"(addr)
            : "memory"
        );
    }
    while (result != 0U);

    return old_val;
}

/**
 * @brief 64位原子加
 * @details 原子地将64位值加上指定数
 *
 * @param addr 地址指针
 * @param value 要加的值
 * @return 旧值
 */
static inline uint64_t atomic_add_u64(volatile uint64_t *addr, uint64_t value)
{
    uint64_t old_val;
    uint64_t new_val;
    uint64_t result;

    do
    {
        __asm__ volatile(
            "ldxr %0, [%2]"
            : "=&r"(old_val)
            : "r"(addr)
            : "memory"
        );
        new_val = old_val + value;
        __asm__ volatile(
            "stxr %w0, %1, [%2]"
            : "=&r"(result)
            : "r"(new_val), "r"(addr)
            : "memory"
        );
    }
    while (result != 0U);

    return old_val;
}

/**
 * @brief 64位原子减
 * @details 原子地将64位值减去指定数
 *
 * @param addr 地址指针
 * @param value 要减的值
 * @return 旧值
 */
static inline uint64_t atomic_sub_u64(volatile uint64_t *addr, uint64_t value)
{
    uint64_t old_val;
    uint64_t new_val;
    uint64_t result;

    do
    {
        __asm__ volatile(
            "ldxr %0, [%2]"
            : "=&r"(old_val)
            : "r"(addr)
            : "memory"
        );
        new_val = old_val - value;
        __asm__ volatile(
            "stxr %w0, %1, [%2]"
            : "=&r"(result)
            : "r"(new_val), "r"(addr)
            : "memory"
        );
    }
    while (result != 0U);

    return old_val;
}

/**
 * @brief 64位原子交换
 * @details 原子地将新值写入并返回旧值
 *
 * @param addr 地址指针
 * @param value 新值
 * @return 旧值
 */
static inline uint64_t atomic_xchg_u64(volatile uint64_t *addr, uint64_t value)
{
    uint64_t old_val;
    uint64_t result;

    do
    {
        __asm__ volatile(
            "ldxr %0, [%2]"
            : "=&r"(old_val)
            : "r"(addr)
            : "memory"
        );
        __asm__ volatile(
            "stxr %w0, %1, [%2]"
            : "=&r"(result)
            : "r"(value), "r"(addr)
            : "memory"
        );
    }
    while (result != 0U);

    return old_val;
}

/* ============================================================================
 * 返回新值的原子操作
 * ============================================================================ */

/**
 * @brief 原子递增并返回新值
 * @details 原子地递增并返回新值（而非旧值）
 *
 * @param addr 地址指针
 * @return 操作后的新值
 */
static inline uint32_t atomic_inc_fetch_u32(volatile uint32_t *addr)
{
    return atomic_inc_u32(addr) + 1U;
}

/**
 * @brief 原子递减并返回新值
 * @details 原子地递减并返回新值（而非旧值）
 *
 * @param addr 地址指针
 * @return 操作后的新值
 */
static inline uint32_t atomic_dec_fetch_u32(volatile uint32_t *addr)
{
    return atomic_dec_u32(addr) - 1U;
}

/**
 * @brief 原子加并返回新值
 * @details 原子地加并返回新值（而非旧值）
 *
 * @param addr 地址指针
 * @param value 要加的值
 * @return 操作后的新值
 */
static inline uint32_t atomic_add_fetch_u32(volatile uint32_t *addr, uint32_t value)
{
    return atomic_add_u32(addr, value) + value;
}

/**
 * @brief 原子减并返回新值
 * @details 原子地减并返回新值（而非旧值）
 *
 * @param addr 地址指针
 * @param value 要减的值
 * @return 操作后的新值
 */
static inline uint32_t atomic_sub_fetch_u32(volatile uint32_t *addr, uint32_t value)
{
    return atomic_sub_u32(addr, value) - value;
}

/**
 * @brief 64位原子递增并返回新值
 * @details 原子地递增并返回新值（而非旧值）
 *
 * @param addr 地址指针
 * @return 操作后的新值
 */
static inline uint64_t atomic_inc_fetch_u64(volatile uint64_t *addr)
{
    return atomic_inc_u64(addr) + 1ULL;
}

/**
 * @brief 64位原子递减并返回新值
 * @details 原子地递减并返回新值（而非旧值）
 *
 * @param addr 地址指针
 * @return 操作后的新值
 */
static inline uint64_t atomic_dec_fetch_u64(volatile uint64_t *addr)
{
    return atomic_dec_u64(addr) - 1ULL;
}

/**
 * @brief 64位原子加并返回新值
 * @details 原子地加并返回新值（而非旧值）
 *
 * @param addr 地址指针
 * @param value 要加的值
 * @return 操作后的新值
 */
static inline uint64_t atomic_add_fetch_u64(volatile uint64_t *addr, uint64_t value)
{
    return atomic_add_u64(addr, value) + value;
}

/**
 * @brief 64位原子减并返回新值
 * @details 原子地减并返回新值（而非旧值）
 *
 * @param addr 地址指针
 * @param value 要减的值
 * @return 操作后的新值
 */
static inline uint64_t atomic_sub_fetch_u64(volatile uint64_t *addr, uint64_t value)
{
    return atomic_sub_u64(addr, value) - value;
}

/* ============================================================================
 * 位测试和设置操作
 * ============================================================================ */

/**
 * @brief 原子测试并设置位
 * @details 原子地测试指定位，如果为0则设置为1
 *
 * @param addr 地址指针
 * @param bit 位号（0-31）
 * @return 如果位原本为0返回true，否则返回false
 */
static inline bool atomic_test_and_set_u32(volatile uint32_t *addr, uint32_t bit)
{
    uint32_t mask = 1U << bit;
    uint32_t old_val = atomic_fetch_or_u32(addr, mask);
    return (old_val & mask) == 0U;
}

/**
 * @brief 原子清除位
 * @details 原子地清除指定位（设置为0）
 *
 * @param addr 地址指针
 * @param bit 位号（0-31）
 * @return 如果位原本为1返回true，否则返回false
 */
static inline bool atomic_test_and_clear_u32(volatile uint32_t *addr, uint32_t bit)
{
    uint32_t mask = 1U << bit;
    uint32_t old_val = atomic_and_u32(addr, ~mask);
    return (old_val & mask) != 0U;
}

/**
 * @brief 原子测试并取反位
 * @details 原子地取反指定位
 *
 * @param addr 地址指针
 * @param bit 位号（0-31）
 * @return 返回位的旧值
 */
static inline bool atomic_test_and_toggle_u32(volatile uint32_t *addr, uint32_t bit)
{
    uint32_t mask = 1U << bit;
    uint32_t old_val = atomic_xor_u32(addr, mask);
    return (old_val & mask) != 0U;
}

/**
 * @brief 64位原子测试并设置位
 * @details 原子地测试指定位，如果为0则设置为1
 *
 * @param addr 地址指针
 * @param bit 位号（0-63）
 * @return 如果位原本为0返回true，否则返回false
 */
static inline bool atomic_test_and_set_u64(volatile uint64_t *addr, uint64_t bit)
{
    uint64_t mask = 1ULL << bit;
    uint64_t old_val;
    uint64_t result;

    do
    {
        __asm__ volatile(
            "ldxr %0, [%2]"
            : "=&r"(old_val)
            : "r"(addr)
            : "memory"
        );
        uint64_t new_val = old_val | mask;
        __asm__ volatile(
            "stxr %w0, %1, [%2]"
            : "=&r"(result)
            : "r"(new_val), "r"(addr)
            : "memory"
        );
    }
    while (result != 0U);

    return (old_val & mask) == 0ULL;
}

/**
 * @brief 64位原子清除位
 * @details 原子地清除指定位（设置为0）
 *
 * @param addr 地址指针
 * @param bit 位号（0-63）
 * @return 如果位原本为1返回true，否则返回false
 */
static inline bool atomic_test_and_clear_u64(volatile uint64_t *addr, uint64_t bit)
{
    uint64_t mask = 1ULL << bit;
    uint64_t old_val;
    uint64_t result;

    do
    {
        __asm__ volatile(
            "ldxr %0, [%2]"
            : "=&r"(old_val)
            : "r"(addr)
            : "memory"
        );
        uint64_t new_val = old_val & ~mask;
        __asm__ volatile(
            "stxr %w0, %1, [%2]"
            : "=&r"(result)
            : "r"(new_val), "r"(addr)
            : "memory"
        );
    }
    while (result != 0U);

    return (old_val & mask) != 0ULL;
}

/* ============================================================================
 * 原子标志操作（用于锁和状态标志）
 * ============================================================================ */

/**
 * @brief 原子获取锁
 * @details 原子地尝试获取锁，如果未锁定则锁定
 *
 * @param lock 锁指针
 * @return 成功获取返回true，失败返回false
 */
static inline bool atomic_acquire_lock(volatile uint32_t *lock)
{
    return atomic_cas_u32(lock, 0U, 1U);
}

/**
 * @brief 原子释放锁
 * @details 原子地释放锁
 *
 * @param lock 锁指针
 */
static inline void atomic_release_lock(volatile uint32_t *lock)
{
    MEMORY_BARRIER();
    *lock = 0U;
    MEMORY_BARRIER();
}

/**
 * @brief 原子设置标志
 * @details 原子地设置标志位
 *
 * @param flag 标志指针
 * @return 旧的标志值
 */
static inline uint32_t atomic_set_flag(volatile uint32_t *flag)
{
    return atomic_xchg_u32(flag, 1U);
}

/**
 * @brief 原子清除标志
 * @details 原子地清除标志位
 *
 * @param flag 标志指针
 * @return 旧的标志值
 */
static inline uint32_t atomic_clear_flag(volatile uint32_t *flag)
{
    return atomic_xchg_u32(flag, 0U);
}

/* ============================================================================
 * 带内存序选项的原子操作（简化版）
 * ============================================================================ */

/**
 * @brief 原子加载（宽松内存序）
 * @details 不带内存屏障的原子读取
 *
 * @param addr 地址指针
 * @return 读取的值
 */
static inline uint32_t atomic_load_relaxed_u32(const volatile uint32_t *addr)
{
    return *addr;
}

/**
 * @brief 原子加载（获取内存序）
 * @details 带获取内存屏障的原子读取
 *
 * @param addr 地址指针
 * @return 读取的值
 */
static inline uint32_t atomic_load_acquire_u32(const volatile uint32_t *addr)
{
    uint32_t value = *addr;
    ARM64_DMB(acquire);
    return value;
}

/**
 * @brief 原子存储（宽松内存序）
 * @details 不带内存屏障的原子写入
 *
 * @param addr 地址指针
 * @param value 要写入的值
 */
static inline void atomic_store_relaxed_u32(volatile uint32_t *addr, uint32_t value)
{
    *addr = value;
}

/**
 * @brief 原子存储（释放内存序）
 * @details 带释放内存屏障的原子写入
 *
 * @param addr 地址指针
 * @param value 要写入的值
 */
static inline void atomic_store_release_u32(volatile uint32_t *addr, uint32_t value)
{
    ARM64_DMB(release);
    *addr = value;
}

/**
 * @brief 原子读-改-写操作（获取内存序）
 * @details 带获取屏障的原子加法
 *
 * @param addr 地址指针
 * @param value 要加的值
 * @return 旧值
 */
static inline uint32_t atomic_fetch_add_acquire_u32(volatile uint32_t *addr,
                                                     uint32_t value)
{
    uint32_t old_val = atomic_fetch_add(addr, value);
    ARM64_DMB(acquire);
    return old_val;
}

/**
 * @brief 原子读-改-写操作（释放内存序）
 * @details 带释放屏障的原子加法
 *
 * @param addr 地址指针
 * @param value 要加的值
 * @return 旧值
 */
static inline uint32_t atomic_fetch_add_release_u32(volatile uint32_t *addr,
                                                     uint32_t value)
{
    ARM64_DMB(release);
    return atomic_fetch_add(addr, value);
}

#ifdef __cplusplus
}
#endif

#endif /* BARRIER_H */
