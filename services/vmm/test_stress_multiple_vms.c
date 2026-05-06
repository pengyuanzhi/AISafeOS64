/**
 * @file    test_stress_multiple_vms.c
 * @brief   多 VM 并发压力测试
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 1.0
 *
 * @details 测试多 VM 并发运行的性能和稳定性：
 *          - 4 个 VM 并发运行
 *          - 多 vCPU 并发调度
 *          - 资源竞争测试
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
#include "../../vmm.h"
#include "vmm_stats.h"

/* ========================================================================
 * 测试常量
 * ======================================================================== */

/** @brief 测试的 VM 数量 */
#define STRESS_TEST_VM_COUNT         4U

/** @brief 每个 VM 的 vCPU 数量 */
#define STRESS_TEST_VCPU_PER_VM      4U

/** @brief 总 vCPU 数量 */
#define STRESS_TEST_TOTAL_VCPUS      (STRESS_TEST_VM_COUNT * STRESS_TEST_VCPU_PER_VM)

/** @brief 循环次数 */
#define STRESS_TEST_ITERATIONS       1000U

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
 * 测试用例 - 多 VM 并发运行
 * ======================================================================== */

/**
 * @brief 测试创建并运行 4 个 VM
 */
void test_stress_multi_vm_create_run(void)
{
    vm_desc_t *vms[STRESS_TEST_VM_COUNT];
    kernel_config_t config;
    kernel_status_t ret;
    uint32_t i;

    /* 创建并运行 4 个 VM */
    for (i = 0U; i < STRESS_TEST_VM_COUNT; i++)
    {
        ret = vmm_vm_create(&vms[i]);
        TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

        (void)memset(&config, 0, sizeof(config));
        config.memory_size = 256U;
        config.vcpu_count = STRESS_TEST_VCPU_PER_VM;
        config.vgic_enabled = true;

        ret = vmm_vm_configure(vms[i], &config);
        TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

        ret = vmm_vm_start(vms[i]);
        TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    }

    /* 验证所有 VM 都在运行 */
    for (i = 0U; i < STRESS_TEST_VM_COUNT; i++)
    {
        TEST_ASSERT_EQUAL_INT(VM_STATE_RUNNING, vms[i]->state);
    }

    /* 停止所有 VM */
    for (i = 0U; i < STRESS_TEST_VM_COUNT; i++)
    {
        (void)vmm_vm_stop(vms[i]);
    }

    /* 销毁所有 VM */
    for (i = 0U; i < STRESS_TEST_VM_COUNT; i++)
    {
        (void)vmm_vm_destroy(vms[i]);
    }
}

/* ========================================================================
 * 测试用例 - 多 vCPU 并发调度
 * ======================================================================== */

/**
 * @brief 测试创建并调度 16 个 vCPU
 */
void test_stress_multi_vcpu_schedule(void)
{
    vm_desc_t *vms[STRESS_TEST_VM_COUNT];
    vcpu_desc_t *vcpus[STRESS_TEST_TOTAL_VCPUS];
    kernel_config_t config;
    kernel_status_t ret;
    uint32_t i, j;

    /* 创建并运行 4 个 VM */
    for (i = 0U; i < STRESS_TEST_VM_COUNT; i++)
    {
        ret = vmm_vm_create(&vms[i]);
        TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

        (void)memset(&config, 0, sizeof(config));
        config.memory_size = 256U;
        config.vcpu_count = STRESS_TEST_VCPU_PER_VM;
        config.vgic_enabled = true;

        ret = vmm_vm_configure(vms[i], &config);
        TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

        ret = vmm_vm_start(vms[i]);
        TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    }

    /* 创建 16 个 vCPU */
    for (i = 0U; i < STRESS_TEST_VM_COUNT; i++)
    {
        for (j = 0U; j < STRESS_TEST_VCPU_PER_VM; j++)
        {
            ret = vmm_vcpu_create(vms[i], j, &vcpus[i * STRESS_TEST_VCPU_PER_VM + j]);
            TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
        }
    }

    /* 并发调度所有 vCPU */
    for (i = 0U; i < STRESS_TEST_ITERATIONS; i++)
    {
        for (j = 0U; j < STRESS_TEST_TOTAL_VCPUS; j++)
        {
            ret = vmm_vcpu_schedule(vms[j / STRESS_TEST_VCPU_PER_VM], vcpus[j]);
            TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
        }
    }

    /* 验证所有 vCPU 都在运行 */
    for (j = 0U; j < STRESS_TEST_TOTAL_VCPUS; j++)
    {
        TEST_ASSERT_EQUAL_INT(VCPU_STATE_RUNNING, vcpus[j]->state);
    }

    /* 销毁所有 vCPU */
    for (j = 0U; j < STRESS_TEST_TOTAL_VCPUS; j++)
    {
        (void)vmm_vcpu_destroy(vms[j / STRESS_TEST_VCPU_PER_VM], vcpus[j]);
    }

    /* 停止所有 VM */
    for (i = 0U; i < STRESS_TEST_VM_COUNT; i++)
    {
        (void)vmm_vm_stop(vms[i]);
    }

    /* 销毁所有 VM */
    for (i = 0U; i < STRESS_TEST_VM_COUNT; i++)
    {
        (void)vmm_vm_destroy(vms[i]);
    }
}

