/**
 * @file    test_npt_phase1.c
 * @brief   NPT 页表项操作单元测试（Phase 1）
 * @author  AISafe64 Team
 * @date    2026-05-22
 * @version 1.0
 *
 * @details 测试嵌套页表页表项操作功能：
 *          - 页表项设置（类型/物理地址/标志位）
 *          - 页表项清除
 *          - 页表项查询
 *          - 权限检查
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "mock_kernel.h"       /* 必须在内核头文件之前包含 */
#include "unity.h"
#include "../services/vmm/npt/npt.h"

/* ========================================================================
 * 测试常量
 * ======================================================================== */

#define TEST_PADDR           (0x40001000ULL)    /* 4KB 对齐 */
#define TEST_FLAGS_R         (1ULL << 6ULL)     /* 读权限 */
#define TEST_FLAGS_W         (1ULL << 7ULL)     /* 写权限 */
#define TEST_FLAGS_X         (1ULL << 10ULL)    /* 执行权限 */
#define TEST_FLAGS_U         (1ULL << 11ULL)    /* 用户模式 */

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
 * 测试用例 - 页表项设置
 * ======================================================================== */

/**
 * @brief 测试设置页表项类型（Table）
 */
void test_npt_pte_set_type_table(void)
{
    npt_entry_t entry = 0ULL;
    npt_entry_t result;

    result = npt_pte_set_type(entry, NPT_ENTRY_TYPE_TABLE);

    TEST_ASSERT_EQUAL_UINT64(NPT_ENTRY_TYPE_TABLE << NPT_ENTRY_TYPE_SHIFT, result);
}

/**
 * @brief 测试设置页表项类型（Block）
 */
void test_npt_pte_set_type_block(void)
{
    npt_entry_t entry = 0ULL;
    npt_entry_t result;

    result = npt_pte_set_type(entry, NPT_ENTRY_TYPE_BLOCK);

    TEST_ASSERT_EQUAL_UINT64(NPT_ENTRY_TYPE_BLOCK << NPT_ENTRY_TYPE_SHIFT, result);
}

/**
 * @brief 测试设置页表项类型（Page）
 */
void test_npt_pte_set_type_page(void)
{
    npt_entry_t entry = 0ULL;
    npt_entry_t result;

    result = npt_pte_set_type(entry, NPT_ENTRY_TYPE_PAGE);

    TEST_ASSERT_EQUAL_UINT64(NPT_ENTRY_TYPE_PAGE << NPT_ENTRY_TYPE_SHIFT, result);
}

/**
 * @brief 测试设置物理地址
 */
void test_npt_pte_set_paddr(void)
{
    npt_entry_t entry = 0ULL;
    npt_entry_t result;
    uint64_t expected;

    expected = (TEST_PADDR >> 12ULL) << NPT_ENTRY_PADDR_SHIFT;
    result = npt_pte_set_paddr(entry, TEST_PADDR);

    TEST_ASSERT_EQUAL_UINT64(expected, result);
}

/**
 * @brief 测试设置标志位
 */
void test_npt_pte_set_flags(void)
{
    npt_entry_t entry = 0ULL;
    npt_entry_t result;
    uint64_t flags;

    flags = TEST_FLAGS_R | TEST_FLAGS_W | TEST_FLAGS_X;
    result = npt_pte_set_flags(entry, flags);

    TEST_ASSERT_EQUAL_UINT64(flags, result);
}

/**
 * @brief 测试完整页表项设置
 */
void test_npt_pte_set_complete(void)
{
    npt_entry_t entry = 0ULL;
    npt_entry_t result;
    uint64_t flags;
    uint64_t expected;

    flags = TEST_FLAGS_R | TEST_FLAGS_W | TEST_FLAGS_X;
    expected = (TEST_PADDR >> 12ULL) << NPT_ENTRY_PADDR_SHIFT;
    expected |= (NPT_ENTRY_TYPE_PAGE << NPT_ENTRY_TYPE_SHIFT);
    expected |= flags;

    result = npt_pte_set_type(entry, NPT_ENTRY_TYPE_PAGE);
    result = npt_pte_set_paddr(result, TEST_PADDR);
    result = npt_pte_set_flags(result, flags);

    TEST_ASSERT_EQUAL_UINT64(expected, result);
}

/* ========================================================================
 * 测试用例 - 页表项清除
 * ======================================================================== */

