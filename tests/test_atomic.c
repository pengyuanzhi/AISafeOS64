/**
 * @file test_atomic.c
 * @brief AISafe64 RTOS - 原子操作单元测试
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 原子操作的全面单元测试
 *          - 32位原子操作
 *          - 64位原子操作
 *          - CAS操作
 *          - Fetch操作
 *          - 内存屏障
 *
 * @note MISRA-C:2012合规
 */

#include "test_framework.h"
#include "../src/arch/arm64/include/barrier.h"
#include <stdint.h>
#include <stddef.h>

/**
 * @brief 测试32位原子加法
 */
TEST_CASE(atomic_add_u32_basic)
{
    volatile uint32_t value = 10U;

    uint32_t result = atomic_add_u32(&value, 5U);

    TEST_ASSERT_EQ(value, 15U);
    TEST_ASSERT_EQ(result, 10U);  /* 返回旧值 */
}

/**
 * @brief 测试32位原子减法
 */
TEST_CASE(atomic_sub_u32_basic)
{
    volatile uint32_t value = 20U;

    uint32_t result = atomic_sub_u32(&value, 7U);

    TEST_ASSERT_EQ(value, 13U);
    TEST_ASSERT_EQ(result, 20U);  /* 返回旧值 */
}

/**
 * @brief 测试32位原子自增
 */
TEST_CASE(atomic_inc_u32_basic)
{
    volatile uint32_t value = 99U;

    uint32_t result = atomic_inc_u32(&value);

    TEST_ASSERT_EQ(value, 100U);
    TEST_ASSERT_EQ(result, 99U);  /* 返回旧值 */
}

/**
 * @brief 测试32位原子自减
 */
TEST_CASE(atomic_dec_u32_basic)
{
    volatile uint32_t value = 50U;

    uint32_t result = atomic_dec_u32(&value);

    TEST_ASSERT_EQ(value, 49U);
    TEST_ASSERT_EQ(result, 50U);  /* 返回旧值 */
}

/**
 * @brief 测试32位原子交换
 */
TEST_CASE(atomic_xchg_u32_basic)
{
    volatile uint32_t value = 100U;

    uint32_t result = atomic_xchg_u32(&value, 200U);

    TEST_ASSERT_EQ(value, 200U);
    TEST_ASSERT_EQ(result, 100U);  /* 返回旧值 */
}

/**
 * @brief 测试32位原子CAS（成功）
 */
TEST_CASE(atomic_cas_u32_success)
{
    volatile uint32_t value = 50U;

    bool result = atomic_cas_u32(&value, 50U, 100U);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQ(value, 100U);
}

/**
 * @brief 测试32位原子CAS（失败）
 */
TEST_CASE(atomic_cas_u32_fail)
{
    volatile uint32_t value = 50U;

    bool result = atomic_cas_u32(&value, 60U, 100U);

    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQ(value, 50U);  /* 值不变 */
}

/**
 * @brief 测试32位原子比较交换强类型
 */
TEST_CASE(atomic_compare_exchange_strong_u32)
{
    volatile uint32_t value = 50U;
    uint32_t expected = 50U;

    bool result = atomic_compare_exchange_strong(&value, &expected, 100U);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQ(value, 100U);
    TEST_ASSERT_EQ(expected, 50U);

    /* 再次尝试（应该失败） */
    expected = 50U;
    result = atomic_compare_exchange_strong(&value, &expected, 200U);

    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_EQ(value, 100U);  /* 值不变 */
    TEST_ASSERT_EQ(expected, 100U);  /* expected被更新 */
}

/**
 * @brief 测试64位原子加法
 */
TEST_CASE(atomic_add_u64_basic)
{
    volatile uint64_t value = 1000UL;

    uint64_t result = atomic_add_u64(&value, 500UL);

    TEST_ASSERT_EQ(result, 1000UL);  /* 返回旧值 */
    TEST_ASSERT_EQ(value, 1500UL);
}

/**
 * @brief 测试64位原子自增
 */
TEST_CASE(atomic_inc_u64_basic)
{
    volatile uint64_t value = 0xFFFFFFFFUL;

    uint64_t result = atomic_inc_u64(&value);

    TEST_ASSERT_EQ(result, 0xFFFFFFFFUL);  /* 返回旧值 */
    TEST_ASSERT_EQ(value, 0x100000000UL);
}

/**
 * @brief 测试64位原子CAS
 */
TEST_CASE(atomic_cas_u64_basic)
{
    volatile uint64_t value = 0xDEADBEEFUL;

    bool result = atomic_cas_u64(&value, 0xDEADBEEFUL, 0x12345678UL);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQ(value, 0x12345678UL);
}

/**
 * @brief 测试原子自增并返回新值
 */
TEST_CASE(atomic_inc_fetch_u32_basic)
{
    volatile uint32_t value = 99U;

    uint32_t result = atomic_inc_fetch_u32(&value);

    TEST_ASSERT_EQ(result, 100U);  /* 返回新值 */
    TEST_ASSERT_EQ(value, 100U);
}

/**
 * @brief 测试原子自减并返回新值
 */
