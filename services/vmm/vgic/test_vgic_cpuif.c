/**
 * @file    test_vgic_cpuif.c
 * @brief   GIC CPU Interface 单元测试
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 1.0
 *
 * @details 测试 GIC CPU Interface 的以下功能：
 *          - GICC_CTLR 读/写
 *          - GICC_PMR 读/写
 *          - GICC_BPR 读/写
 *          - GICC_IAR 读（中断确认）
 *          - GICC_EOIR 写（中断结束）
 *          - GICC_RPR 读（运行优先级）
 *          - GICC_HPPIR 读（最高优先级待处理中断）
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
#include "../../vgic/vgic_cpuif.h"
#include "../../vmm.h"

/* ========================================================================
 * 测试常量
 * ======================================================================== */

/** @brief 测试 VM ID */
#define TEST_VM_ID                   0U

/** @brief 测试 vCPU ID */
#define TEST_VCPU_ID                 0U

/** @brief 测试中断号 */
#define TEST_IRQ_0                   0U
#define TEST_IRQ_1                   1U
#define TEST_IRQ_SGI                 2U

/* ========================================================================
 * 测试夹具
 * ======================================================================== */

/**
 * @brief 测试 VM 描述符
 */
static vm_desc_t s_test_vm;

/**
 * @brief 设置测试环境
 */
void setUp(void)
{
    (void)memset(&s_test_vm, 0, sizeof(s_test_vm));
    s_test_vm.vm_id = TEST_VM_ID;
    s_test_vm.vcpu_count = 1U;
}

/**
 * @brief 清理测试环境
 */
void tearDown(void)
{
    (void)memset(&s_test_vm, 0, sizeof(s_test_vm));
}

/* ========================================================================
 * 测试用例 - GICC_CTLR 读/写
 * ======================================================================== */

/**
 * @brief 测试 GICC_CTLR 读/写
 */
void test_gicc_ctlr_read_write(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试写入 GICC_CTLR */
    ret = vgic_cpuif_write(TEST_VM_ID, TEST_VCPU_ID, GICC_CTLR_OFFSET, 4U, 0x1U);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 测试读取 GICC_CTLR */
    ret = vgic_cpuif_read(TEST_VM_ID, TEST_VCPU_ID, GICC_CTLR_OFFSET, 4U, &value);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_HEX32(0x1U, value);
}

/* ========================================================================
 * 测试用例 - GICC_PMR 读/写
 * ======================================================================== */

/**
 * @brief 测试 GICC_PMR 读/写
 */
void test_gicc_pmr_read_write(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试写入 GICC_PMR（设置优先级屏蔽为 0xA0） */
    ret = vgic_cpuif_write(TEST_VM_ID, TEST_VCPU_ID, GICC_PMR_OFFSET, 4U, 0xA0U);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 测试读取 GICC_PMR */
    ret = vgic_cpuif_read(TEST_VM_ID, TEST_VCPU_ID, GICC_PMR_OFFSET, 4U, &value);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_HEX32(0xA0U, value);
}

/* ========================================================================
 * 测试用例 - GICC_BPR 读/写
 * ======================================================================== */

/**
 * @brief 测试 GICC_BPR 读/写
 */
void test_gicc_bpr_read_write(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试写入 GICC_BPR（设置二进制点为 2） */
    ret = vgic_cpuif_write(TEST_VM_ID, TEST_VCPU_ID, GICC_BPR_OFFSET, 4U, 0x2U);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 测试读取 GICC_BPR */
    ret = vgic_cpuif_read(TEST_VM_ID, TEST_VCPU_ID, GICC_BPR_OFFSET, 4U, &value);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_HEX32(0x2U, value);
}

/**
 * @brief 测试 GICC_BPR 无效值
 */
void test_gicc_bpr_invalid_value(void)
{
    kernel_status_t ret;

    /* 测试写入无效的 GICC_BPR（值应该 0-7） */
    ret = vgic_cpuif_write(TEST_VM_ID, TEST_VCPU_ID, GICC_BPR_OFFSET, 4U, 0x8U);
    TEST_ASSERT_EQUAL_INT(-(int32_t)EINVAL, ret);
}

/* ========================================================================
 * 测试用例 - GICC_IAR 读（中断确认）
 * ======================================================================== */

/**
 * @brief 测试 GICC_IAR 读（无待处理中断）
 */
void test_gicc_iar_read_no_pending_irq(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试读取 GICC_IAR（没有待处理中断，应该返回 1023） */
    ret = vgic_cpuif_read(TEST_VM_ID, TEST_VCPU_ID, GICC_IAR_OFFSET, 4U, &value);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_HEX32(1023U << 0, value);
}

/* ========================================================================
 * 测试用例 - GICC_EOIR 写（中断结束）
 * ======================================================================== */

/**
 * @brief 测试 GICC_EOIR 写
 */
void test_gicc_eoir_write(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试写入 GICC_EOIR（结束中断 2） */
    value = (0x0U << 10) |  /* CPUID = 0 */
             (TEST_IRQ_SGI << 0);  /* INTID = 2 */

    ret = vgic_cpuif_write(TEST_VM_ID, TEST_VCPU_ID, GICC_EOIR_OFFSET, 4U, value);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
}

/**
 * @brief 测试 GICC_EOIR 写（无效中断号）
 */
