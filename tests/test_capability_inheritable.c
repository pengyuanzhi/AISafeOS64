/**
 * @file    test_capability_inheritable.c
 * @brief   能力继承标志测试套件
 * @author  AISafe64 Team
 * @date    2026-05-25
 * @version 1.0
 *
 * @details 测试能力继承标志功能：
 *          - 继承标志设置和查询
 *          - 继承标志对能力复制的影响
 *          - 继承标志默认值
 */

#include "unity.h"
#include <kernel/capability.h>
#include <kernel/cspace.h>
#include <kernel/types.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 测试设置
 * ======================================================================== */

static cspace_t *g_src_cspace;
static cspace_t *g_dest_cspace;
static cap_slot_t g_src_root;
static cap_slot_t g_dest_root;

/* ========================================================================
 * 测试用例
 * ======================================================================== */

void setUp(void)
{
    kernel_status_t status;

    status = cspace_create(CSPACE_DEFAULT_CAPACITY, &g_src_cspace);
    TEST_ASSERT_EQUAL(KERNEL_OK, status);
    g_src_root = cspace_root(g_src_cspace);

    status = cspace_create(CSPACE_DEFAULT_CAPACITY, &g_dest_cspace);
    TEST_ASSERT_EQUAL(KERNEL_OK, status);
    g_dest_root = cspace_root(g_dest_cspace);
}

void tearDown(void)
{
    if (g_src_cspace != NULL)
    {
        cspace_destroy(g_src_cspace);
    }
    if (g_dest_cspace != NULL)
    {
        cspace_destroy(g_dest_cspace);
    }
    g_src_cspace = NULL;
    g_dest_cspace = NULL;
    g_src_root = CAP_SLOT_INVALID;
    g_dest_root = CAP_SLOT_INVALID;
}

/* ========================================================================
 * 测试 1: 能力继承标志默认值
 * ======================================================================== */

void test_inheritable_default_true(void)
{
    cspace_t *cspace;
    cap_slot_t slot;
    cap_t *cap;
    bool inheritable;

    /* 创建线程能力（默认继承标志为 true） */
    slot = cspace_alloc(cspace_root(g_src_cspace));
    TEST_ASSERT_NOT_EQUAL(CAP_SLOT_INVALID, slot);

    cap = cspace_lookup(cspace_root(g_src_cspace), slot);
    TEST_ASSERT_NOT_NULL(cap);

    /* 查询继承标志 */
    cap_get_inheritable(cspace_root(g_src_cspace), slot, &inheritable);
    TEST_ASSERT_EQUAL(true, inheritable);
}

/* ========================================================================
 * 测试 2: 设置能力继承标志为 true
 * ======================================================================== */

void test_set_inheritable_true(void)
{
    cspace_t *cspace;
    cap_slot_t slot;
    bool inheritable;

    /* 创建线程能力 */
    slot = cspace_alloc(cspace_root(g_src_cspace));
    TEST_ASSERT_NOT_EQUAL(CAP_SLOT_INVALID, slot);

    /* 设置继承标志为 true */
    TEST_ASSERT_EQUAL(KERNEL_OK,
                      cap_set_inheritable(cspace_root(g_src_cspace),
                                          slot,
                                          true));

    /* 验证继承标志 */
    TEST_ASSERT_EQUAL(KERNEL_OK,
                      cap_get_inheritable(cspace_root(g_src_cspace),
                                          slot,
                                          &inheritable));
    TEST_ASSERT_EQUAL(true, inheritable);
}

/* ========================================================================
 * 测试 3: 设置能力继承标志为 false
 * ======================================================================== */

void test_set_inheritable_false(void)
{
    cspace_t *cspace;
    cap_slot_t slot;
    bool inheritable;

    /* 创建线程能力 */
    slot = cspace_alloc(cspace_root(g_src_cspace));
    TEST_ASSERT_NOT_EQUAL(CAP_SLOT_INVALID, slot);

    /* 设置继承标志为 false */
    TEST_ASSERT_EQUAL(KERNEL_OK,
                      cap_set_inheritable(cspace_root(g_src_cspace),
                                          slot,
                                          false));

    /* 验证继承标志 */
    TEST_ASSERT_EQUAL(KERNEL_OK,
                      cap_get_inheritable(cspace_root(g_src_cspace),
                                          slot,
                                          &inheritable));
    TEST_ASSERT_EQUAL(false, inheritable);
}

/* ========================================================================
 * 测试 4: 无效能力槽返回错误
 * ======================================================================== */

void test_invalid_slot_returns_error(void)
{
    kernel_status_t status;
    bool inheritable;

    /* 测试 CAP_SLOT_INVALID */
    status = cap_set_inheritable(cspace_root(g_src_cspace),
                                  CAP_SLOT_INVALID,
                                  true);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, status);

    status = cap_get_inheritable(cspace_root(g_src_cspace),
                                  CAP_SLOT_INVALID,
                                  &inheritable);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, status);

    /* 测试 NULL 输出指针 */
    status = cap_get_inheritable(cspace_root(g_src_cspace),
                                  0U,
                                  NULL);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, status);
}

