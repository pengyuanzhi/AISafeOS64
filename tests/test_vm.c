/**
 * @file    test_vm.c
 * @brief   VM 生命周期管理单元测试
 * @author  AISafe64 Team
 * @date    2026-05-03
 * @version 1.0
 *
 * @details 测试 VM 生命周期管理的以下功能：
 *          - 状态管理
 *          - VM 启动/停止
 *          - VM 信息查询
 *          - VM 信息转储
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
#include "vm.h"
#include "vmm.h"

/* ========================================================================
 * 测试常量
 * ======================================================================== */

#define TEST_VM_NAME  "test_vm"
#define TEST_MEM_SIZE (0x10000000ULL)  /* 256 MB */

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
 * @brief 测试获取 VM 状态
 */
void test_vm_get_state(void)
{
    kernel_status_t ret;
    vm_state_t state;
    uint32_t vm_id;

    /* 创建 VM */
    vm_id = vmm_create_vm(TEST_VM_NAME, TEST_MEM_SIZE);
    TEST_ASSERT_GREATER_THAN_INT32(0, (int32_t)vm_id);

    /* 获取 VM 状态 */
    ret = vm_get_state(vm_id, &state);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_UINT32(VM_STATE_CREATED, state);
}

/**
 * @brief 测试设置 VM 状态（合法转换）
 */
void test_vm_set_state_valid_transition(void)
{
    kernel_status_t ret;
    vm_state_t state;
    uint32_t vm_id;

    /* 创建 VM */
    vm_id = vmm_create_vm(TEST_VM_NAME, TEST_MEM_SIZE);
    TEST_ASSERT_GREATER_THAN_INT32(0, (int32_t)vm_id);

    /* CREATED → RUNNING */
    ret = vm_set_state(vm_id, VM_STATE_RUNNING);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    ret = vm_get_state(vm_id, &state);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_UINT32(VM_STATE_RUNNING, state);

    /* RUNNING → PAUSED */
    ret = vm_set_state(vm_id, VM_STATE_PAUSED);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    ret = vm_get_state(vm_id, &state);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_UINT32(VM_STATE_PAUSED, state);
}

/**
 * @brief 测试设置 VM 状态（非法转换）
 */
void test_vm_set_state_invalid_transition(void)
{
    kernel_status_t ret;
    uint32_t vm_id;

    /* 创建 VM */
    vm_id = vmm_create_vm(TEST_VM_NAME, TEST_MEM_SIZE);
    TEST_ASSERT_GREATER_THAN_INT32(0, (int32_t)vm_id);

    /* CREATED → STOPPED (合法) */
    ret = vm_set_state(vm_id, VM_STATE_STOPPED);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* STOPPED → RUNNING (非法) */
    ret = vm_set_state(vm_id, VM_STATE_RUNNING);
    TEST_ASSERT_LESS_THAN_INT32(0, ret);
}

/**
 * @brief 测试设置 VM 状态（无效参数）
 */
void test_vm_set_state_invalid_params(void)
{
    kernel_status_t ret;

    /* 无效 VM ID */
    ret = vm_set_state(999U, VM_STATE_RUNNING);
    TEST_ASSERT_LESS_THAN_INT32(0, ret);
}

/**
 * @brief 测试启动 VM
 */
void test_vm_start(void)
{
    kernel_status_t ret;
    vm_state_t state;
    vcpu_state_t vcpu_state;
    uint32_t vm_id;
    int32_t vcpu_id;

    /* 创建 VM */
    vm_id = vmm_create_vm(TEST_VM_NAME, TEST_MEM_SIZE);
    TEST_ASSERT_GREATER_THAN_INT32(0, (int32_t)vm_id);

    /* 创建 vCPU */
    vcpu_id = vmm_create_vcpu(vm_id, 0x40000000ULL);
    TEST_ASSERT_GREATER_THAN_INT32(-1, vcpu_id);

    /* 启动 VM */
    ret = vm_start(vm_id);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 检查 VM 状态 */
    ret = vm_get_state(vm_id, &state);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_UINT32(VM_STATE_RUNNING, state);

    /* 检查 vCPU 状态 */
    ret = vcpu_get_state(vm_id, (uint32_t)vcpu_id, &vcpu_state);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_UINT32(VCPU_STATE_RUNNING, vcpu_state);
}

/**
 * @brief 测试启动 VM（无效参数）
 */
void test_vm_start_invalid_params(void)
{
    kernel_status_t ret;

    /* 无效 VM ID */
    ret = vm_start(999U);
    TEST_ASSERT_LESS_THAN_INT32(0, ret);
}

/**
 * @brief 测试停止 VM
 */