/* ========================================================================
 * 测试用例 - vCPU 上下文切换压力测试
 * ======================================================================== */

/**
 * @brief 测试 vCPU 上下文切换压力
 */
void test_stress_context_switch(void)
{
    vm_desc_t *vms[STRESS_TEST_VM_COUNT];
    vcpu_desc_t *vcpus[STRESS_TEST_TOTAL_VCPUS];
    kernel_config_t config;
    kernel_status_t ret;
    uint32_t i, j, k;

    /* 创建并运行 4 个 VM */
    for (i = 0U; i < STRESS_TEST_VM_COUNT; i++)
    {
        ret = vmm_vm_create(&vms[i]);
        TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

        (void)memset(&config, 0, sizeof(config));
        config.memory_size = 256U;
        config.vcpu_count = STRESS_TEST_VCPU_PER_VM;
        config.vgic_enabled = true;

        ret = vmm_vm_configure(vms[i], &config);
        TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

        ret = vmm_vm_start(vms[i]);
        TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    }

    /* 创建 16 个 vCPU */
    for (i = 0U; i < STRESS_TEST_VM_COUNT; i++)
    {
        for (j = 0U; j < STRESS_TEST_VCPU_PER_VM; j++)
        {
            ret = vmm_vcpu_create(vms[i], j, &vcpus[i * STRESS_TEST_VCPU_PER_VM + j]);
            TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
        }
    }

    /* 上下文切换压力测试（10,000 次切换） */
    for (i = 0U; i < 10000U; i++)
    {
        for (j = 0U; j < STRESS_TEST_TOTAL_VCPUS; j++)
        {
            ret = vmm_vcpu_schedule(vms[j / STRESS_TEST_VCPU_PER_VM], vcpus[j]);
            TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
        }
    }

    /* 销毁所有 vCPU */
    for (j = 0U; j < STRESS_TEST_TOTAL_VCPUS; j++)
    {
        (void)vmm_vcpu_destroy(vms[j / STRESS_TEST_VCPU_PER_VM], vcpus[j]);
    }

    /* 停止所有 VM */
    for (i = 0U; i < STRESS_TEST_VM_COUNT; i++)
    {
        (void)vmm_vm_stop(vms[i]);
    }

    /* 销毁所有 VM */
    for (i = 0U; i < STRESS_TEST_VM_COUNT; i++)
    {
        (void)vmm_vm_destroy(vms[i]);
    }
}

/* ========================================================================
 * 测试用例 - 资源竞争测试
 * ======================================================================== */

/**
 * @brief 测试多个 VM 同时访问资源
 */
