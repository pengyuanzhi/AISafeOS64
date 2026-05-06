/**
 * @file    test_vcpu.c
 * @brief   vCPU 上下文管理单元测试
 * @author  AISafe64 Team
 * @date    2026-05-03
 * @version 1.0
 *
 * @details 测试 vCPU 上下文管理的以下功能：
 *          - 状态管理
 *          - 上下文保存/恢复
 *          - 寄存器操作
 *          - vCPU 重置
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
#include "vcpu.h"
#include "vm.h"
#include "vmm.h"

/* ========================================================================
 * 测试常量
 * ======================================================================== */

#define TEST_VM_ID    (0U)
#define TEST_VCPU_ID  (0U)

/* ========================================================================
 * 测试夹具
 * ======================================================================== */

void setUp(void)
{
    /* 初始化 VMM */
    vmm_init();
}

void tearDown(void)
{
    /* 清理 VMM */
    /* vmm_deinit(); */
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 测试获取 vCPU 状态
 */
void test_vcpu_get_state(void)
{
    kernel_status_t ret;
    vcpu_state_t state;
    uint32_t vm_id;
    int32_t vcpu_id;

    /* 创建 VM */
    vm_id = vmm_create_vm("test_vm", 0x10000000ULL);
    TEST_ASSERT_GREATER_THAN_INT32(0, (int32_t)vm_id);

    /* 创建 vCPU */
    vcpu_id = vmm_create_vcpu(vm_id, 0x40000000ULL);
    TEST_ASSERT_GREATER_THAN_INT32(-1, vcpu_id);

    /* 获取 vCPU 状态 */
    ret = vcpu_get_state(vm_id, (uint32_t)vcpu_id, &state);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_UINT32(VCPU_STATE_STOPPED, state);
}

/**
 * @brief 测试设置 vCPU 状态（合法转换）
 */
void test_vcpu_set_state_valid_transition(void)
{
    kernel_status_t ret;
    vcpu_state_t state;
    uint32_t vm_id;
    int32_t vcpu_id;

    /* 创建 VM */
    vm_id = vmm_create_vm("test_vm", 0x10000000ULL);
    TEST_ASSERT_GREATER_THAN_INT32(0, (int32_t)vm_id);

    /* 创建 vCPU */
    vcpu_id = vmm_create_vcpu(vm_id, 0x40000000ULL);
    TEST_ASSERT_GREATER_THAN_INT32(-1, vcpu_id);

    /* STOPPED → RUNNING */
    ret = vcpu_set_state(vm_id, (uint32_t)vcpu_id, VCPU_STATE_RUNNING);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    ret = vcpu_get_state(vm_id, (uint32_t)vcpu_id, &state);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_UINT32(VCPU_STATE_RUNNING, state);

    /* RUNNING → BLOCKED */
    ret = vcpu_set_state(vm_id, (uint32_t)vcpu_id, VCPU_STATE_BLOCKED);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    ret = vcpu_get_state(vm_id, (uint32_t)vcpu_id, &state);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_UINT32(VCPU_STATE_BLOCKED, state);
}

/**
 * @brief 测试设置 vCPU 状态（非法转换）
 */
void test_vcpu_set_state_invalid_transition(void)
{
    kernel_status_t ret;
    uint32_t vm_id;
    int32_t vcpu_id;

    /* 创建 VM */
    vm_id = vmm_create_vm("test_vm", 0x10000000ULL);
    TEST_ASSERT_GREATER_THAN_INT32(0, (int32_t)vm_id);

    /* 创建 vCPU */
    vcpu_id = vmm_create_vcpu(vm_id, 0x40000000ULL);
    TEST_ASSERT_GREATER_THAN_INT32(-1, vcpu_id);

    /* OFF → RUNNING (非法) */
    ret = vcpu_set_state(vm_id, (uint32_t)vcpu_id, VCPU_STATE_OFF);
    TEST_ASSERT_LESS_THAN_INT32(0, ret);
}

/**
 * @brief 测试设置 vCPU 状态（无效参数）
 */
void test_vcpu_set_state_invalid_params(void)
{
    kernel_status_t ret;

    /* NULL 指针 */
    ret = vcpu_set_state(0U, 0U, VCPU_STATE_STOPPED);
    TEST_ASSERT_LESS_THAN_INT32(0, ret);

    /* 无效状态 */
    ret = vcpu_set_state(0U, 0U, VCPU_STATE_MAX);
    TEST_ASSERT_LESS_THAN_INT32(0, ret);
}

/**
 * @brief 测试保存/恢复 vCPU 上下文
 */
void test_vcpu_save_restore_context(void)
{
    kernel_status_t ret;
    cpu_context_t ctx1, ctx2;
    uint32_t vm_id;
    int32_t vcpu_id;

    /* 创建 VM */
    vm_id = vmm_create_vm("test_vm", 0x10000000ULL);
    TEST_ASSERT_GREATER_THAN_INT32(0, (int32_t)vm_id);

    /* 创建 vCPU */
    vcpu_id = vmm_create_vcpu(vm_id, 0x40000000ULL);
    TEST_ASSERT_GREATER_THAN_INT32(-1, vcpu_id);

    /* 保存上下文 */
    ret = vcpu_save_context(vm_id, (uint32_t)vcpu_id, &ctx1);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 恢复上下文 */
    ret = vcpu_restore_context(vm_id, (uint32_t)vcpu_id, &ctx2);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 检查上下文是否一致 */
    TEST_ASSERT_EQUAL_UINT64_ARRAY(ctx1.gp_regs.x, ctx2.gp_regs.x, VCPU_GP_REG_COUNT);
}

/**
 * @brief 测试保存/恢复 vCPU 上下文（无效参数）
 */
void test_vcpu_save_restore_context_invalid_params(void)
{
    kernel_status_t ret;
    cpu_context_t ctx;

    /* NULL 指针（保存） */
    ret = vcpu_save_context(0U, 0U, NULL);
    TEST_ASSERT_LESS_THAN_INT32(0, ret);

    /* NULL 指针（恢复） */
    ret = vcpu_restore_context(0U, 0U, NULL);
    TEST_ASSERT_LESS_THAN_INT32(0, ret);
}

/**
 * @brief 测试获取/设置 vCPU 寄存器
 */
void test_vcpu_get_set_regs(void)
{
    kernel_status_t ret;
    vcpu_gpregs_t regs1, regs2;
    uint32_t vm_id;
    int32_t vcpu_id;

    /* 创建 VM */
    vm_id = vmm_create_vm("test_vm", 0x10000000ULL);
    TEST_ASSERT_GREATER_THAN_INT32(0, (int32_t)vm_id);

    /* 创建 vCPU */
    vcpu_id = vmm_create_vcpu(vm_id, 0x40000000ULL);
    TEST_ASSERT_GREATER_THAN_INT32(-1, vcpu_id);

    /* 获取寄存器 */
    ret = vcpu_get_regs(vm_id, (uint32_t)vcpu_id, &regs1);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 修改寄存器 */
    regs1.x[0] = 0xDEADBEEFDEADBEEFULL;
    regs1.pc = 0x40000000ULL;
    regs1.pstate = 0x3C5ULL;

    /* 设置寄存器 */
    ret = vcpu_set_regs(vm_id, (uint32_t)vcpu_id, &regs1);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 获取寄存器 */
    ret = vcpu_get_regs(vm_id, (uint32_t)vcpu_id, &regs2);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 检查寄存器是否一致 */
    TEST_ASSERT_EQUAL_UINT64_ARRAY(regs1.x, regs2.x, VCPU_GP_REG_COUNT);
    TEST_ASSERT_EQUAL_UINT64(regs1.pc, regs2.pc);
    TEST_ASSERT_EQUAL_UINT64(regs1.pstate, regs2.pstate);
}

/**
 * @brief 测试获取/设置 vCPU 寄存器（无效参数）
 */
void test_vcpu_get_set_regs_invalid_params(void)
{
    kernel_status_t ret;
    vcpu_gpregs_t regs;

    /* NULL 指针（获取） */
    ret = vcpu_get_regs(0U, 0U, NULL);
    TEST_ASSERT_LESS_THAN_INT32(0, ret);

    /* NULL 指针（设置） */
    ret = vcpu_set_regs(0U, 0U, NULL);
    TEST_ASSERT_LESS_THAN_INT32(0, ret);
}

/**
 * @brief 测试重置 vCPU
 */
void test_vcpu_reset(void)
{
    kernel_status_t ret;
    vcpu_gpregs_t regs;
    vcpu_state_t state;
    uint32_t vm_id;
    int32_t vcpu_id;

    /* 创建 VM */
    vm_id = vmm_create_vm("test_vm", 0x10000000ULL);
    TEST_ASSERT_GREATER_THAN_INT32(0, (int32_t)vm_id);

    /* 创建 vCPU */
    vcpu_id = vmm_create_vcpu(vm_id, 0x40000000ULL);
    TEST_ASSERT_GREATER_THAN_INT32(-1, vcpu_id);

    /* 修改寄存器 */
    ret = vcpu_get_regs(vm_id, (uint32_t)vcpu_id, &regs);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    regs.x[0] = 0xDEADBEEFDEADBEEFULL;
    ret = vcpu_set_regs(vm_id, (uint32_t)vcpu_id, &regs);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 运行 vCPU */
    ret = vcpu_set_state(vm_id, (uint32_t)vcpu_id, VCPU_STATE_RUNNING);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 重置 vCPU */
    ret = vcpu_reset(vm_id, (uint32_t)vcpu_id);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 检查寄存器是否清零 */
    ret = vcpu_get_regs(vm_id, (uint32_t)vcpu_id, &regs);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    TEST_ASSERT_EQUAL_UINT64(0ULL, regs.x[0]);

    /* 检查状态是否重置为 OFF */
    ret = vcpu_get_state(vm_id, (uint32_t)vcpu_id, &state);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_UINT32(VCPU_STATE_OFF, state);
}

/**
 * @brief 测试重置 vCPU（无效参数）
 */
void test_vcpu_reset_invalid_params(void)
{
    kernel_status_t ret;

    /* 无效 VM ID */
    ret = vcpu_reset(999U, 0U);
    TEST_ASSERT_LESS_THAN_INT32(0, ret);
}

/* ========================================================================
 * 测试主函数
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_vcpu_get_state);
    RUN_TEST(test_vcpu_set_state_valid_transition);
    RUN_TEST(test_vcpu_set_state_invalid_transition);
    RUN_TEST(test_vcpu_set_state_invalid_params);
    RUN_TEST(test_vcpu_save_restore_context);
    RUN_TEST(test_vcpu_save_restore_context_invalid_params);
    RUN_TEST(test_vcpu_get_set_regs);
    RUN_TEST(test_vcpu_get_set_regs_invalid_params);
    RUN_TEST(test_vcpu_reset);
    RUN_TEST(test_vcpu_reset_invalid_params);

    return UNITY_END();
}
