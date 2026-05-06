/**
 * @file    test_vgic_dist.c
 * @brief   GIC Distributor 单元测试
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 1.0
 *
 * @details 测试 GIC Distributor 的以下功能：
 *          - GIC Distributor 初始化/销毁
 *          - 寄存器读写（GICD_CTLR, GICD_TYPER, GICD_ISENABLER 等）
 *          - 中断使能/禁用
 *          - 中断挂起/清除
 *          - 中断优先级设置
 *          - 中断路由设置
 *          - SGI 处理
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
#include "../../vgic/vgic_dist.h"
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
#define TEST_IRQ_32                  32U
#define TEST_IRQ_64                  64U
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
 * 测试用例 - GIC Distributor 初始化/销毁
 * ======================================================================== */

/**
 * @brief 测试 GICD_CTLR 读/写
 */
void test_gicd_ctlr_read_write(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试写入 GICD_CTLR */
    ret = vgic_dist_write(TEST_VM_ID, GICD_CTLR_OFFSET, 4U, 0x1U);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 测试读取 GICD_CTLR */
    ret = vgic_dist_read(TEST_VM_ID, GICD_CTLR_OFFSET, 4U, &value);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_HEX32(0x1U, value);
}

/**
 * @brief 测试 GICD_TYPER 读
 */
void test_gicd_typer_read(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试读取 GICD_TYPER */
    ret = vgic_dist_read(TEST_VM_ID, GICD_TYPER_OFFSET, 4U, &value);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 检查 ITLinesNumber 字段 */
    TEST_ASSERT_EQUAL_HEX32((0x1F << 0), value & 0x1F);
}

/**
 * @brief 测试 GICD_ISENABLER 读/写
 */
void test_gicd_isenabler_read_write(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试写入 GICD_ISENABLER（使能中断 0-31） */
    ret = vgic_dist_write(TEST_VM_ID, GICD_ISENABLER_OFFSET, 4U, 0xFFFFFFFFU);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 测试读取 GICD_ISENABLER */
    ret = vgic_dist_read(TEST_VM_ID, GICD_ISENABLER_OFFSET, 4U, &value);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFU, value);
}

/**
 * @brief 测试 GICD_ICENABLER 写（禁用中断）
 */
void test_gicd_icenabler_write(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 先使能所有中断 */
    ret = vgic_dist_write(TEST_VM_ID, GICD_ISENABLER_OFFSET, 4U, 0xFFFFFFFFU);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 测试写入 GICD_ICENABLER（禁用中断 0-31） */
    ret = vgic_dist_write(TEST_VM_ID, GICD_ICENABLER_OFFSET, 4U, 0x0000FFFFU);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 测试读取 GICD_ISENABLER（ICENABLER 是只写的，应返回 ISENABLER 的值） */
    ret = vgic_dist_read(TEST_VM_ID, GICD_ICENABLER_OFFSET, 4U, &value);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    /* 只有高 16 位应该被使能 */
    TEST_ASSERT_EQUAL_HEX32(0xFFFF0000U, value);
}

/**
 * @brief 测试 GICD_IPRIORITYR 读/写
 */
void test_gicd_ipriorityr_read_write(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试写入 GICD_IPRIORITYR（设置中断 0-3 的优先级） */
    ret = vgic_dist_write(TEST_VM_ID, GICD_IPRIORITYR_OFFSET, 4U, 0x01020304U);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 测试读取 GICD_IPRIORITYR */
    ret = vgic_dist_read(TEST_VM_ID, GICD_IPRIORITYR_OFFSET, 4U, &value);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_HEX32(0x01020304U, value);
}

/**
 * @brief 测试 GICD_ITARGETSR 读/写
 */
void test_gicd_itargetsr_read_write(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试写入 GICD_ITARGETSR（设置中断 0-3 的目标 CPU） */
    ret = vgic_dist_write(TEST_VM_ID, GICD_ITARGETSR_OFFSET, 4U, 0x01010101U);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 测试读取 GICD_ITARGETSR */
    ret = vgic_dist_read(TEST_VM_ID, GICD_ITARGETSR_OFFSET, 4U, &value);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_HEX32(0x01010101U, value);
}

/**
 * @brief 测试 GICD_ICFGR 读/写
 */