void test_vm_stop(void)
{
    kernel_status_t ret;
    vm_state_t state;
    vcpu_state_t vcpu_state;
    uint32_t vm_id;
    int32_t vcpu_id;

    /* 创建 VM */
    vm_id = vmm_create_vm(TEST_VM_NAME, TEST_MEM_SIZE);
    TEST_ASSERT_GREATER_THAN_INT32(0, (int32_t)vm_id);

    /* 创建 vCPU */
    vcpu_id = vmm_create_vcpu(vm_id, 0x40000000ULL);
    TEST_ASSERT_GREATER_THAN_INT32(-1, vcpu_id);

    /* 启动 VM */
    ret = vm_start(vm_id);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 停止 VM */
    ret = vm_stop(vm_id);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 检查 VM 状态 */
    ret = vm_get_state(vm_id, &state);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_UINT32(VM_STATE_STOPPED, state);

    /* 检查 vCPU 状态 */
    ret = vcpu_get_state(vm_id, (uint32_t)vcpu_id, &vcpu_state);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_UINT32(VCPU_STATE_STOPPED, vcpu_state);
}

/**
 * @brief 测试停止 VM（无效参数）
 */
void test_vm_stop_invalid_params(void)
{
    kernel_status_t ret;

    /* 无效 VM ID */
    ret = vm_stop(999U);
    TEST_ASSERT_LESS_THAN_INT32(0, ret);
}

/**
 * @brief 测试获取 VM 信息
 */
void test_vm_get_info(void)
{
    kernel_status_t ret;
    vm_info_t info;
    uint32_t vm_id;

    /* 创建 VM */
    vm_id = vmm_create_vm(TEST_VM_NAME, TEST_MEM_SIZE);
    TEST_ASSERT_GREATER_THAN_INT32(0, (int32_t)vm_id);

    /* 创建 vCPU */
    vmm_create_vcpu(vm_id, 0x40000000ULL);

    /* 获取 VM 信息 */
    ret = vm_get_info(vm_id, &info);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 检查 VM 信息 */
    TEST_ASSERT_EQUAL_UINT32(vm_id, info.vm_id);
    TEST_ASSERT_EQUAL_UINT32(VM_STATE_CREATED, info.state);
    TEST_ASSERT_TRUE(info.active == false);
    TEST_ASSERT_EQUAL_STRING(TEST_VM_NAME, info.name);
    TEST_ASSERT_EQUAL_UINT64(TEST_MEM_SIZE, info.mem_size);
    TEST_ASSERT_GREATER_THAN_UINT32(0U, info.vcpu_count);
}

/**
 * @brief 测试获取 VM 信息（无效参数）
 */
void test_vm_get_info_invalid_params(void)
{
    kernel_status_t ret;
    vm_info_t info;

    /* NULL 指针 */
    ret = vm_get_info(0U, NULL);
    TEST_ASSERT_LESS_THAN_INT32(0, ret);

    /* 无效 VM ID */
    ret = vm_get_info(999U, &info);
    TEST_ASSERT_LESS_THAN_INT32(0, ret);
}

/**
 * @brief 测试转储 VM 信息
 */
void test_vm_dump(void)
{
    kernel_status_t ret;
    uint32_t vm_id;
    int32_t vcpu_id;

    /* 创建 VM */
    vm_id = vmm_create_vm(TEST_VM_NAME, TEST_MEM_SIZE);
    TEST_ASSERT_GREATER_THAN_INT32(0, (int32_t)vm_id);

    /* 创建 vCPU */
    vcpu_id = vmm_create_vcpu(vm_id, 0x40000000ULL);
    TEST_ASSERT_GREATER_THAN_INT32(-1, vcpu_id);

    /* 启动 VM */
    vm_start(vm_id);

    /* 转储 VM 信息 */
    ret = vm_dump(vm_id);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
}

/**
 * @brief 测试转储 VM 信息（无效参数）
 */
void test_vm_dump_invalid_params(void)
{
    kernel_status_t ret;

    /* 无效 VM ID */
    ret = vm_dump(999U);
    TEST_ASSERT_LESS_THAN_INT32(0, ret);
}

/* ========================================================================
 * 测试主函数
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_vm_get_state);
    RUN_TEST(test_vm_set_state_valid_transition);
    RUN_TEST(test_vm_set_state_invalid_transition);
    RUN_TEST(test_vm_set_state_invalid_params);
    RUN_TEST(test_vm_start);
    RUN_TEST(test_vm_start_invalid_params);
    RUN_TEST(test_vm_stop);
    RUN_TEST(test_vm_stop_invalid_params);
    RUN_TEST(test_vm_get_info);
    RUN_TEST(test_vm_get_info_invalid_params);
    RUN_TEST(test_vm_dump);
    RUN_TEST(test_vm_dump_invalid_params);

    return UNITY_END();
}
