/**
 * @file    test_syscall.c
 * @brief   系统调用接口单元测试
 * @author  AISafe64 Team
 * @date    2026-04-05
 * @version 1.0
 *
 * @details 测试系统调用表机制：
 *          - 系统调用表初始化
 *          - 处理函数注册
 *          - 已知/未知系统调用号分发
 *          - 参数校验
 *          - SYS_yield / SYS_getpid 基础 syscall
 *
 * @note TDD RED 阶段：先编写测试，确认编译失败后再实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 宿主机 Mock 基础设施（必须在内核头文件之前包含）
 * ======================================================================== */
#include "mock_kernel.h"

/* ========================================================================
 * 被测模块头文件
 * ======================================================================== */
#include <kernel/syscall.h>

/* ========================================================================
 * 测试辅助：模拟系统调用处理函数
 * ======================================================================== */

/**
 * @brief 简单处理函数：返回固定值 42
 */
static int64_t mock_handler_return42(syscall_frame_t *frame)
{
    (void)frame;
    return 42;
}

/**
 * @brief 处理函数：将 x0 加 1 后返回
 */
static int64_t mock_handler_inc_x0(syscall_frame_t *frame)
{
    if (frame == NULL)
    {
        return -(int64_t)EINVAL;
    }
    return (int64_t)(frame->x0 + 1U);
}

/**
 * @brief 处理函数：返回 x0 + x1
 */
static int64_t mock_handler_add(syscall_frame_t *frame)
{
    if (frame == NULL)
    {
        return -(int64_t)EINVAL;
    }
    return (int64_t)(frame->x0 + frame->x1);
}

/**
 * @brief 模拟 getpid 处理函数：返回固定线程 ID = 1
 */
static int64_t mock_handler_getpid(syscall_frame_t *frame)
{
    (void)frame;
    return 1;
}

/**
 * @brief 模拟 yield 处理函数：返回 0（成功）
 */
static int64_t mock_handler_yield(syscall_frame_t *frame)
{
    (void)frame;
    return 0;
}

/* ========================================================================
 * 测试用例 1：系统调用表初始化
 * ======================================================================== */

/**
 * @brief 验证系统调用表初始化后所有槽位为空
 *
 * @details 初始化后：
 *          - 已注册数量应为 0
 *          - 分发到任意未注册系统调用号应返回 -ENOSYS
 */
static void test_syscall_table_init(void)
{
    printf("测试: 系统调用表初始化...\n");

    syscall_table_init();

    /* 验证：初始注册数量为 0 */
    TEST_ASSERT_EQ(syscall_table_count(), 0U);

    /* 验证：分发到未注册的系统调用号应返回 -ENOSYS */
    syscall_frame_t frame;
    kernel_memset(&frame, 0, sizeof(frame));
    frame.x8 = SYS_getpid;

    int64_t ret = syscall_table_dispatch(&frame);
    TEST_ASSERT_EQ(ret, -(int64_t)ENOSYS);

    printf("  通过: 系统调用表初始化验证完成\n");
}

/* ========================================================================
 * 测试用例 2：注册系统调用处理函数
 * ======================================================================== */

/**
 * @brief 验证注册系统调用处理函数
 *
 * @details 注册后：
 *          - 已注册数量递增
 *          - 重复注册同一号应返回错误
 *          - 使用无效参数应返回错误
 */
static void test_syscall_register(void)
{
    printf("测试: 注册系统调用处理函数...\n");

    syscall_table_init();

    /* 注册 SYS_getpid */
    int32_t ret = syscall_register(SYS_getpid, mock_handler_getpid);
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_EQ(syscall_table_count(), 1U);

    /* 注册 SYS_yield */
    ret = syscall_register(SYS_yield, mock_handler_yield);
    TEST_ASSERT_EQ(ret, 0);
    TEST_ASSERT_EQ(syscall_table_count(), 2U);

    /* 重复注册同一系统调用号应失败 */
    ret = syscall_register(SYS_getpid, mock_handler_getpid);
    TEST_ASSERT_EQ(ret, -(int32_t)EEXIST);

    /* 已注册数量不应增加 */
    TEST_ASSERT_EQ(syscall_table_count(), 2U);

    printf("  通过: 注册系统调用处理函数验证完成\n");
}

/* ========================================================================
 * 测试用例 3：已知系统调用号的分发
 * ======================================================================== */

/**
 * @brief 验证已知系统调用号的正确分发
 *
 * @details 注册处理函数后，分发应调用正确的处理函数
 */
static void test_syscall_dispatch_known(void)
{
    printf("测试: 已知系统调用号分发...\n");

    syscall_table_init();

    /* 注册处理函数 */
    syscall_register((uint32_t)100U, mock_handler_return42);
    syscall_register((uint32_t)101U, mock_handler_inc_x0);
    syscall_register((uint32_t)102U, mock_handler_add);

    /* 分发到 100：返回 42 */
    syscall_frame_t frame;
    kernel_memset(&frame, 0, sizeof(frame));
    frame.x8 = 100U;
    int64_t ret = syscall_table_dispatch(&frame);
    TEST_ASSERT_EQ(ret, 42);

    /* 分发到 101：x0=10，返回 11 */
    kernel_memset(&frame, 0, sizeof(frame));
    frame.x8 = 101U;
    frame.x0 = 10U;
    ret = syscall_table_dispatch(&frame);
    TEST_ASSERT_EQ(ret, 11);

    /* 分发到 102：x0=5, x1=3，返回 8 */
    kernel_memset(&frame, 0, sizeof(frame));
    frame.x8 = 102U;
    frame.x0 = 5U;
    frame.x1 = 3U;
    ret = syscall_table_dispatch(&frame);
    TEST_ASSERT_EQ(ret, 8);

    printf("  通过: 已知系统调用号分发验证完成\n");
}