void test_gicd_icfgr_read_write(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试写入 GICD_ICFGR（设置中断 0-31 的配置） */
    ret = vgic_dist_write(TEST_VM_ID, GICD_ICFGR_OFFSET, 4U, 0xAAAAAAAAU);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);

    /* 测试读取 GICD_ICFGR */
    ret = vgic_dist_read(TEST_VM_ID, GICD_ICFGR_OFFSET, 4U, &value);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_HEX32(0xAAAAAAAAU, value);
}

/**
 * @brief 测试 GICD_IABR 读
 */
void test_gicd_iabr_read(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试读取 GICD_IABR（应该返回 0，因为没有活跃中断） */
    ret = vgic_dist_read(TEST_VM_ID, GICD_IABR_OFFSET, 4U, &value);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_HEX32(0x00000000U, value);
}

/* ========================================================================
 * 测试用例 - SGI 处理
 * ======================================================================== */

/**
 * @brief 测试 GICD_SGIR 写入（SGI 注入）
 */
void test_gicd_sgir_sgi_inject(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试写入 GICD_SGIR（发送 SGI 2 到 CPU 0） */
    value = (0x0U << 24) |  /* TargetListFilter = 0 (List) */
             (0x1U << 16) |  /* TargetList = CPU 0 */
             (TEST_IRQ_SGI << 0);  /* SGIINTID = 2 */

    ret = vgic_dist_write(TEST_VM_ID, GICD_SGIR_OFFSET, 4U, value);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
}

/**
 * @brief 测试 GICD_SGIR 写入（All Others 模式）
 */
void test_gicd_sgir_all_others(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试写入 GICD_SGIR（发送 SGI 2 到所有其他 CPU） */
    value = (0x2U << 24) |  /* TargetListFilter = 2 (All Others) */
             (0x0U << 16) |  /* TargetList = 0 */
             (TEST_IRQ_SGI << 0);  /* SGIINTID = 2 */

    ret = vgic_dist_write(TEST_VM_ID, GICD_SGIR_OFFSET, 4U, value);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
}

/**
 * @brief 测试 GICD_SGIR 写入（Self 模式）
 */
void test_gicd_sgir_self(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试写入 GICD_SGIR（发送 SGI 2 到自己） */
    value = (0x3U << 24) |  /* TargetListFilter = 3 (Self) */
             (0x0U << 16) |  /* TargetList = 0 */
             (TEST_IRQ_SGI << 0);  /* SGIINTID = 2 */

    ret = vgic_dist_write(TEST_VM_ID, GICD_SGIR_OFFSET, 4U, value);
    TEST_ASSERT_EQUAL_INT(KERNEL_OK, ret);
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
    ret = vgic_dist_read(0xFFU, GICD_CTLR_OFFSET, 4U, &value);
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
    ret = vgic_dist_read(TEST_VM_ID, 0x2000U, 4U, &value);
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
    ret = vgic_dist_read(TEST_VM_ID, GICD_CTLR_OFFSET, 3U, &value);
    TEST_ASSERT_EQUAL_INT(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试无效的 SGI ID
 */
void test_invalid_sgi_id(void)
{
    uint32_t value;
    kernel_status_t ret;

    /* 测试写入无效的 SGI ID（SGI ID 应该是 0-15） */
    value = (0x0U << 24) |  /* TargetListFilter = 0 (List) */
             (0x1U << 16) |  /* TargetList = CPU 0 */
             (0x10U << 0);  /* SGIINTID = 16 (无效） */

    ret = vgic_dist_write(TEST_VM_ID, GICD_SGIR_OFFSET, 4U, value);
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

    /* 测试用例 - GIC Distributor 初始化/销毁 */
    RUN_TEST(test_gicd_ctlr_read_write);
    RUN_TEST(test_gicd_typer_read);
    RUN_TEST(test_gicd_isenabler_read_write);
    RUN_TEST(test_gicd_icenabler_write);

    /* 测试用例 - 寄存器读写 */
    RUN_TEST(test_gicd_ipriorityr_read_write);
    RUN_TEST(test_gicd_itargetsr_read_write);
    RUN_TEST(test_gicd_icfgr_read_write);
    RUN_TEST(test_gicd_iabr_read);

    /* 测试用例 - SGI 处理 */
    RUN_TEST(test_gicd_sgir_sgi_inject);
    RUN_TEST(test_gicd_sgir_all_others);
    RUN_TEST(test_gicd_sgir_self);

    /* 测试用例 - 边界条件 */
    RUN_TEST(test_invalid_vm_id);
    RUN_TEST(test_invalid_offset);
    RUN_TEST(test_invalid_size);
    RUN_TEST(test_invalid_sgi_id);

    return UNITY_END();
}