/**
 * @brief 测试清除页表项
 */
void test_npt_pte_clear(void)
{
    npt_entry_t entry;
    npt_entry_t result;
    uint64_t flags;

    flags = TEST_FLAGS_R | TEST_FLAGS_W | TEST_FLAGS_X;
    entry = npt_pte_set_type(0ULL, NPT_ENTRY_TYPE_PAGE);
    entry = npt_pte_set_paddr(entry, TEST_PADDR);
    entry = npt_pte_set_flags(entry, flags);

    result = npt_pte_clear(entry);

    TEST_ASSERT_EQUAL_UINT64(0ULL, result);
}

/**
 * @brief 测试清除页表项类型
 */
void test_npt_pte_clear_type(void)
{
    npt_entry_t entry;
    npt_entry_t result;
    uint64_t expected;

    expected = (TEST_PADDR >> 12ULL) << NPT_ENTRY_PADDR_SHIFT;
    expected |= (TEST_FLAGS_R | TEST_FLAGS_W | TEST_FLAGS_X);

    entry = npt_pte_set_type(0ULL, NPT_ENTRY_TYPE_PAGE);
    entry = npt_pte_set_paddr(entry, TEST_PADDR);
    entry = npt_pte_set_flags(entry, (TEST_FLAGS_R | TEST_FLAGS_W | TEST_FLAGS_X));

    result = npt_pte_clear_type(entry);

    TEST_ASSERT_EQUAL_UINT64(expected, result);
}

/**
 * @brief 测试清除标志位
 */
void test_npt_pte_clear_flags(void)
{
    npt_entry_t entry;
    npt_entry_t result;
    uint64_t expected;

    expected = (TEST_PADDR >> 12ULL) << NPT_ENTRY_PADDR_SHIFT;
    expected |= (NPT_ENTRY_TYPE_PAGE << NPT_ENTRY_TYPE_SHIFT);

    entry = npt_pte_set_type(0ULL, NPT_ENTRY_TYPE_PAGE);
    entry = npt_pte_set_paddr(entry, TEST_PADDR);
    entry = npt_pte_set_flags(entry, (TEST_FLAGS_R | TEST_FLAGS_W | TEST_FLAGS_X));

    result = npt_pte_clear_flags(entry);

    TEST_ASSERT_EQUAL_UINT64(expected, result);
}

/* ========================================================================
 * 测试用例 - 页表项查询
 * ======================================================================== */

/**
 * @brief 测试获取页表项类型
 */
void test_npt_pte_get_type(void)
{
    npt_entry_t entry;
    uint64_t type;

    entry = npt_pte_set_type(0ULL, NPT_ENTRY_TYPE_PAGE);
    entry = npt_pte_set_paddr(entry, TEST_PADDR);
    entry = npt_pte_set_flags(entry, (TEST_FLAGS_R | TEST_FLAGS_W | TEST_FLAGS_X));

    type = npt_pte_get_type(entry);

    TEST_ASSERT_EQUAL_UINT64(NPT_ENTRY_TYPE_PAGE, type);
}

/**
 * @brief 测试获取物理地址
 */
void test_npt_pte_get_paddr(void)
{
    npt_entry_t entry;
    uint64_t paddr;

    entry = npt_pte_set_type(0ULL, NPT_ENTRY_TYPE_PAGE);
    entry = npt_pte_set_paddr(entry, TEST_PADDR);
    entry = npt_pte_set_flags(entry, (TEST_FLAGS_R | TEST_FLAGS_W | TEST_FLAGS_X));

    paddr = npt_pte_get_paddr(entry);

    TEST_ASSERT_EQUAL_UINT64(TEST_PADDR, paddr);
}

/**
 * @brief 测试获取标志位
 */
void test_npt_pte_get_flags(void)
{
    npt_entry_t entry;
    uint64_t flags;
    uint64_t expected;

    expected = TEST_FLAGS_R | TEST_FLAGS_W | TEST_FLAGS_X;

    entry = npt_pte_set_type(0ULL, NPT_ENTRY_TYPE_PAGE);
    entry = npt_pte_set_paddr(entry, TEST_PADDR);
    entry = npt_pte_set_flags(entry, expected);

    flags = npt_pte_get_flags(entry);

    TEST_ASSERT_EQUAL_UINT64(expected, flags);
}

/* ========================================================================
 * 测试用例 - 权限检查
 * ======================================================================== */

