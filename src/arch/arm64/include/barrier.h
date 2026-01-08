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
 *
 * @note MISRA-C:2012合规
 * @note 仅适用于ARMv8-A架构
 */

#ifndef BARRIER_H
#define BARRIER_H

#include <stdint.h>

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
                                  uint32_t desired) {
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
 * @brief 原子读取
 * @details 带内存屏障的读取操作
 *
 * @param addr 地址指针
 * @return 读取的值
 */
static inline uint32_t atomic_read_u32(const volatile uint32_t *addr) {
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
static inline void atomic_write_u32(volatile uint32_t *addr, uint32_t value) {
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
static inline uint32_t atomic_inc_u32(volatile uint32_t *addr) {
    uint32_t old_val;
    uint32_t new_val;
    uint32_t result;

    do {
        LDXR(old_val, addr);
        new_val = old_val + 1U;
        STXR(result, new_val, addr);
    } while (result != 0U);

    return old_val;
}

/**
 * @brief 原子递减
 * @details 原子地将值减1
 *
 * @param addr 地址指针
 * @return 旧值
 */
static inline uint32_t atomic_dec_u32(volatile uint32_t *addr) {
    uint32_t old_val;
    uint32_t new_val;
    uint32_t result;

    do {
        LDXR(old_val, addr);
        new_val = old_val - 1U;
        STXR(result, new_val, addr);
    } while (result != 0U);

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
static inline uint32_t atomic_add_u32(volatile uint32_t *addr, uint32_t value) {
    uint32_t old_val;
    uint32_t new_val;
    uint32_t result;

    do {
        LDXR(old_val, addr);
        new_val = old_val + value;
        STXR(result, new_val, addr);
    } while (result != 0U);

    return old_val;
}

/**
 * @brief 原子减
 * @details 原子地将值减去指定数
 *
 * @param addr 地址指针
 * @param value 要减的值
 * @return 旧值
 */
static inline uint32_t atomic_sub_u32(volatile uint32_t *addr, uint32_t value) {
    uint32_t old_val;
    uint32_t new_val;
    uint32_t result;

    do {
        LDXR(old_val, addr);
        new_val = old_val - value;
        STXR(result, new_val, addr);
    } while (result != 0U);

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
static inline uint32_t atomic_and_u32(volatile uint32_t *addr, uint32_t value) {
    uint32_t old_val;
    uint32_t new_val;
    uint32_t result;

    do {
        LDXR(old_val, addr);
        new_val = old_val & value;
        STXR(result, new_val, addr);
    } while (result != 0U);

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
static inline uint32_t atomic_or_u32(volatile uint32_t *addr, uint32_t value) {
    uint32_t old_val;
    uint32_t new_val;
    uint32_t result;

    do {
        LDXR(old_val, addr);
        new_val = old_val | value;
        STXR(result, new_val, addr);
    } while (result != 0U);

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
static inline uint32_t atomic_xor_u32(volatile uint32_t *addr, uint32_t value) {
    uint32_t old_val;
    uint32_t new_val;
    uint32_t result;

    do {
        LDXR(old_val, addr);
        new_val = old_val ^ value;
        STXR(result, new_val, addr);
    } while (result != 0U);

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
static inline uint32_t atomic_xchg_u32(volatile uint32_t *addr, uint32_t value) {
    uint32_t old_val;
    uint32_t result;

    do {
        LDXR(old_val, addr);
        STXR(result, value, addr);
    } while (result != 0U);

    return old_val;
}

#ifdef __cplusplus
}
#endif

#endif /* BARRIER_H */