TEST_CASE(atomic_dec_fetch_u32_basic)
{
    volatile uint32_t value = 50U;

    uint32_t result = atomic_dec_fetch_u32(&value);

    TEST_ASSERT_EQ(result, 49U);  /* 返回新值 */
    TEST_ASSERT_EQ(value, 49U);
}

/**
 * @brief 测试原子加法并返回新值
 */
TEST_CASE(atomic_add_fetch_u32_basic)
{
    volatile uint32_t value = 10U;

    uint32_t result = atomic_add_fetch_u32(&value, 5U);

    TEST_ASSERT_EQ(result, 15U);  /* 返回新值 */
    TEST_ASSERT_EQ(value, 15U);
}

/**
 * @brief 测试原子减法并返回新值
 */
TEST_CASE(atomic_sub_fetch_u32_basic)
{
    volatile uint32_t value = 20U;

    uint32_t result = atomic_sub_fetch_u32(&value, 7U);

    TEST_ASSERT_EQ(result, 13U);  /* 返回新值 */
    TEST_ASSERT_EQ(value, 13U);
}

/**
 * @brief 测试原子位测试并设置
 */
TEST_CASE(atomic_test_and_set_u32_basic)
{
    volatile uint32_t value = 0x0000F0F0U;

    /* 设置第8位（bit 8） */
    bool was_set = atomic_test_and_set_u32(&value, 8U);

    TEST_ASSERT_FALSE(was_set);  /* 原来未设置 */
    TEST_ASSERT_EQ(value, 0x0001F0F0U);  /* bit 8 被设置 */

    /* 再次设置第8位 */
    was_set = atomic_test_and_set_u32(&value, 8U);

    TEST_ASSERT_TRUE(was_set);  /* 已经设置 */
}

/**
 * @brief 测试原子位测试并清除
 */
TEST_CASE(atomic_test_and_clear_u32_basic)
{
    volatile uint32_t value = 0xFFFF00FFU;

    /* 清除第16位（bit 16） */
    bool was_set = atomic_test_and_clear_u32(&value, 16U);

    TEST_ASSERT_TRUE(was_set);  /* 原来已设置 */
    TEST_ASSERT_EQ(value, 0xFFFE00FFU);  /* bit 16 被清除 */

    /* 再次清除第16位 */
    was_set = atomic_test_and_clear_u32(&value, 16U);

    TEST_ASSERT_FALSE(was_set);  /* 已经清除 */
}

/**
 * @brief 测试原子位测试并翻转
 */
TEST_CASE(atomic_test_and_toggle_u32_basic)
{
    volatile uint32_t value = 0x12345678U;

    /* 翻转第0位 */
    bool was_set = atomic_test_and_toggle_u32(&value, 0U);

    TEST_ASSERT_FALSE(was_set);  /* bit 0 = 0 */
    TEST_ASSERT_EQ(value, 0x12345679U);  /* bit 0 翻转为1 */

    /* 再次翻转第0位 */
    was_set = atomic_test_and_toggle_u32(&value, 0U);

    TEST_ASSERT_TRUE(was_set);  /* bit 0 = 1 */
    TEST_ASSERT_EQ(value, 0x12345678U);  /* bit 0 翻转回0 */
}

/**
 * @brief 测试原子获取锁
 */
TEST_CASE(atomic_acquire_lock_basic)
{
    volatile uint32_t lock = 0U;

    /* 第一次获取锁（成功） */
    bool success = atomic_acquire_lock(&lock);

    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_EQ(lock, 1U);

    /* 第二次获取锁（失败） */
    success = atomic_acquire_lock(&lock);

    TEST_ASSERT_FALSE(success);
    TEST_ASSERT_EQ(lock, 1U);
}

/**
 * @brief 测试原子释放锁
 */
TEST_CASE(atomic_release_lock_basic)
{
    volatile uint32_t lock = 1U;

    /* 释放锁 */
    atomic_release_lock(&lock);

    TEST_ASSERT_EQ(lock, 0U);

    /* 现在可以获取锁了 */
    bool success = atomic_acquire_lock(&lock);

    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_EQ(lock, 1U);
}

/**
 * @brief 测试原子设置标志
 */
TEST_CASE(atomic_set_flag_basic)
{
    volatile uint32_t flag = 0U;

    uint32_t old = atomic_set_flag(&flag);

    TEST_ASSERT_EQ(old, 0U);  /* 返回旧值 */
    TEST_ASSERT_EQ(flag, 1U);
}

/**
 * @brief 测试原子清除标志
 */
TEST_CASE(atomic_clear_flag_basic)
{
    volatile uint32_t flag = 1U;

    uint32_t old = atomic_clear_flag(&flag);

    TEST_ASSERT_EQ(old, 1U);  /* 返回旧值 */
    TEST_ASSERT_EQ(flag, 0U);
}

/**
 * @brief 测试内存屏障（编译时检查）
 */