void test_stress_resource_competition(void)
{
    vm_desc_t *vms[STRESS_TEST_VM_COUNT];
    vcpu_desc_t *vcpus[STRESS_TEST_TOTAL_VCPUS];
    kernel_config_t config;
    kernel_status_t ret;
    uint32_t i, j;

    /* 创建并运行 4 个 VM */
    for (i = 0U; i < STRESS_TEST_VM_COUNT; i++)
    {
        ret = vmm_vm_create(&vms[i]);
        TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

        (void)memset(&config, 0, sizeof(config));
        config.memory_size = 256U;
        config.vcpu_count = STRESS_TEST_VCPU_PER_VM;
        config.vgic_enabled = true;

        ret = vmm_vm_configure(vms[i], &config);
        TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

        ret = vmm_vm_start(vms[i]);
        TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    }

    /* 创建 16 个 vCPU */
    for (i = 0U; i < STRESS_TEST_VM_COUNT; i++)
    {
        for (j = 0U; j < STRESS_TEST_VCPU_PER_VM; j++)
        {
            ret = vmm_vcpu_create(vms[i], j, &vcpus[i * STRESS_TEST_VCPU_PER_VM + j]);
            TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
        }
    }

    /* 并发访问资源 */
    for (i = 0U; i < STRESS_TEST_ITERATIONS; i++)
    {
        for (j = 0U; j < STRESS_TEST_TOTAL_VCPUS; j++)
        {
            /* 每个 vCPU 访问自己的资源 */
            ret = vmm_vcpu_schedule(vms[j / STRESS_TEST_VCPU_PER_VM], vcpus[j]);
            TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
        }
    }

    /* 验证所有 VM 和 vCPU 都正常工作 */
    for (i = 0U; i < STRESS_TEST_VM_COUNT; i++)
    {
        TEST_ASSERT_EQUAL_INT(VM_STATE_RUNNING, vms[i]->state);
    }

    /* 销毁所有 vCPU */
    for (j = 0U; j < STRESS_TEST_TOTAL_VCPUS; j++)
    {
        (void)vmm_vcpu_destroy(vms[j / STRESS_TEST_VCPU_PER_VM], vcpus[j]);
    }

    /* 停止所有 VM */
    for (i = 0U; i < STRESS_TEST_VM_COUNT; i++)
    {
        (void)vmm_vm_stop(vms[i]);
    }

    /* 销毁所有 VM */
    for (i = 0U; i < STRESS_TEST_VM_COUNT; i++)
    {
        (void)vmm_vm_destroy(vms[i]);
    }
}

/* ========================================================================
 * 测试用例 - 性能统计验证
 * ======================================================================== */

/**
 * @brief 测试性能统计正确性
 */
void test_stress_performance_stats(void)
{
    vm_desc_t *vms[STRESS_TEST_VM_COUNT];
    vcpu_desc_t *vcpus[STRESS_TEST_TOTAL_VCPUS];
    kernel_config_t config;
    kernel_status_t ret;
    vmm_perf_stats_t stats;
    uint32_t i, j;

    /* 创建并运行 4 个 VM */
    for (i = 0U; i < STRESS_TEST_VM_COUNT; i++)
    {
        ret = vmm_vm_create(&vms[i]);
        TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

        (void)memset(&config, 0, sizeof(config));
        config.memory_size = 256U;
        config.vcpu_count = STRESS_TEST_VCPU_PER_VM;
        config.vgic_enabled = true;

        ret = vmm_vm_configure(vms[i], &config);
        TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

        ret = vmm_vm_start(vms[i]);
        TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    }

    /* 创建 16 个 vCPU */
    for (i = 0U; i < STRESS_TEST_VM_COUNT; i++)
    {
        for (j = 0U; j < STRESS_TEST_VCPU_PER_VM; j++)
        {
            ret = vmm_vcpu_create(vms[i], j, &vcpus[i * STRESS_TEST_VCPU_PER_VM + j]);
            TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
        }
    }

    /* 并发调度所有 vCPU */
    for (i = 0U; i < 1000U; i++)
    {
        for (j = 0U; j < STRESS_TEST_TOTAL_VCPUS; j++)
        {
            (void)vmm_vcpu_schedule(vms[j / STRESS_TEST_VCPU_PER_VM], vcpus[j]);
        }
    }

    /* 获取性能统计 */
    ret = vmm_perf_get_stats(&stats);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 验证统计信息 */
    TEST_ASSERT_GREATER_THAN(0U, stats.vcpu_schedule_count);
    TEST_ASSERT_GREATER_THAN(0U, stats.context_switches);

    /* 销毁所有 vCPU */
    for (j = 0U; j < STRESS_TEST_TOTAL_VCPUS; j++)
    {
        (void)vmm_vcpu_destroy(vms[j / STRESS_TEST_VCPU_PER_VM], vcpus[j]);
    }

    /* 停止所有 VM */
    for (i = 0U; i < STRESS_TEST_VM_COUNT; i++)
    {
        (void)vmm_vm_stop(vms[i]);
    }

    /* 销毁所有 VM */
    for (i = 0U; i < STRESS_TEST_VM_COUNT; i++)
    {
        (void)vmm_vm_destroy(vms[i]);
    }
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    /* 测试用例 - 多 VM 并发运行 */
    RUN_TEST(test_stress_multi_vm_create_run);

    /* 测试用例 - 多 vCPU 并发调度 */
    RUN_TEST(test_stress_multi_vcpu_schedule);

    /* 测试用例 - vCPU 上下文切换压力测试 */
    RUN_TEST(test_stress_context_switch);

    /* 测试用例 - 资源竞争测试 */
    RUN_TEST(test_stress_resource_competition);

    /* 测试用例 - 性能统计验证 */
    RUN_TEST(test_stress_performance_stats);

    return UNITY_END();
}