/**
 * @brief 测试检查可读权限
 */
void test_npt_pte_is_readable(void)
{
    npt_entry_t entry;
    bool is_readable;

    /* 只读 */
    entry = npt_pte_set_flags(0ULL, TEST_FLAGS_R);
    is_readable = npt_pte_is_readable(entry);
    TEST_ASSERT_TRUE(is_readable);

    /* 读写 */
    entry = npt_pte_set_flags(0ULL, TEST_FLAGS_R | TEST_FLAGS_W);
    is_readable = npt_pte_is_readable(entry);
    TEST_ASSERT_TRUE(is_readable);

    /* 只写（不可读） */
    entry = npt_pte_set_flags(0ULL, TEST_FLAGS_W);
    is_readable = npt_pte_is_readable(entry);
    TEST_ASSERT_FALSE(is_readable);
}

/**
 * @brief 测试检查可写权限
 */
void test_npt_pte_is_writable(void)
{
    npt_entry_t entry;
    bool is_writable;

    /* 只写 */
    entry = npt_pte_set_flags(0ULL, TEST_FLAGS_W);
    is_writable = npt_pte_is_writable(entry);
    TEST_ASSERT_TRUE(is_writable);

    /* 读写 */
    entry = npt_pte_set_flags(0ULL, TEST_FLAGS_R | TEST_FLAGS_W);
    is_writable = npt_pte_is_writable(entry);
    TEST_ASSERT_TRUE(is_writable);

    /* 只读（不可写） */
    entry = npt_pte_set_flags(0ULL, TEST_FLAGS_R);
    is_writable = npt_pte_is_writable(entry);
    TEST_ASSERT_FALSE(is_writable);
}

/**
 * @brief 测试检查可执行权限
 */
void test_npt_pte_is_executable(void)
{
    npt_entry_t entry;
    bool is_executable;

    /* 可执行 */
    entry = npt_pte_set_flags(0ULL, TEST_FLAGS_X);
    is_executable = npt_pte_is_executable(entry);
    TEST_ASSERT_TRUE(is_executable);

    /* 读写执行 */
    entry = npt_pte_set_flags(0ULL, TEST_FLAGS_R | TEST_FLAGS_W | TEST_FLAGS_X);
    is_executable = npt_pte_is_executable(entry);
    TEST_ASSERT_TRUE(is_executable);

    /* 只读（不可执行） */
    entry = npt_pte_set_flags(0ULL, TEST_FLAGS_R);
    is_executable = npt_pte_is_executable(entry);
    TEST_ASSERT_FALSE(is_executable);
}

/**
 * @brief 测试检查用户模式访问
 */
void test_npt_pte_is_user(void)
{
    npt_entry_t entry;
    bool is_user;

    /* 用户模式 */
    entry = npt_pte_set_flags(0ULL, TEST_FLAGS_U);
    is_user = npt_pte_is_user(entry);
    TEST_ASSERT_TRUE(is_user);

    /* 用户模式 + 读写 */
    entry = npt_pte_set_flags(0ULL, TEST_FLAGS_U | TEST_FLAGS_R | TEST_FLAGS_W);
    is_user = npt_pte_is_user(entry);
    TEST_ASSERT_TRUE(is_user);

    /* 内核模式（无用户标志） */
    entry = npt_pte_set_flags(0ULL, TEST_FLAGS_R | TEST_FLAGS_W);
    is_user = npt_pte_is_user(entry);
    TEST_ASSERT_FALSE(is_user);
}

/* ========================================================================
 * 测试用例 - 综合测试
 * ======================================================================== */

/**
 * @brief 测试完整的页表项操作流程
 */