/* ========================================================================
 * 测试用例 4：未知系统调用号返回错误
 * ======================================================================== */

/**
 * @brief 验证未知系统调用号返回 -ENOSYS
 */
static void test_syscall_dispatch_unknown(void)
{
    printf("测试: 未知系统调用号返回错误...\n");

    syscall_table_init();

    /* 只注册一个 */
    syscall_register((uint32_t)100U, mock_handler_return42);

    /* 分发到未注册的号 */
    syscall_frame_t frame;
    kernel_memset(&frame, 0, sizeof(frame));

    frame.x8 = 0U;
    TEST_ASSERT_EQ(syscall_table_dispatch(&frame), -(int64_t)ENOSYS);

    frame.x8 = 99U;
    TEST_ASSERT_EQ(syscall_table_dispatch(&frame), -(int64_t)ENOSYS);

    frame.x8 = 101U;
    TEST_ASSERT_EQ(syscall_table_dispatch(&frame), -(int64_t)ENOSYS);

    frame.x8 = (uint64_t)0xFFFFU;
    TEST_ASSERT_EQ(syscall_table_dispatch(&frame), -(int64_t)ENOSYS);

    printf("  通过: 未知系统调用号错误返回验证完成\n");
}

/* ========================================================================
 * 测试用例 5：参数校验
 * ======================================================================== */

/**
 * @brief 验证参数校验
 *
 * @details NULL 参数、越界系统调用号等
 */
static void test_syscall_invalid_args(void)
{
    printf("测试: 参数校验...\n");

    syscall_table_init();

    /* 注册处理函数到已知号 */
    syscall_register((uint32_t)50U, mock_handler_return42);

    /* 1. dispatch 传入 NULL frame 应返回 -EINVAL */
    int64_t ret = syscall_table_dispatch(NULL);
    TEST_ASSERT_EQ(ret, -(int64_t)EINVAL);

    /* 2. register 传入 NULL handler 应返回 -EINVAL */
    int32_t reg_ret = syscall_register((uint32_t)51U, NULL);
    TEST_ASSERT_EQ(reg_ret, -(int32_t)EINVAL);

    /* 3. register 使用越界系统调用号应返回 -EINVAL */
    reg_ret = syscall_register(SYSCALL_TABLE_MAX, mock_handler_return42);
    TEST_ASSERT_EQ(reg_ret, -(int32_t)EINVAL);

    reg_ret = syscall_register((uint32_t)0xFFFFFFFFU, mock_handler_return42);
    TEST_ASSERT_EQ(reg_ret, -(int32_t)EINVAL);

    printf("  通过: 参数校验验证完成\n");
}

/* ========================================================================
 * 测试用例 6：SYS_yield / SYS_getpid 基础 syscall
 * ======================================================================== */

/**
 * @brief 验证基础系统调用 SYS_yield 和 SYS_getpid
 *
 * @details 模拟注册和调用基础系统调用
 */
static void test_syscall_priority(void)
{
    printf("测试: SYS_yield / SYS_getpid 基础 syscall...\n");

    syscall_table_init();

    /* 注册 SYS_getpid 和 SYS_yield */
    syscall_register(SYS_getpid, mock_handler_getpid);
    syscall_register(SYS_yield, mock_handler_yield);

    /* 调用 SYS_getpid：应返回 1（模拟线程 ID） */
    syscall_frame_t frame;
    kernel_memset(&frame, 0, sizeof(frame));
    frame.x8 = SYS_getpid;
    int64_t ret = syscall_table_dispatch(&frame);
    TEST_ASSERT_EQ(ret, 1);

    /* 调用 SYS_yield：应返回 0（成功） */
    kernel_memset(&frame, 0, sizeof(frame));
    frame.x8 = SYS_yield;
    ret = syscall_table_dispatch(&frame);
    TEST_ASSERT_EQ(ret, 0);

    /* 验证系统调用号定义正确 */
    TEST_ASSERT_EQ(SYS_getpid, 0U);
    TEST_ASSERT_EQ(SYS_yield, 1U);
    TEST_ASSERT_EQ(SYS_send, 2U);
    TEST_ASSERT_EQ(SYS_receive, 3U);
    TEST_ASSERT_EQ(SYS_reply, 4U);
    TEST_ASSERT_EQ(SYS_notify, 5U);
    TEST_ASSERT_EQ(SYS_map, 6U);
    TEST_ASSERT_EQ(SYS_unmap, 7U);
    TEST_ASSERT_EQ(SYS_capability_create, 8U);
    TEST_ASSERT_EQ(SYS_capability_derive, 9U);

    printf("  通过: 基础 syscall 验证完成\n");
}

/* ========================================================================
 * 测试主函数
 * ======================================================================== */

int main(void)
{
    printf("\n=== 系统调用接口单元测试 (TDD RED) ===\n\n");

    TEST_RESET();

    test_syscall_table_init();
    test_syscall_register();
    test_syscall_dispatch_known();
    test_syscall_dispatch_unknown();
    test_syscall_invalid_args();
    test_syscall_priority();

    TEST_SUMMARY("test_syscall");

    return TEST_RESULT();
}
