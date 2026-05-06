/**
 * @file    test_npt.c
 * @brief   嵌套页表（NPT）单元测试
 * @author  AISafe64 Team
 * @date    2026-05-03
 * @version 1.0
 *
 * @details 测试嵌套页表的以下功能：
 *          - NPT 创建/销毁
 *          - NPT 映射/解除映射
 *          - NPT 二阶段翻译
 *          - NPT TLB 刷新
 *          - NPT 引用计数
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <unity.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <kernel/types.h>
#include "../npt/npt.h"

/* ========================================================================
 * 测试常量
 * ======================================================================== */

#define TEST_VM_ID               (0U)
#define TEST_GUEST_SIZE          (0x40000000ULL)  /* 1GB */
#define TEST_HOST_BASE           (0x40000000ULL)

/* ========================================================================
 * 测试夹具
 * ======================================================================== */

static nested_page_table_t *s_test_npt;

void setUp(void)
{
    /* 每个 test 前执行 */
    s_test_npt = NULL;
}

void tearDown(void)
{
    /* 每个 test 后执行 */
    if (s_test_npt != NULL)
    {
        npt_destroy(s_test_npt);
        s_test_npt = NULL;
    }
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 测试 NPT 创建成功
 */
void test_npt_create_success(void)
{
    kernel_status_t ret;

    ret = npt_create(TEST_VM_ID, TEST_GUEST_SIZE, TEST_HOST_BASE);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
}

/**
 * @brief 测试 NPT 创建失败（无效 VM ID）
 */
void test_npt_create_invalid_vm_id(void)
{
    kernel_status_t ret;

    ret = npt_create(0xFFFFFFFFU, TEST_GUEST_SIZE, TEST_HOST_BASE);
    TEST_ASSERT_NOT_EQUAL_INT32(KERNEL_OK, ret);
}

/**
 * @brief 测试 NPT 创建失败（Guest 大小无效）
 */
void test_npt_create_invalid_guest_size(void)
{
    kernel_status_t ret;

    ret = npt_create(TEST_VM_ID, 0ULL, TEST_HOST_BASE);
    TEST_ASSERT_NOT_EQUAL_INT32(KERNEL_OK, ret);

    ret = npt_create(TEST_VM_ID, 0x80000000ULL, TEST_HOST_BASE);  /* 超过 2GB */
    TEST_ASSERT_NOT_EQUAL_INT32(KERNEL_OK, ret);
}

/**
 * @brief 测试 NPT 销毁成功
 */
void test_npt_destroy_success(void)
{
    kernel_status_t ret;

    ret = npt_create(TEST_VM_ID, TEST_GUEST_SIZE, TEST_HOST_BASE);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    npt_destroy(s_test_npt);
    TEST_PASS();  /* 销毁没有返回值 */
}

/**
 * @brief 测试 NPT 映射成功
 */
void test_npt_map_page_success(void)
{
    kernel_status_t ret;
    paddr_t guest_paddr;
    paddr_t host_paddr;

    ret = npt_create(TEST_VM_ID, TEST_GUEST_SIZE, TEST_HOST_BASE);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    guest_paddr = 0x00001000ULL;  /* 4KB */
    host_paddr = 0x50001000ULL;   /* 4KB */

    ret = npt_map_page(TEST_VM_ID, guest_paddr, host_paddr, 0ULL);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
}

/**
 * @brief 测试 NPT 映射失败（无效 Guest 地址）
 */
void test_npt_map_invalid_guest_addr(void)
{
    kernel_status_t ret;

    ret = npt_create(TEST_VM_ID, TEST_GUEST_SIZE, TEST_HOST_BASE);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    ret = npt_map_page(TEST_VM_ID, 0ULL, TEST_HOST_BASE, 0ULL);
    TEST_ASSERT_NOT_EQUAL_INT32(KERNEL_OK, ret);

    ret = npt_map_page(TEST_VM_ID, 0x40000000ULL, TEST_HOST_BASE, 0ULL);
    TEST_ASSERT_NOT_EQUAL_INT32(KERNEL_OK, ret);
}

/**
 * @brief 测试 NPT 映射失败（无效 Host 地址）
 */
void test_npt_map_invalid_host_addr(void)
{
    kernel_status_t ret;

    ret = npt_create(TEST_VM_ID, TEST_GUEST_SIZE, TEST_HOST_BASE);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    ret = npt_map_page(TEST_VM_ID, 0x00001000ULL, 0ULL, 0ULL);
    TEST_ASSERT_NOT_EQUAL_INT32(KERNEL_OK, ret);
}

/**
 * @brief 测试 NPT 解除映射成功
 */
void test_npt_unmap_page_success(void)
{
    kernel_status_t ret;
    paddr_t guest_paddr;
    paddr_t host_paddr;

    ret = npt_create(TEST_VM_ID, TEST_GUEST_SIZE, TEST_HOST_BASE);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    guest_paddr = 0x00001000ULL;
    host_paddr = 0x50001000ULL;

    ret = npt_map_page(TEST_VM_ID, guest_paddr, host_paddr, 0ULL);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    ret = npt_unmap_page(TEST_VM_ID, guest_paddr);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
}

/**
 * @brief 测试 NPT 二阶段翻译成功
 */
void test_npt_translate_success(void)
{
    kernel_status_t ret;
    paddr_t guest_paddr;
    paddr_t host_paddr;
    paddr_t translated_paddr;

    ret = npt_create(TEST_VM_ID, TEST_GUEST_SIZE, TEST_HOST_BASE);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    guest_paddr = 0x00001000ULL;
    host_paddr = 0x50001000ULL;

    ret = npt_map_page(TEST_VM_ID, guest_paddr, host_paddr, 0ULL);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    ret = npt_translate(s_test_npt, (vaddr_t)guest_paddr, &translated_paddr);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_UINT64(host_paddr, translated_paddr);
}

/**
 * @brief 测试 NPT 二阶段翻译失败（未映射）
 */
void test_npt_translate_unmapped(void)
{
    kernel_status_t ret;
    paddr_t translated_paddr;

    ret = npt_create(TEST_VM_ID, TEST_GUEST_SIZE, TEST_HOST_BASE);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    ret = npt_translate(s_test_npt, 0x00001000ULL, &translated_paddr);
    TEST_ASSERT_NOT_EQUAL_INT32(KERNEL_OK, ret);
}

/**
 * @brief 测试 NPT 二阶段翻译失败（无效地址）
 */
void test_npt_translate_invalid_addr(void)
{
    kernel_status_t ret;
    paddr_t translated_paddr;

    ret = npt_translate(s_test_npt, 0ULL, &translated_paddr);
    TEST_ASSERT_NOT_EQUAL_INT32(KERNEL_OK, ret);
}

/**
 * @brief 测试 NPT TLB 刷新
 */
void test_npt_tlb_flush(void)
{
    kernel_status_t ret;

    ret = npt_create(TEST_VM_ID, TEST_GUEST_SIZE, TEST_HOST_BASE);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    ret = npt_tlb_flush(TEST_VM_ID, 0U);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
}

/**
 * @brief 测试 NPT 引用计数
 */
void test_npt_ref_count(void)
{
    kernel_status_t ret;
    uint32_t ref_count;

    ret = npt_create(TEST_VM_ID, TEST_GUEST_SIZE, TEST_HOST_BASE);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    ref_count = npt_get_ref_count(s_test_npt);
    TEST_ASSERT_EQUAL_UINT32(1U, ref_count);
}

/**
 * @brief 测试 NPT NULL 指针处理
 */
void test_npt_null_pointer(void)
{
    kernel_status_t ret;
    paddr_t translated_paddr;

    /* NULL 指针测试 */
    ret = npt_map_page(TEST_VM_ID, 0x00001000ULL, 0x50001000ULL, 0ULL);
    TEST_ASSERT_NOT_EQUAL_INT32(KERNEL_OK, ret);  /* VM 不存在 */

    ret = npt_unmap_page(TEST_VM_ID, 0x00001000ULL);
    TEST_ASSERT_NOT_EQUAL_INT32(KERNEL_OK, ret);  /* VM 不存在 */

    ret = npt_translate(NULL, 0x00001000ULL, &translated_paddr);
    TEST_ASSERT_NOT_EQUAL_INT32(KERNEL_OK, ret);  /* NULL 指针 */

    ref_count = npt_get_ref_count(NULL);
    TEST_ASSERT_EQUAL_UINT32(0U, ref_count);  /* NULL 指针 */
}

/* ========================================================================
 * 测试主函数
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_npt_create_success);
    RUN_TEST(test_npt_create_invalid_vm_id);
    RUN_TEST(test_npt_create_invalid_guest_size);
    RUN_TEST(test_npt_destroy_success);
    RUN_TEST(test_npt_map_page_success);
    RUN_TEST(test_npt_map_invalid_guest_addr);
    RUN_TEST(test_npt_map_invalid_host_addr);
    RUN_TEST(test_npt_unmap_page_success);
    RUN_TEST(test_npt_translate_success);
    RUN_TEST(test_npt_translate_unmapped);
    RUN_TEST(test_npt_translate_invalid_addr);
    RUN_TEST(test_npt_tlb_flush);
    RUN_TEST(test_npt_ref_count);
    RUN_TEST(test_npt_null_pointer);

    return UNITY_END();
}
