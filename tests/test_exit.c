/**
 * @file    test_exit.c
 * @brief   VM 退出处理单元测试
 * @author  AISafe64 Team
 * @date    2026-05-03
 * @version 1.0
 *
 * @details 测试 VM 退出处理的以下功能：
 *          - WFI/WFE 退出处理
 *          - HVC (Hypercall) 退出处理
 *          - MMIO 退出处理
 *          - 系统寄存器退出处理
 *          - 指令中止退出处理
 *          - VM 退出分发器
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
#include "exit.h"
#include "vmm.h"
#include "hypercall.h"
#include "npt.h"
#include "vm.h"
#include "vmm_stats.h"

/* ========================================================================
 * 测试常量
 * ======================================================================== */

#define TEST_VM_ID             (0U)
#define TEST_VCPU_ID           (0U)
#define TEST_MMIO_BASE         (0x09000000ULL)

/* ========================================================================
 * 测试夹具
 * ======================================================================== */

void setUp(void)
{
    /* 每个 test 前执行 */
}

void tearDown(void)
{
    /* 每个 test 后执行 */
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 测试 WFI/WFE 退出处理（有中断）
 */
void test_exit_wfi_wfe_with_irq(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    uint64_t original_state;

    /* 创建 VM */
    vm = vmm_create_vm(TEST_VM_ID, TEST_MMIO_BASE);
    TEST_ASSERT_NOT_NULL(vm);

    /* 创建 vCPU */
    kernel_status_t ret = vmm_create_vcpu(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    vcpu = &vm->vcpus[TEST_VCPU_ID];

    /* 保存原始状态 */
    original_state = vcpu->state;

    /* 注入中断 */
    vcpu->irq_pending = true;

    /* 处理 WFI/WFE 退出 */
    ret = vmm_handle_exit(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* vCPU 应该保持 RUNNING 状态（因为有待注入中断） */
    TEST_ASSERT_EQUAL_INT32(VCPU_STATE_RUNNING, vcpu->state);

    /* 清除中断 */
    vcpu->irq_pending = false;
    vcpu->state = original_state;

    /* 销毁 vCPU */
    vmm_destroy_vcpu(TEST_VM_ID, TEST_VCPU_ID);
}

/**
 * @brief 测试 WFI/WFE 退出处理（无中断）
 */
void test_exit_wfi_wfe_without_irq(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    uint64_t original_state;
    uint64_t exit_count;

    /* 创建 VM */
    vm = vmm_create_vm(TEST_VM_ID, TEST_MMIO_BASE);
    TEST_ASSERT_NOT_NULL(vm);

    /* 创建 vCPU */
    kernel_status_t ret = vmm_create_vcpu(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    vcpu = &vm->vcpus[TEST_VCPU_ID];

    /* 保存原始状态和退出计数 */
    original_state = vcpu->state;
    exit_count = vcpu->exit_count;

    /* 清除中断 */
    vcpu->irq_pending = false;

    /* 处理 WFI/WFE 退出 */
    ret = vmm_handle_exit(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* vCPU 应该进入 BLOCKED 状态 */
    TEST_ASSERT_EQUAL_INT32(VCPU_STATE_BLOCKED, vcpu->state);

    /* 退出计数应该增加 */
    TEST_ASSERT_EQUAL_UINT64(exit_count + 1, vcpu->exit_count);

    /* 恢复状态 */
    vcpu->state = original_state;
    vcpu->exit_count = exit_count;

    /* 销毁 vCPU */
    vmm_destroy_vcpu(TEST_VM_ID, TEST_VCPU_ID);
}

/**
 * @brief 测试 HVC (Hypercall) 退出处理
 */
void test_exit_hypercall(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;

    /* 创建 VM */
    vm = vmm_create_vm(TEST_VM_ID, TEST_MMIO_BASE);
    TEST_ASSERT_NOT_NULL(vm);

    /* 创建 vCPU */
    kernel_status_t ret = vmm_create_vcpu(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    vcpu = &vm->vcpus[TEST_VCPU_ID];

    /* 设置 Hypercall 号 */
    vcpu->gp_regs.x[0] = 0ULL;  /* CONSOLE_PUTC */

    /* 处理 HVC 退出 */
    ret = vmm_handle_exit(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 销毁 vCPU */
    vmm_destroy_vcpu(TEST_VM_ID, TEST_VCPU_ID);
}

/**
 * @brief 测试 MMIO 退出处理（读操作）
 */
void test_exit_data_abort_read(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    uint64_t original_pc;
    uint64_t original_x0;

    /* 创建 VM */
    vm = vmm_create_vm(TEST_VM_ID, TEST_MMIO_BASE);
    TEST_ASSERT_NOT_NULL(vm);

    /* 创建 vCPU */
    kernel_status_t ret = vmm_create_vcpu(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    vcpu = &vm->vcpus[TEST_VCPU_ID];

    /* 设置 ESR_EL2（读操作，1 字节） */
    vcpu->sys_regs.esr_el2 = 0x0000200000000000ULL;  /* EC=0x0A, WnR=0, Tn=0 */

    /* 设置 FAR_EL2（MMIO 地址） */
    vcpu->sys_regs.far_el2 = TEST_MMIO_BASE;

    /* 保存原始 PC */
    original_pc = vcpu->gp_regs.pc;

    /* 处理 MMIO 退出 */
    ret = vmm_handle_exit(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* PC 应该自增加 4 字节 */
    TEST_ASSERT_EQUAL_UINT64(original_pc + 4ULL, vcpu->gp_regs.pc);

    /* 如果返回值成功，x0 应该被写入返回值 */
    if (ret == KERNEL_OK)
    {
        original_x0 = vcpu->gp_regs.x[0];
    }

    /* 销毁 vCPU */
    vmm_destroy_vcpu(TEST_VM_ID, TEST_VCPU_ID);
}

/**
 * @brief 测试 MMIO 退出处理（写操作）
 */
void test_exit_data_abort_write(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    uint64_t original_pc;

    /* 创建 VM */
    vm = vmm_create_vm(TEST_VM_ID, TEST_MMIO_BASE);
    TEST_ASSERT_NOT_NULL(vm);

    /* 创建 vCPU */
    kernel_status_t ret = vmm_create_vcpu(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    vcpu = &vm->vcpus[TEST_VCPU_ID];

    /* 设置 ESR_EL2（写操作，1 字节） */
    vcpu->sys_regs.esr_el2 = 0x0000400000000000ULL;  /* EC=0x0A, WnR=1, Tn=0 */

    /* 设置 FAR_EL2（MMIO 地址） */
    vcpu->sys_regs.far_el2 = TEST_MMIO_BASE;

    /* 保存原始 PC */
    original_pc = vcpu->gp_regs.pc;

    /* 处理 MMIO 退出 */
    ret = vmm_handle_exit(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* PC 应该自增加 4 字节 */
    TEST_ASSERT_EQUAL_UINT64(original_pc + 4ULL, vcpu->gp_regs.pc);

    /* 销毁 vCPU */
    vmm_destroy_vcpu(TEST_VM_ID, TEST_VCPU_ID);
}

/**
 * @brief 测试系统寄存器退出处理
 */
void test_exit_sysreg(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;

    /* 创建 VM */
    vm = vmm_create_vm(TEST_VM_ID, TEST_MMIO_BASE);
    TEST_ASSERT_NOT_NULL(vm);

    /* 创建 vCPU */
    kernel_status_t ret = vmm_create_vcpu(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    vcpu = &vm->vcpus[TEST_VCPU_ID];

    /* 设置 ESR_EL2（系统寄存器访问） */
    vcpu->sys_regs.esr_el2 = 0x003E000000000000ULL;  /* EC=0x06 */

    /* 处理系统寄存器退出 */
    ret = vmm_handle_exit(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 销毁 vCPU */
    vmm_destroy_vcpu(TEST_VM_ID, TEST_VCPU_ID);
}

/**
 * @brief 测试指令中止退出处理
 */
void test_exit_inst_abort(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    uint64_t exit_count;

    /* 创建 VM */
    vm = vmm_create_vm(TEST_VM_ID, TEST_MMIO_BASE);
    TEST_ASSERT_NOT_NULL(vm);

    /* 创建 vCPU */
    kernel_status_t ret = vmm_create_vcpu(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    vcpu = &vm->vcpus[TEST_VCPU_ID];

    /* 保存退出计数 */
    exit_count = vcpu->exit_count;

    /* 设置 ESR_EL2（指令中止） */
    vcpu->sys_regs.esr_el2 = 0x003E000000000000ULL;  /* EC=0x0E */

    /* 处理指令中止退出 */
    ret = vmm_handle_exit(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 退出计数应该增加 */
    TEST_ASSERT_EQUAL_UINT64(exit_count + 1, vcpu->exit_count);

    /* 销毁 vCPU */
    vmm_destroy_vcpu(TEST_VM_ID, TEST_VCPU_ID);
}

/**
 * @brief 测试 VM 退出分发器（WFI/WFE）
 */
void test_vmm_handle_exit_dispatcher_wfi(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    uint64_t ec;

    /* 创建 VM */
    vm = vmm_create_vm(TEST_VM_ID, TEST_MMIO_BASE);
    TEST_ASSERT_NOT_NULL(vm);

    /* 创建 vCPU */
    kernel_status_t ret = vmm_create_vcpu(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    vcpu = &vm->vcpus[TEST_VCPU_ID];

    /* 设置 EC (异常类) */
    ec = 0x01ULL;  /* WFI/WFE */
    vcpu->sys_regs.esr_el2 = (ec << 26ULL);

    /* 处理退出 */
    ret = vmm_handle_exit(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 销毁 vCPU */
    vmm_destroy_vcpu(TEST_VM_ID, TEST_VCPU_ID);
}

/**
 * @brief 测试 VM 退出分发器（HVC）
 */
void test_vmm_handle_exit_dispatcher_hvc(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    uint64_t ec;

    /* 创建 VM */
    vm = vmm_create_vm(TEST_VM_ID, TEST_MMIO_BASE);
    TEST_ASSERT_NOT_NULL(vm);

    /* 创建 vCPU */
    kernel_status_t ret = vmm_create_vcpu(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    vcpu = &vm->vcpus[TEST_VCPU_ID];

    /* 设置 EC (异常类) */
    ec = 0x08ULL;  /* HVC */
    vcpu->sys_regs.esr_el2 = (ec << 26ULL);

    /* 处理退出 */
    ret = vmm_handle_exit(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 销毁 vCPU */
    vmm_destroy_vcpu(TEST_VM_ID, TEST_VCPU_ID);
}

/**
 * @brief 测试 VM 退出分发器（系统寄存器）
 */
void test_vmm_handle_exit_dispatcher_sysreg(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    uint64_t ec;

    /* 创建 VM */
    vm = vmm_create_vm(TEST_VM_ID, TEST_MMIO_BASE);
    TEST_ASSERT_NOT_NULL(vm);

    /* 创建 vCPU */
    kernel_status_t ret = vmm_create_vcpu(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    vcpu = &vm->vcpus[TEST_VCPU_ID];

    /* 设置 EC (异常类) */
    ec = 0x06ULL;  /* 系统寄存器 */
    vcpu->sys_regs.esr_el2 = (ec << 26ULL);

    /* 处理退出 */
    ret = vmm_handle_exit(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 销毁 vCPU */
    vmm_destroy_vcpu(TEST_VM_ID, TEST_VCPU_ID);
}

/**
 * @brief 测试 VM 退出分发器（指令中止）
 */
void test_vmm_handle_exit_dispatcher_inst_abort(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    uint64_t ec;

    /* 创建 VM */
    vm = vmm_create_vm(TEST_VM_ID, TEST_MMIO_BASE);
    TEST_ASSERT_NOT_NULL(vm);

    /* 创建 vCPU */
    kernel_status_t ret = vmm_create_vcpu(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    vcpu = &vm->vcpus[TEST_VCPU_ID];

    /* 设置 EC (异常类) */
    ec = 0x0EULL;  /* 指令中止 */
    vcpu->sys_regs.esr_el2 = (ec << 26ULL);

    /* 处理退出 */
    ret = vmm_handle_exit(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 销毁 vCPU */
    vmm_destroy_vcpu(TEST_VM_ID, TEST_VCPU_ID);
}

/**
 * @brief 测试 VM 退出分发器（数据中止）
 */
void test_vmm_handle_exit_dispatcher_data_abort(void)
{
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    uint64_t ec;

    /* 创建 VM */
    vm = vmm_create_vm(TEST_VM_ID, TEST_MMIO_BASE);
    TEST_ASSERT_NOT_NULL(vm);

    /* 创建 vCPU */
    kernel_status_t ret = vmm_create_vcpu(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    vcpu = &vm->vcpus[TEST_VCPU_ID];

    /* 设置 EC (异常类) */
    ec = 0x0AULL;  /* 数据中止 */
    vcpu->sys_regs.esr_el2 = (ec << 26ULL);

    /* 处理退出 */
    ret = vmm_handle_exit(TEST_VM_ID, TEST_VCPU_ID);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 销毁 vCPU */
    vmm_destroy_vcpu(TEST_VM_ID, TEST_VCPU_ID);
}

/* ========================================================================
 * 测试主函数
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_exit_wfi_wfe_with_irq);
    RUN_TEST(test_exit_wfi_wfe_without_irq);
    RUN_TEST(test_exit_hypercall);
    RUN_TEST(test_exit_data_abort_read);
    RUN_TEST(test_exit_data_abort_write);
    RUN_TEST(test_exit_sysreg);
    RUN_TEST(test_exit_inst_abort);
    RUN_TEST(test_vmm_handle_exit_dispatcher_wfi);
    RUN_TEST(test_vmm_handle_exit_dispatcher_hvc);
    RUN_TEST(test_vmm_handle_exit_dispatcher_sysreg);
    RUN_TEST(test_vmm_handle_exit_dispatcher_inst_abort);
    RUN_TEST(test_vmm_handle_exit_dispatcher_data_abort);

    return UNITY_END();
}