/* ========================================================================
 * 测试 5: 无能力槽返回错误
 * ======================================================================== */

void test_invalid_slot_returns_noent(void)
{
    kernel_status_t status;
    bool inheritable;

    /* 测试未分配的能力槽 */
    status = cap_get_inheritable(cspace_root(g_src_cspace),
                                  100U,
                                  &inheritable);
    TEST_ASSERT_EQUAL(-(int32_t)ENOENT, status);
}

/* ========================================================================
 * 测试 6: 继承标志对能力复制的影响
 * ======================================================================== */

void test_inheritable_false_blocks_copy(void)
{
    kernel_status_t status;
    cap_slot_t src_slot;
    cap_slot_t dest_slot;
    bool inheritable;

    /* 在源 CSpace 创建能力 */
    src_slot = cspace_alloc(cspace_root(g_src_cspace));
    TEST_ASSERT_NOT_EQUAL(CAP_SLOT_INVALID, src_slot);

    /* 设置继承标志为 false */
    TEST_ASSERT_EQUAL(KERNEL_OK,
                      cap_set_inheritable(cspace_root(g_src_cspace),
                                          src_slot,
                                          false));

    /* 尝试复制到目标 CSpace（应该失败） */
    dest_slot = cspace_alloc(cspace_root(g_dest_cspace));
    TEST_ASSERT_NOT_EQUAL(CAP_SLOT_INVALID, dest_slot);

    status = cap_copy(cspace_root(g_src_cspace),
                      src_slot,
                      cspace_root(g_dest_cspace),
                      dest_slot,
                      CAP_RIGHT_ALL);
    TEST_ASSERT_EQUAL(-(int32_t)EACCES, status);

    /* 验证目标 CSpace 中没有复制的能力 */
    cap_t *dest_cap = cspace_lookup(cspace_root(g_dest_cspace), dest_slot);
    TEST_ASSERT_NULL(dest_cap);
}

/* ========================================================================
 * 测试 7: 继承标志为 true 允许复制
 * ======================================================================== */

void test_inheritable_true_allows_copy(void)
{
    kernel_status_t status;
    cap_slot_t src_slot;
    cap_slot_t dest_slot;
    cap_t *dest_cap;
    bool inheritable;

    /* 在源 CSpace 创建能力 */
    src_slot = cspace_alloc(cspace_root(g_src_cspace));
    TEST_ASSERT_NOT_EQUAL(CAP_SLOT_INVALID, src_slot);

    /* 设置继承标志为 true（默认值） */
    TEST_ASSERT_EQUAL(KERNEL_OK,
                      cap_set_inheritable(cspace_root(g_src_cspace),
                                          src_slot,
                                          true));

    /* 复制到目标 CSpace（应该成功） */
    dest_slot = cspace_alloc(cspace_root(g_dest_cspace));
    TEST_ASSERT_NOT_EQUAL(CAP_SLOT_INVALID, dest_slot);

    status = cap_copy(cspace_root(g_src_cspace),
                      src_slot,
                      cspace_root(g_dest_cspace),
                      dest_slot,
                      CAP_RIGHT_ALL);
    TEST_ASSERT_EQUAL(KERNEL_OK, status);

    /* 验证目标 CSpace 中成功复制 */
    dest_cap = cspace_lookup(cspace_root(g_dest_cspace), dest_slot);
    TEST_ASSERT_NOT_NULL(dest_cap);
    TEST_ASSERT_EQUAL(true, dest_cap->inheritable);
}

/* ========================================================================
 * 测试 8: 多核并发设置继承标志
 * ======================================================================== */

void test_concurrent_inheritable_modification(void)
{
    kernel_status_t status;
    cap_slot_t slot;
    bool inheritable;
    uint32_t cpu;

    /* 创建能力 */
    slot = cspace_alloc(cspace_root(g_src_cspace));
    TEST_ASSERT_NOT_EQUAL(CAP_SLOT_INVALID, slot);

    /* 在多核上并发修改继承标志 */
    for (cpu = 0; cpu < 4; cpu++)
    {
        status = cap_set_inheritable(cspace_root(g_src_cspace),
                                      slot,
                                      true);
        TEST_ASSERT_EQUAL(KERNEL_OK, status);

        status = cap_get_inheritable(cspace_root(g_src_cspace),
                                      slot,
                                      &inheritable);
        TEST_ASSERT_EQUAL(KERNEL_OK, status);
        TEST_ASSERT_EQUAL(true, inheritable);
    }
}

/* ========================================================================
 * 测试套件运行
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* 继承标志测试 */
    RUN_TEST(test_inheritable_default_true);
    RUN_TEST(test_set_inheritable_true);
    RUN_TEST(test_set_inheritable_false);
    RUN_TEST(test_invalid_slot_returns_error);
    RUN_TEST(test_invalid_slot_returns_noent);
    RUN_TEST(test_inheritable_false_blocks_copy);
    RUN_TEST(test_inheritable_true_allows_copy);
    RUN_TEST(test_concurrent_inheritable_modification);

    return UNITY_END();
}