void test_npt_pte_complete_flow(void)
{
    npt_entry_t entry;
    uint64_t flags;
    uint64_t type;
    uint64_t paddr;

    /* 1. 创建完整的页表项 */
    flags = TEST_FLAGS_R | TEST_FLAGS_W | TEST_FLAGS_X;
    entry = 0ULL;
    entry = npt_pte_set_type(entry, NPT_ENTRY_TYPE_PAGE);
    entry = npt_pte_set_paddr(entry, TEST_PADDR);
    entry = npt_pte_set_flags(entry, flags);

    /* 2. 验证所有属性 */
    type = npt_pte_get_type(entry);
    TEST_ASSERT_EQUAL_UINT64(NPT_ENTRY_TYPE_PAGE, type);

    paddr = npt_pte_get_paddr(entry);
    TEST_ASSERT_EQUAL_UINT64(TEST_PADDR, paddr);

    TEST_ASSERT_TRUE(npt_pte_is_readable(entry));
    TEST_ASSERT_TRUE(npt_pte_is_writable(entry));
    TEST_ASSERT_TRUE(npt_pte_is_executable(entry));
    TEST_ASSERT_FALSE(npt_pte_is_user(entry));

    /* 3. 清除类型 */
    entry = npt_pte_clear_type(entry);
    type = npt_pte_get_type(entry);
    TEST_ASSERT_EQUAL_UINT64(0ULL, type);

    /* 4. 清除标志位 */
    entry = npt_pte_clear_flags(entry);
    TEST_ASSERT_FALSE(npt_pte_is_readable(entry));
    TEST_ASSERT_FALSE(npt_pte_is_writable(entry));
    TEST_ASSERT_FALSE(npt_pte_is_executable(entry));

    /* 5. 完全清除 */
    entry = npt_pte_clear(entry);
    TEST_ASSERT_EQUAL_UINT64(0ULL, entry);
}

/* ========================================================================
 * 测试用例 - 边界条件
 * ======================================================================== */

/**
 * @brief 测试物理地址边界（4KB 对齐）
 */
void test_npt_pte_paddr_alignment(void)
{
    npt_entry_t entry;
    uint64_t paddr;

    /* 4KB 对齐 */
    entry = npt_pte_set_paddr(0ULL, 0x1000ULL);
    paddr = npt_pte_get_paddr(entry);
    TEST_ASSERT_EQUAL_UINT64(0x1000ULL, paddr);

    /* 1MB 对齐 */
    entry = npt_pte_set_paddr(0ULL, 0x100000ULL);
    paddr = npt_pte_get_paddr(entry);
    TEST_ASSERT_EQUAL_UINT64(0x100000ULL, paddr);

    /* 不对齐（应该被截断） */
    entry = npt_pte_set_paddr(0ULL, 0x1001ULL);
    paddr = npt_pte_get_paddr(entry);
    TEST_ASSERT_EQUAL_UINT64(0x1000ULL, paddr);
}

/**
 * @brief 测试无效页表项
 */
void test_npt_pte_invalid_entry(void)
{
    npt_entry_t entry = 0ULL;

    /* 无效页表项应该返回默认值 */
    TEST_ASSERT_EQUAL_UINT64(0ULL, npt_pte_get_type(entry));
    TEST_ASSERT_EQUAL_UINT64(0ULL, npt_pte_get_paddr(entry));
    TEST_ASSERT_EQUAL_UINT64(0ULL, npt_pte_get_flags(entry));
    TEST_ASSERT_FALSE(npt_pte_is_readable(entry));
    TEST_ASSERT_FALSE(npt_pte_is_writable(entry));
    TEST_ASSERT_FALSE(npt_pte_is_executable(entry));
    TEST_ASSERT_FALSE(npt_pte_is_user(entry));
}

/* ========================================================================
 * 测试主函数
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* 页表项设置测试 */
    RUN_TEST(test_npt_pte_set_type_table);
    RUN_TEST(test_npt_pte_set_type_block);
    RUN_TEST(test_npt_pte_set_type_page);
    RUN_TEST(test_npt_pte_set_paddr);
    RUN_TEST(test_npt_pte_set_flags);
    RUN_TEST(test_npt_pte_set_complete);

    /* 页表项清除测试 */
    RUN_TEST(test_npt_pte_clear);
    RUN_TEST(test_npt_pte_clear_type);
    RUN_TEST(test_npt_pte_clear_flags);

    /* 页表项查询测试 */
    RUN_TEST(test_npt_pte_get_type);
    RUN_TEST(test_npt_pte_get_paddr);
    RUN_TEST(test_npt_pte_get_flags);

    /* 权限检查测试 */
    RUN_TEST(test_npt_pte_is_readable);
    RUN_TEST(test_npt_pte_is_writable);
    RUN_TEST(test_npt_pte_is_executable);
    RUN_TEST(test_npt_pte_is_user);

    /* 综合测试 */
    RUN_TEST(test_npt_pte_complete_flow);

    /* 边界条件测试 */
    RUN_TEST(test_npt_pte_paddr_alignment);
    RUN_TEST(test_npt_pte_invalid_entry);

    return UNITY_END();
}