TEST_CASE(memory_barrier_exists)
{
    /* 这些函数在编译时存在即可，运行时无法直接测试效果 */
    volatile uint32_t value = 100U;

    /* 调用所有内存屏障函数确保它们可以编译通过 */
    MEMORY_BARRIER();
    COMPILER_BARRIER();
    WMB();
    RMB();
    SMB();

    /* 使用内存屏障确保volatile访问不被优化掉 */
    MEMORY_BARRIER();
    uint32_t tmp = value;
    (void)tmp;
    MEMORY_BARRIER();

    TEST_ASSERT_TRUE(true);  /* 如果能编译到这里就通过 */
}

/**
 * @brief 测试WFE和SEV指令
 */
TEST_CASE(wfe_sev_instructions)
{
    /* 这些指令在实际硬件上才有意义，这里只测试能编译通过 */
    WFE();
    SEVL();
    WFE();

    TEST_ASSERT_TRUE(true);  /* 如果能编译到这里就通过 */
}

/**
 * @brief 测试原子操作顺序一致性
 */
TEST_CASE(atomic_operations_sequential)
{
    volatile uint32_t value = 0U;

    /* 连续执行多个原子操作 */
    TEST_ASSERT_EQ(atomic_inc_u32(&value), 0U);
    TEST_ASSERT_EQ(atomic_inc_u32(&value), 1U);
    TEST_ASSERT_EQ(atomic_inc_u32(&value), 2U);
    TEST_ASSERT_EQ(value, 3U);

    TEST_ASSERT_EQ(atomic_add_u32(&value, 10U), 3U);
    TEST_ASSERT_EQ(value, 13U);

    TEST_ASSERT_EQ(atomic_sub_u32(&value, 5U), 13U);
    TEST_ASSERT_EQ(value, 8U);

    TEST_ASSERT_EQ(atomic_xchg_u32(&value, 100U), 8U);
    TEST_ASSERT_EQ(value, 100U);
}

/**
 * @brief 测试原子操作边界值
 */
TEST_CASE(atomic_operations_boundary)
{
    volatile uint32_t max = 0xFFFFFFFFU;
    volatile uint32_t min = 0U;

    /* 最大值减1 */
    TEST_ASSERT_EQ(atomic_dec_u32(&max), 0xFFFFFFFFU);
    TEST_ASSERT_EQ(max, 0xFFFFFFFEU);

    /* 最小值加1 */
    TEST_ASSERT_EQ(atomic_inc_u32(&min), 0U);
    TEST_ASSERT_EQ(min, 1U);

    /* 交换边界值 */
    volatile uint32_t value = 0U;
    TEST_ASSERT_EQ(atomic_xchg_u32(&value, 0xFFFFFFFFU), 0U);
    TEST_ASSERT_EQ(value, 0xFFFFFFFFU);
}

/**
 * @brief 主测试函数
 */
int main(void)
{
    /* 初始化测试框架 */
    test_init();

    /* 测试32位原子操作 */
    TEST_SUITE_START(atomic_u32)
    {
        TEST_RUN(atomic_add_u32_basic);
        TEST_RUN(atomic_sub_u32_basic);
        TEST_RUN(atomic_inc_u32_basic);
        TEST_RUN(atomic_dec_u32_basic);
        TEST_RUN(atomic_xchg_u32_basic);
        TEST_RUN(atomic_cas_u32_success);
        TEST_RUN(atomic_cas_u32_fail);
        TEST_RUN(atomic_compare_exchange_strong_u32);
        TEST_RUN(atomic_operations_sequential);
        TEST_RUN(atomic_operations_boundary);
    }
    TEST_SUITE_END()

    /* 测试64位原子操作 */
    TEST_SUITE_START(atomic_u64)
    {
        TEST_RUN(atomic_add_u64_basic);
        TEST_RUN(atomic_inc_u64_basic);
        TEST_RUN(atomic_cas_u64_basic);
    }
    TEST_SUITE_END()

    /* 测试fetch操作 */
    TEST_SUITE_START(fetch_operations)
    {
        TEST_RUN(atomic_inc_fetch_u32_basic);
        TEST_RUN(atomic_dec_fetch_u32_basic);
        TEST_RUN(atomic_add_fetch_u32_basic);
        TEST_RUN(atomic_sub_fetch_u32_basic);
    }
    TEST_SUITE_END()

    /* 测试位操作 */
    TEST_SUITE_START(bit_operations)
    {
        TEST_RUN(atomic_test_and_set_u32_basic);
        TEST_RUN(atomic_test_and_clear_u32_basic);
        TEST_RUN(atomic_test_and_toggle_u32_basic);
    }
    TEST_SUITE_END()

    /* 测试锁和标志操作 */
    TEST_SUITE_START(lock_operations)
    {
        TEST_RUN(atomic_acquire_lock_basic);
        TEST_RUN(atomic_release_lock_basic);
        TEST_RUN(atomic_set_flag_basic);
        TEST_RUN(atomic_clear_flag_basic);
    }
    TEST_SUITE_END()

    /* 测试内存屏障 */
    TEST_SUITE_START(memory_barriers)
    {
        TEST_RUN(memory_barrier_exists);
        TEST_RUN(wfe_sev_instructions);
    }
    TEST_SUITE_END()

    /* 打印测试报告 */
    test_report();

    /* 返回测试结果 */
    return (g_test_stats.failed_tests == 0U) ? 0 : 1;
}
