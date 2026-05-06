/**
 * @file    test_vgic.c
 * @brief   虚拟 GIC（VGIC）单元测试
 * @author  AISafe64 Team
 * @date    2026-05-03
 * @version 1.0
 *
 * @details 测试 VGIC 的以下功能：
 *          - VGIC 初始化/销毁
 *          - 中断注入/清除
 *          - 优先级设置
 *          - 中断路由
 *          - 使能/禁用
 *          - 状态检查
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <unity.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <kernel/types.h>
#include "vgic.h"
#include "vmm.h"
#include "vm.h"
#include "vmm_stats.h"

/* ========================================================================
 * 测试常量
 * ======================================================================== */

#define TEST_VM_ID             (0U)
#define TEST_VCPU_ID           (0U)
#define TEST_IRQ_0             (0U)
#define TEST_IRQ_1             (1U)
#define TEST_IRQ_255           (255U)

/* ========================================================================
 * 测试夹具
 * ======================================================================== */

void setUp(void)
{
    /* 每个 test 前执行 */
    vgic_global_init();
}

void tearDown(void)
{
    /* 每个 test 后执行 */
    /* 清空 VGIC */
    vgic_clear_all_irqs(TEST_VM_ID);
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 测试 VGIC 初始化
 */
void test_vgic_init(void)
{
    kernel_status_t ret;

    ret = vgic_init(TEST_VM_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
}

/**
 * @brief 测试 VGIC 销毁
 */
void test_vgic_destroy(void)
{
    kernel_status_t ret;

    /* 初始化 VGIC */
    ret = vgic_init(TEST_VM_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 销毁 VGIC */
    ret = vgic_destroy(TEST_VM_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
}

/**
 * @brief 测试中断注入
 */
void test_vgic_inject_irq(void)
{
    kernel_status_t ret;
    bool pending;

    /* 初始化 VGIC */
    ret = vgic_init(TEST_VM_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 使能中断 */
    ret = vgic_enable_irq(TEST_VM_ID, TEST_IRQ_0, true);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 注入中断 */
    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 检查中断是否挂起 */
    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_TRUE(pending);
}

/**
 * @brief 测试中断注入（无效中断号）
 */
void test_vgic_inject_irq_invalid_irq(void)
{
    kernel_status_t ret;

    /* 初始化 VGIC */
    ret = vgic_init(TEST_VM_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 注入无效中断（256 >= MAX_INTERRUPTS） */
    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, 256U);
    TEST_ASSERT_LESS_THAN_INT32(0, ret);
}

/**
 * @brief 测试中断注入（未使能）
 */
void test_vgic_inject_irq_not_enabled(void)
{
    kernel_status_t ret;

    /* 初始化 VGIC */
    ret = vgic_init(TEST_VM_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 不使能中断，直接注入 */
    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_LESS_THAN_INT32(0, ret);
}

/**
 * @brief 测试中断清除
 */
void test_vgic_clear_irq(void)
{
    kernel_status_t ret;
    bool pending;

    /* 初始化 VGIC */
    ret = vgic_init(TEST_VM_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 使能中断 */
    ret = vgic_enable_irq(TEST_VM_ID, TEST_IRQ_0, true);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 注入中断 */
    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 清除中断 */
    ret = vgic_clear_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 检查中断是否未挂起 */
    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_FALSE(pending);
}

/**
 * @brief 测试优先级设置
 */
void test_vgic_set_priority(void)
{
    kernel_status_t ret;
    uint8_t priority;

    /* 初始化 VGIC */
    ret = vgic_init(TEST_VM_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 设置优先级为 0（最高） */
    ret = vgic_set_priority(TEST_VM_ID, TEST_IRQ_0, 0U);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 设置优先级为 7（最低） */
    ret = vgic_set_priority(TEST_VM_ID, TEST_IRQ_0, 7U);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 设置优先级为无效值（8） */
    ret = vgic_set_priority(TEST_VM_ID, TEST_IRQ_0, 8U);
    TEST_ASSERT_LESS_THAN_INT32(0, ret);
}

/**
 * @brief 测试中断路由
 */
void test_vgic_set_target(void)
{
    kernel_status_t ret;

    /* 初始化 VGIC */
    ret = vgic_init(TEST_VM_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 设置中断路由到 CPU 0 */
    ret = vgic_set_target(TEST_VM_ID, TEST_IRQ_0, 0x1U);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 设置中断路由到 CPU 0 和 CPU 1 */
    ret = vgic_set_target(TEST_VM_ID, TEST_IRQ_0, 0x3U);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
}

/**
 * @brief 测试中断使能
 */
void test_vgic_enable_irq(void)
{
    kernel_status_t ret;

    /* 初始化 VGIC */
    ret = vgic_init(TEST_VM_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 使能中断 */
    ret = vgic_enable_irq(TEST_VM_ID, TEST_IRQ_0, true);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 再次使能中断 */
    ret = vgic_enable_irq(TEST_VM_ID, TEST_IRQ_0, true);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
}

/**
 * @brief 测试中断禁用
 */
void test_vgic_disable_irq(void)
{
    kernel_status_t ret;

    /* 初始化 VGIC */
    ret = vgic_init(TEST_VM_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 使能中断 */
    ret = vgic_enable_irq(TEST_VM_ID, TEST_IRQ_0, true);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 禁用中断 */
    ret = vgic_enable_irq(TEST_VM_ID, TEST_IRQ_0, false);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 再次禁用中断 */
    ret = vgic_enable_irq(TEST_VM_ID, TEST_IRQ_0, false);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
}

/**
 * @brief 测试检查中断是否挂起
 */
void test_vgic_irq_is_pending(void)
{
    kernel_status_t ret;
    bool pending;

    /* 初始化 VGIC */
    ret = vgic_init(TEST_VM_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 使能中断 */
    ret = vgic_enable_irq(TEST_VM_ID, TEST_IRQ_0, true);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 注入前，中断未挂起 */
    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_FALSE(pending);

    /* 注入中断 */
    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 注入后，中断挂起 */
    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_TRUE(pending);

    /* 清除中断 */
    ret = vgic_clear_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 清除后，中断未挂起 */
    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_FALSE(pending);
}

/**
 * @brief 测试获取中断状态
 */
void test_vgic_get_irq_state(void)
{
    kernel_status_t ret;
    vgic_irq_state_t state;

    /* 初始化 VGIC */
    ret = vgic_init(TEST_VM_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 初始状态为 INACTIVE */
    state = vgic_get_irq_state(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQUAL_INT32(VGIC_IRQ_INACTIVE, state);

    /* 使能中断 */
    ret = vgic_enable_irq(TEST_VM_ID, TEST_IRQ_0, true);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 注入中断 */
    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 注入后，状态为 PENDING */
    state = vgic_get_irq_state(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQUAL_INT32(VGIC_IRQ_PENDING, state);

    /* 清除中断 */
    ret = vgic_clear_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 清除后，状态为 INACTIVE */
    state = vgic_get_irq_state(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQUAL_INT32(VGIC_IRQ_INACTIVE, state);
}

/**
 * @brief 测试清空所有中断
 */
void test_vgic_clear_all_irqs(void)
{
    kernel_status_t ret;
    bool pending0;
    bool pending1;

    /* 初始化 VGIC */
    ret = vgic_init(TEST_VM_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 使能中断 */
    ret = vgic_enable_irq(TEST_VM_ID, TEST_IRQ_0, true);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    ret = vgic_enable_irq(TEST_VM_ID, TEST_IRQ_1, true);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 注入中断 */
    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_1);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 检查两个中断都挂起 */
    pending0 = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    pending1 = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_1);
    TEST_ASSERT_TRUE(pending0);
    TEST_ASSERT_TRUE(pending1);

    /* 清空所有中断 */
    vgic_clear_all_irqs(TEST_VM_ID);

    /* 检查两个中断都未挂起 */
    pending0 = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    pending1 = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_1);
    TEST_ASSERT_FALSE(pending0);
    TEST_ASSERT_FALSE(pending1);
}

/**
 * @brief 测试多个中断
 */
void test_vgic_multiple_irqs(void)
{
    kernel_status_t ret;
    bool pending;

    /* 初始化 VGIC */
    ret = vgic_init(TEST_VM_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 使能多个中断 */
    ret = vgic_enable_irq(TEST_VM_ID, TEST_IRQ_0, true);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    ret = vgic_enable_irq(TEST_VM_ID, TEST_IRQ_1, true);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    ret = vgic_enable_irq(TEST_VM_ID, TEST_IRQ_255, true);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 注入多个中断 */
    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_1);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_255);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 检查三个中断都挂起 */
    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_TRUE(pending);

    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_1);
    TEST_ASSERT_TRUE(pending);

    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_255);
    TEST_ASSERT_TRUE(pending);
}

/* ========================================================================
 * 测试主函数
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_vgic_init);
    RUN_TEST(test_vgic_destroy);
    RUN_TEST(test_vgic_inject_irq);
    RUN_TEST(test_vgic_inject_irq_invalid_irq);
    RUN_TEST(test_vgic_inject_irq_not_enabled);
    RUN_TEST(test_vgic_clear_irq);
    RUN_TEST(test_vgic_set_priority);
    RUN_TEST(test_vgic_set_target);
    RUN_TEST(test_vgic_enable_irq);
    RUN_TEST(test_vgic_disable_irq);
    RUN_TEST(test_vgic_irq_is_pending);
    RUN_TEST(test_vgic_get_irq_state);
    RUN_TEST(test_vgic_clear_all_irqs);
    RUN_TEST(test_vgic_multiple_irqs);

    return UNITY_END();
}
