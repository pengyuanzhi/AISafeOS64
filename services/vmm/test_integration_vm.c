/**
 * @file    test_integration_vm.c
 * @brief   VM 集成测试
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 1.0
 *
 * @details 测试 VM 的以下功能：
 *          - VM 创建/销毁
 *          - VM 启动/停止
 *          - VM 暂停/恢复
 *          - VM 配置管理
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

/** @brief 测试 vCPU 数量 */
#define TEST_VCPU_COUNT              4U

/** @brief 测试内存大小 */
#define TEST_MEMORY_SIZE             512U  /* 512 MB */

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
 * 测试用例 - VM 创建/销毁
 * ======================================================================== */

/**
 * @brief 测试 VM 创建
 */
void test_vm_create(void)
{
    vm_desc_t *vm;
    kernel_status_t ret;

    /* 测试创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    TEST_ASSERT_NOT_NULL(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, vm->vm_id);

    /* 测试销毁 VM */
    ret = vmm_vm_destroy(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
}

/**
 * @brief 测试创建多个 VM
 */
void test_vm_create_multiple(void)
{
    vm_desc_t *vms[VMM_MAX_VMS];
    kernel_status_t ret;
    uint32_t i;

    /* 测试创建多个 VM */
    for (i = 0U; i < VMM_MAX_VMS; i++)
    {
        ret = vmm_vm_create(&vms[i]);
        TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
        TEST_ASSERT_NOT_NULL(vms[i]);
    }

    /* 测试销毁所有 VM */
    for (i = 0U; i < VMM_MAX_VMS; i++)
    {
        ret = vmm_vm_destroy(vms[i]);
        TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    }
}

/**
 * @brief 测试创建 VM 后获取 VM
 */
void test_vm_get_after_create(void)
{
    vm_desc_t *vm;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 获取 VM */
    vm_desc_t *vm_get;
    ret = vmm_vm_get(vm->vm_id, &vm_get);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_PTR(vm, vm_get);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/* ========================================================================
 * 测试用例 - VM 启动/停止
 * ======================================================================== */

/**
 * @brief 测试 VM 启动
 */
void test_vm_start(void)
{
    vm_desc_t *vm;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 启动 VM */
    ret = vmm_vm_start(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 验证 VM 状态 */
    TEST_ASSERT_EQUAL_INT(VM_STATE_RUNNING, vm->state);

    /* 停止 VM */
    ret = vmm_vm_stop(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 验证 VM 状态 */
    TEST_ASSERT_EQUAL_INT(VM_STATE_STOPPED, vm->state);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/**
 * @brief 测试 VM 停止
 */
void test_vm_stop(void)
{
    vm_desc_t *vm;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 启动 VM */
    ret = vmm_vm_start(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 停止 VM */
    ret = vmm_vm_stop(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 验证 VM 状态 */
    TEST_ASSERT_EQUAL_INT(VM_STATE_STOPPED, vm->state);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/**
 * @brief 测试 VM 启动后再次启动
 */
void test_vm_start_after_stop(void)
{
    vm_desc_t *vm;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 启动 VM */
    ret = vmm_vm_start(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 停止 VM */
    ret = vmm_vm_stop(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 再次启动 VM */
    ret = vmm_vm_start(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 验证 VM 状态 */
    TEST_ASSERT_EQUAL_INT(VM_STATE_RUNNING, vm->state);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/**
 * @brief 测试 VM 停止后再停止
 */
void test_vm_stop_after_stop(void)
{
    vm_desc_t *vm;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 启动 VM */
    ret = vmm_vm_start(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 停止 VM */
    ret = vmm_vm_stop(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 再次停止 VM */
    ret = vmm_vm_stop(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 验证 VM 状态 */
    TEST_ASSERT_EQUAL_INT(VM_STATE_STOPPED, vm->state);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/* ========================================================================
 * 测试用例 - VM 暂停/恢复
 * ======================================================================== */

/**
 * @brief 测试 VM 暂停
 */
void test_vm_pause(void)
{
    vm_desc_t *vm;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 启动 VM */
    ret = vmm_vm_start(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 暂停 VM */
    ret = vmm_vm_pause(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 验证 VM 状态 */
    TEST_ASSERT_EQUAL_INT(VM_STATE_PAUSED, vm->state);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/**
 * @brief 测试 VM 恢复
 */
void test_vm_resume(void)
{
    vm_desc_t *vm;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 启动 VM */
    ret = vmm_vm_start(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 暂停 VM */
    ret = vmm_vm_pause(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 恢复 VM */
    ret = vmm_vm_resume(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 验证 VM 状态 */
    TEST_ASSERT_EQUAL_INT(VM_STATE_RUNNING, vm->state);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/**
 * @brief 测试 VM 暂停后再次暂停
 */
void test_vm_pause_after_pause(void)
{
    vm_desc_t *vm;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 启动 VM */
    ret = vmm_vm_start(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 暂停 VM */
    ret = vmm_vm_pause(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 再次暂停 VM */
    ret = vmm_vm_pause(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 验证 VM 状态 */
    TEST_ASSERT_EQUAL_INT(VM_STATE_PAUSED, vm->state);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/**
 * @brief 测试 VM 恢复后再恢复
 */
void test_vm_resume_after_resume(void)
{
    vm_desc_t *vm;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 启动 VM */
    ret = vmm_vm_start(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 暂停 VM */
    ret = vmm_vm_pause(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 恢复 VM */
    ret = vmm_vm_resume(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 再次恢复 VM */
    ret = vmm_vm_resume(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 验证 VM 状态 */
    TEST_ASSERT_EQUAL_INT(VM_STATE_RUNNING, vm->state);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/* ========================================================================
 * 测试用例 - VM 配置管理
 * ======================================================================== */

/**
 * @brief 测试 VM 配置
 */
void test_vm_configure(void)
{
    vm_desc_t *vm;
    kernel_config_t config;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 配置 VM */
    (void)memset(&config, 0, sizeof(config));
    config.memory_size = 512U;
    config.vcpu_count = 4U;
    config.vgic_enabled = true;

    ret = vmm_vm_configure(vm, &config);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 验证配置 */
    TEST_ASSERT_EQUAL_INT(512U, vm->memory_size);
    TEST_ASSERT_EQUAL_INT(4U, vm->vcpu_count);
    TEST_ASSERT_EQUAL_INT(true, vm->vgic_enabled);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/**
 * @brief 测试 VM 配置后启动
 */
void test_vm_configure_and_start(void)
{
    vm_desc_t *vm;
    kernel_config_t config;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 配置 VM */
    (void)memset(&config, 0, sizeof(config));
    config.memory_size = 256U;
    config.vcpu_count = 2U;
    config.vgic_enabled = true;

    ret = vmm_vm_configure(vm, &config);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 启动 VM */
    ret = vmm_vm_start(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 验证 VM 状态 */
    TEST_ASSERT_EQUAL_INT(VM_STATE_RUNNING, vm->state);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/* ========================================================================
 * 测试用例 - 边界条件
 * ======================================================================== */

/**
 * @brief 测试创建最大数量的 VM
 */
void test_vm_create_max(void)
{
    vm_desc_t *vms[VMM_MAX_VMS];
    kernel_status_t ret;
    uint32_t i;

    /* 创建最大数量的 VM */
    for (i = 0U; i < VMM_MAX_VMS; i++)
    {
        ret = vmm_vm_create(&vms[i]);
        TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    }

    /* 尝试创建第 VMM_MAX_VMS 个 VM（应该失败） */
    ret = vmm_vm_create(NULL);
    TEST_ASSERT_EQUAL_INT(-(int32_t)ENOBUFS, ret);
}

/**
 * @brief 测试获取不存在的 VM
 */
void test_vm_get_invalid(void)
{
    vm_desc_t *vm;
    kernel_status_t ret;

    /* 尝试获取不存在的 VM */
    ret = vmm_vm_get(0xFFFFFFFFU, &vm);
    TEST_ASSERT_EQUAL_INT(-(int32_t)ENOENT, ret);
}

/**
 * @brief 测试销毁不存在的 VM
 */
void test_vm_destroy_invalid(void)
{
    kernel_status_t ret;

    /* 尝试销毁不存在的 VM */
    ret = vmm_vm_destroy(NULL);
    TEST_ASSERT_EQUAL_INT(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试启动已启动的 VM
 */
void test_vm_start_running(void)
{
    vm_desc_t *vm;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 启动 VM */
    ret = vmm_vm_start(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 再次启动 VM */
    ret = vmm_vm_start(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 验证 VM 状态 */
    TEST_ASSERT_EQUAL_INT(VM_STATE_RUNNING, vm->state);

    /* 销毁 VM */
    (void)vmm_vm_destroy(vm);
}

/**
 * @brief 测试暂停已暂停的 VM
 */
void test_vm_pause_paused(void)
{
    vm_desc_t *vm;
    kernel_status_t ret;

    /* 创建 VM */
    ret = vmm_vm_create(&vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 启动 VM */
    ret = vmm_vm_start(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 暂停 VM */
    ret = vmm_vm_pause(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 再次暂停 VM */
    ret = vmm_vm_pause(vm);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 验证 VM 状态 */
    TEST_ASSERT_EQUAL_INT(VM_STATE_PAUSED, vm->state);

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

    /* 测试用例 - VM 创建/销毁 */
    RUN_TEST(test_vm_create);
    RUN_TEST(test_vm_create_multiple);
    RUN_TEST(test_vm_get_after_create);

    /* 测试用例 - VM 启动/停止 */
    RUN_TEST(test_vm_start);
    RUN_TEST(test_vm_stop);
    RUN_TEST(test_vm_start_after_stop);
    RUN_TEST(test_vm_stop_after_stop);

    /* 测试用例 - VM 暂停/恢复 */
    RUN_TEST(test_vm_pause);
    RUN_TEST(test_vm_resume);
    RUN_TEST(test_vm_pause_after_pause);
    RUN_TEST(test_vm_resume_after_resume);

    /* 测试用例 - VM 配置管理 */
    RUN_TEST(test_vm_configure);
    RUN_TEST(test_vm_configure_and_start);

    /* 测试用例 - 边界条件 */
    RUN_TEST(test_vm_create_max);
    RUN_TEST(test_vm_get_invalid);
    RUN_TEST(test_vm_destroy_invalid);
    RUN_TEST(test_vm_start_running);
    RUN_TEST(test_vm_pause_paused);

    return UNITY_END();
}
