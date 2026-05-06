/**
 * @file    test_integration_vcpu.c
 * @brief   vCPU 集成测试
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 1.0
 *
 * @details 测试 vCPU 的以下功能：
 *          - vCPU 创建/销毁
 *          - vCPU 调度
 *          - vCPU 上下文切换
 *          - vCPU 状态管理
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
#include "vmm.h"
#include "stats/vmm_stats.h"

/* ========================================================================
 * 测试常量
 * ======================================================================== */

/** @brief 测试 VM ID */
#define TEST_VM_ID                   0U

/** @brief 测试 vCPU ID */
#define TEST_VCPU_ID                 0U

/* ========================================================================
 * 测试夹具
 * ======================================================================== */

/**
 * @brief 设置测试环境
 */
void setUp(void)
{
    /* 清空统计信息 */
    (void)vmm_stats_reset();
}

/**
 * @brief 清理测试环境
 */
void tearDown(void)
{
    /* 清空统计信息 */
    (void)vmm_stats_reset();
}

/* ========================================================================
 * 测试用例 - vCPU 创建/销毁
 * ======================================================================== */

/**
 * @brief 测试 vCPU 创建
 */
void test_vcpu_create(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 配置 VM */
    (void)vmm_vm_configure(vm, &(kernel_config_t){.vcpu_count = 4U});

    /* 创建 vCPU */
    ret = vmm_vcpu_create(vm, TEST_VCPU_ID, &vcpu);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    TEST_ASSERT_NOT_NULL(vcpu);
    TEST_ASSERT_EQUAL_INT(TEST_VCPU_ID, vcpu->vcpu_id);
    TEST_ASSERT_EQUAL_INT(vm->vm_id, vcpu->vm_id);

    /* 销毁 vCPU */
    ret = vmm_vcpu_destroy(vm, vcpu);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/**
 * @brief 测试 vCPU 获取
 */
void test_vcpu_get(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    vcpu_desc_t *vcpu_get;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 配置 VM */
    (void)vmm_vm_configure(vm, &(kernel_config_t){.vcpu_count = 4U});

    /* 创建 vCPU */
    ret = vmm_vcpu_create(vm, TEST_VCPU_ID, &vcpu);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 获取 vCPU */
    ret = vmm_vcpu_get(vm, TEST_VCPU_ID, &vcpu_get);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_PTR(vcpu, vcpu_get);

    /* 销毁 vCPU */
    (void)vmm_vcpu_destroy(vm, vcpu);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/**
 * @brief 测试创建多个 vCPU
 */
void test_vcpu_create_multiple(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpus[4U];
    kernel_status_t ret;
    uint32_t i;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 配置 VM（4 个 vCPU） */
    (void)vmm_vm_configure(vm, &(kernel_config_t){.vcpu_count = 4U});

    /* 创建 4 个 vCPU */
    for (i = 0U; i < 4U; i++)
    {
        ret = vmm_vcpu_create(vm, i, &vcpus[i]);
        TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
        TEST_ASSERT_NOT_NULL(vcpus[i]);
        TEST_ASSERT_EQUAL_INT(i, vcpus[i]->vcpu_id);
    }

    /* 销毁所有 vCPU */
    for (i = 0U; i < 4U; i++)
    {
        (void)vmm_vcpu_destroy(vm, vcpus[i]);
    }

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/* ========================================================================
 * 测试用例 - vCPU 调度
 * ======================================================================== */

/**
 * @brief 测试 vCPU 调度
 */
void test_vcpu_schedule(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 配置 VM */
    (void)vmm_vm_configure(vm, &(kernel_config_t){.vcpu_count = 4U});

    /* 创建 vCPU */
    ret = vmm_vcpu_create(vm, TEST_VCPU_ID, &vcpu);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 调度 vCPU */
    ret = vmm_vcpu_schedule(vm, vcpu);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 验证 vCPU 状态 */
    TEST_ASSERT_EQUAL_INT(VCPU_STATE_RUNNING, vcpu->state);

    /* 销毁 vCPU */
    (void)vmm_vcpu_destroy(vm, vcpu);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/**
 * @brief 测试 vCPU 调度多次
 */
void test_vcpu_schedule_multiple(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 配置 VM */
    (void)vmm_vm_configure(vm, &(kernel_config_t){.vcpu_count = 4U});

    /* 创建 vCPU */
    ret = vmm_vcpu_create(vm, TEST_VCPU_ID, &vcpu);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 调度 vCPU 3 次 */
    for (uint32_t i = 0U; i < 3U; i++)
    {
        ret = vmm_vcpu_schedule(vm, vcpu);
        TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
        TEST_ASSERT_EQUAL_INT(VCPU_STATE_RUNNING, vcpu->state);
    }

    /* 销毁 vCPU */
    (void)vmm_vcpu_destroy(vm, vcpu);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/* ========================================================================
 * 测试用例 - vCPU 上下文切换
 * ======================================================================== */

/**
 * @brief 测试 vCPU 上下文切换
 */
void test_vcpu_context_switch(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu0;
    vcpu_desc_t *vcpu1;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 配置 VM（2 个 vCPU） */
    (void)vmm_vm_configure(vm, &(kernel_config_t){.vcpu_count = 2U});

    /* 创建 2 个 vCPU */
    ret = vmm_vcpu_create(vm, 0U, &vcpu0);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    ret = vmm_vcpu_create(vm, 1U, &vcpu1);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 调度 vCPU 0 */
    ret = vmm_vcpu_schedule(vm, vcpu0);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_INT(VCPU_STATE_RUNNING, vcpu0->state);

    /* 上下文切换到 vCPU 1 */
    ret = vmm_vcpu_schedule(vm, vcpu1);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_INT(VCPU_STATE_RUNNING, vcpu1->state);
    TEST_ASSERT_EQUAL_INT(VCPU_STATE_READY, vcpu0->state);

    /* 上下文切换回 vCPU 0 */
    ret = vmm_vcpu_schedule(vm, vcpu0);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_INT(VCPU_STATE_RUNNING, vcpu0->state);
    TEST_ASSERT_EQUAL_INT(VCPU_STATE_READY, vcpu1->state);

    /* 销毁 vCPU */
    (void)vmm_vcpu_destroy(vm, vcpu0);
    (void)vmm_vcpu_destroy(vm, vcpu1);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/**
 * @brief 测试 4 个 vCPU 上下文切换
 */
void test_vcpu_context_switch_4(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpus[4U];
    kernel_status_t ret;
    uint32_t i;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 配置 VM（4 个 vCPU） */
    (void)vmm_vm_configure(vm, &(kernel_config_t){.vcpu_count = 4U});

    /* 创建 4 个 vCPU */
    for (i = 0U; i < 4U; i++)
    {
        ret = vmm_vcpu_create(vm, i, &vcpus[i]);
        TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    }

    /* 4 个 vCPU 交替调度 */
    for (i = 0U; i < 8U; i++)
    {
        ret = vmm_vcpu_schedule(vm, vcpus[i % 4U]);
        TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    }

    /* 销毁所有 vCPU */
    for (i = 0U; i < 4U; i++)
    {
        (void)vmm_vcpu_destroy(vm, vcpus[i]);
    }

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/* ========================================================================
 * 测试用例 - vCPU 状态管理
 * ======================================================================== */

/**
 * @brief 测试 vCPU 状态检查
 */
void test_vcpu_state_check(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 配置 VM */
    (void)vmm_vm_configure(vm, &(kernel_config_t){.vcpu_count = 4U});

    /* 创建 vCPU */
    ret = vmm_vcpu_create(vm, TEST_VCPU_ID, &vcpu);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 检查初始状态（应该是 READY） */
    TEST_ASSERT_EQUAL_INT(VCPU_STATE_READY, vcpu->state);

    /* 调度 vCPU */
    ret = vmm_vcpu_schedule(vm, vcpu);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 检查状态（应该是 RUNNING） */
    TEST_ASSERT_EQUAL_INT(VCPU_STATE_RUNNING, vcpu->state);

    /* 销毁 vCPU */
    (void)vmm_vcpu_destroy(vm, vcpu);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/**
 * @brief 测试 vCPU 暂停
 */
void test_vcpu_pause(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 配置 VM */
    (void)vmm_vm_configure(vm, &(kernel_config_t){.vcpu_count = 4U});

    /* 创建 vCPU */
    ret = vmm_vcpu_create(vm, TEST_VCPU_ID, &vcpu);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 调度 vCPU */
    ret = vmm_vcpu_schedule(vm, vcpu);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 暂停 vCPU */
    ret = vmm_vcpu_pause(vm, vcpu);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 检查状态（应该是 PAUSED） */
    TEST_ASSERT_EQUAL_INT(VCPU_STATE_PAUSED, vcpu->state);

    /* 销毁 vCPU */
    (void)vmm_vcpu_destroy(vm, vcpu);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/**
 * @brief 测试 vCPU 恢复
 */
void test_vcpu_resume(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 配置 VM */
    (void)vmm_vm_configure(vm, &(kernel_config_t){.vcpu_count = 4U});

    /* 创建 vCPU */
    ret = vmm_vcpu_create(vm, TEST_VCPU_ID, &vcpu);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 调度 vCPU */
    ret = vmm_vcpu_schedule(vm, vcpu);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 暂停 vCPU */
    ret = vmm_vcpu_pause(vm, vcpu);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 恢复 vCPU */
    ret = vmm_vcpu_resume(vm, vcpu);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 检查状态（应该是 RUNNING） */
    TEST_ASSERT_EQUAL_INT(VCPU_STATE_RUNNING, vcpu->state);

    /* 销毁 vCPU */
    (void)vmm_vcpu_destroy(vm, vcpu);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/* ========================================================================
 * 测试用例 - 边界条件
 * ======================================================================== */

/**
 * @brief 测试创建无效的 vCPU ID
 */
void test_vcpu_create_invalid_id(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 配置 VM */
    (void)vmm_vm_configure(vm, &(kernel_config_t){.vcpu_count = 4U});

    /* 尝试创建无效的 vCPU ID */
    ret = vmm_vcpu_create(vm, 10U, &vcpu);
    TEST_ASSERT_EQUAL_INT(-(int32_t)EINVAL, ret);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/**
 * @brief 测试调度不存在的 vCPU
 */
void test_vcpu_schedule_invalid(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 配置 VM */
    (void)vmm_vm_configure(vm, &(kernel_config_t){.vcpu_count = 4U});

    /* 尝试调度不存在的 vCPU */
    ret = vmm_vcpu_schedule(vm, NULL);
    TEST_ASSERT_EQUAL_INT(-(int32_t)EINVAL, ret);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/**
 * @brief 测试暂停不存在的 vCPU
 */
void test_vcpu_pause_invalid(void)
{
    vm_desc_t *vm;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 配置 VM */
    (void)vmm_vm_configure(vm, &(kernel_config_t){.vcpu_count = 4U});

    /* 尝试暂停不存在的 vCPU */
    ret = vmm_vcpu_pause(vm, NULL);
    TEST_ASSERT_EQUAL_INT(-(int32_t)EINVAL, ret);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/**
 * @brief 测试恢复不存在的 vCPU
 */
void test_vcpu_resume_invalid(void)
{
    vm_desc_t *vm;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 配置 VM */
    (void)vmm_vm_configure(vm, &(kernel_config_t){.vcpu_count = 4U});

    /* 尝试恢复不存在的 vCPU */
    ret = vmm_vcpu_resume(vm, NULL);
    TEST_ASSERT_EQUAL_INT(-(int32_t)EINVAL, ret);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    /* 测试用例 - vCPU 创建/销毁 */
    RUN_TEST(test_vcpu_create);
    RUN_TEST(test_vcpu_get);
    RUN_TEST(test_vcpu_create_multiple);

    /* 测试用例 - vCPU 调度 */
    RUN_TEST(test_vcpu_schedule);
    RUN_TEST(test_vcpu_schedule_multiple);

    /* 测试用例 - vCPU 上下文切换 */
    RUN_TEST(test_vcpu_context_switch);
    RUN_TEST(test_vcpu_context_switch_4);

    /* 测试用例 - vCPU 状态管理 */
    RUN_TEST(test_vcpu_state_check);
    RUN_TEST(test_vcpu_pause);
    RUN_TEST(test_vcpu_resume);

    /* 测试用例 - 边界条件 */
    RUN_TEST(test_vcpu_create_invalid_id);
    RUN_TEST(test_vcpu_schedule_invalid);
    RUN_TEST(test_vcpu_pause_invalid);
    RUN_TEST(test_vcpu_resume_invalid);

    return UNITY_END();
}