void test_gicc_eoir_write_invalid_irq(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试写入 GICC_EOIR（无效中断号，应该忽略） */
    value = (0x0U << 10) |  /* CPUID = 0 */
             (1020U << 0);  /* INTID = 1020 (无效） */

    ret = vgic_cpuif_write(TEST_VM_ID, TEST_VCPU_ID, GICC_EOIR_OFFSET, 4U, value);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
}

/* ========================================================================
 * 测试用例 - GICC_RPR 读（运行优先级）
 * ======================================================================== */

/**
 * @brief 测试 GICC_RPR 读
 */
void test_gicc_rpr_read(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试读取 GICC_RPR（应该返回 0xFF，因为没有活跃中断） */
    ret = vgic_cpuif_read(TEST_VM_ID, TEST_VCPU_ID, GICC_RPR_OFFSET, 4U, &value);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_HEX32(0xFFU, value);
}

/* ========================================================================
 * 测试用例 - GICC_HPPIR 读（最高优先级待处理中断）
 * ======================================================================== */

/**
 * @brief 测试 GICC_HPPIR 读（无待处理中断）
 */
void test_gicc_hppir_read_no_pending_irq(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试读取 GICC_HPPIR（没有待处理中断，应该返回 1023） */
    ret = vgic_cpuif_read(TEST_VM_ID, TEST_VCPU_ID, GICC_HPPIR_OFFSET, 4U, &value);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_HEX32(1023U << 0, value);
}

/* ========================================================================
 * 测试用例 - 中断确认/结束
 * ======================================================================== */

/**
 * @brief 测试中断确认（ACK）
 */
void test_vgic_ack_irq(void)
{
    kernel_status_t ret;

    /* 测试中断确认 */
    ret = vgic_ack_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_SGI);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
}

/**
 * @brief 测试中断结束（EOI）
 */
void test_vgic_end_irq(void)
{
    kernel_status_t ret;

    /* 测试中断结束 */
    ret = vgic_end_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_SGI);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
}

/* ========================================================================
 * 测试用例 - 最高优先级待处理中断
 * ======================================================================== */

/**
 * @brief 测试获取最高优先级待处理中断
 */
void test_vgic_get_highest_priority_irq(void)
{
    uint32_t irq;
    kernel_status_t ret;

    /* 测试获取最高优先级待处理中断（没有待处理中断，应该失败） */
    ret = vgic_get_highest_priority_irq(TEST_VM_ID, TEST_VCPU_ID, &irq);
    TEST_ASSERT_EQUAL_INT(-(int32_t)ENOENT, ret);
}

/* ========================================================================
 * 测试用例 - 边界条件
 * ======================================================================== */

/**
 * @brief 测试无效的 VM ID
 */
void test_invalid_vm_id(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试读取无效的 VM ID */
    ret = vgic_cpuif_read(0xFFU, TEST_VCPU_ID, GICC_CTLR_OFFSET, 4U, &value);
    TEST_ASSERT_EQUAL_INT(-(int32_t)ENOENT, ret);
}

/**
 * @brief 测试无效的 vCPU ID
 */
void test_invalid_vcpu_id(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试读取无效的 vCPU ID */
    ret = vgic_cpuif_read(TEST_VM_ID, 0xFFU, GICC_CTLR_OFFSET, 4U, &value);
    TEST_ASSERT_EQUAL_INT(-(int32_t)ENOENT, ret);
}

/**
 * @brief 测试无效的寄存器偏移
 */
void test_invalid_offset(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试读取无效的寄存器偏移 */
    ret = vgic_cpuif_read(TEST_VM_ID, TEST_VCPU_ID, 0x2000U, 4U, &value);
    TEST_ASSERT_EQUAL_INT(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试无效的访问大小
 */
void test_invalid_size(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试读取无效的访问大小（3 字节） */
    ret = vgic_cpuif_read(TEST_VM_ID, TEST_VCPU_ID, GICC_CTLR_OFFSET, 3U, &value);
    TEST_ASSERT_EQUAL_INT(-(int32_t)EINVAL, ret);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    /* 测试用例 - GICC_CTLR 读/写 */
    RUN_TEST(test_gicc_ctlr_read_write);

    /* 测试用例 - GICC_PMR 读/写 */
    RUN_TEST(test_gicc_pmr_read_write);

    /* 测试用例 - GICC_BPR 读/写 */
    RUN_TEST(test_gicc_bpr_read_write);
    RUN_TEST(test_gicc_bpr_invalid_value);

    /* 测试用例 - GICC_IAR 读（中断确认） */
    RUN_TEST(test_gicc_iar_read_no_pending_irq);

    /* 测试用例 - GICC_EOIR 写（中断结束） */
    RUN_TEST(test_gicc_eoir_write);
    RUN_TEST(test_gicc_eoir_write_invalid_irq);

    /* 测试用例 - GICC_RPR 读（运行优先级） */
    RUN_TEST(test_gicc_rpr_read);

    /* 测试用例 - GICC_HPPIR 读（最高优先级待处理中断） */
    RUN_TEST(test_gicc_hppir_read_no_pending_irq);

    /* 测试用例 - 中断确认/结束 */
    RUN_TEST(test_vgic_ack_irq);
    RUN_TEST(test_vgic_end_irq);

    /* 测试用例 - 最高优先级待处理中断 */
    RUN_TEST(test_vgic_get_highest_priority_irq);

    /* 测试用例 - 边界条件 */
    RUN_TEST(test_invalid_vm_id);
    RUN_TEST(test_invalid_vcpu_id);
    RUN_TEST(test_invalid_offset);
    RUN_TEST(test_invalid_size);

    return UNITY_END();
}
