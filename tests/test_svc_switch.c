/**
 * @file    test_svc_switch.c
 * @brief   SVC 切换与系统调用分发器宿主机单元测试
 * @author  AISafe64 Team
 * @date    2026-04-05
 * @version 1.0
 *
 * @details 测试 syscall_handler 的 SVC 切换分发逻辑：
 *          - syscall_frame_t 布局验证（与 exception.S 一致）
 *          - NULL frame 安全处理
 *          - 线程管理系统调用分发（YIELD / GET_ID）
 *          - IPC 系统调用参数传递
 *          - 能力系统调用参数传递
 *          - 未实现/无效系统调用返回 -ENOSYS
 *          - 调试系统调用 NULL 指针保护
 *
 *          GREEN 阶段：test_thread_get_id 已实现 kthread_get_current_tid()，返回线程 ID
 *          但当前实现返回 -ENOSYS，此测试将失败。
 *
 * @note TDD RED→GREEN 阶段完成
 * @note 对应需求: API-001~004
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 宿主机 Mock 基础设施（必须在内核头文件之前包含）
 * ======================================================================== */
#include "mock_kernel.h"

/* ========================================================================
 * 被测模块头文件（syscall.h 仅依赖 types.h + errno.h，均已被 mock 拦截）
 * ======================================================================== */
#include <kernel/syscall.h>

/* ========================================================================
 * syscall_dispatch.c 需要的外部函数 Stub
 *
 * @details syscall_dispatch.c 引用以下外部函数。
 *          由于其头文件包含 ARM64 内联汇编，无法在 x86 上编译，
 *          因此直接在此声明函数原型并提供空实现。
 *          函数签名必须与 syscall_dispatch.c 的调用约定一致。
 * ======================================================================== */

/* --- 调度器 --- */

static uint32_t g_schedule_called = 0U;

void schedule(void)
{
    g_schedule_called++;
}

/* --- HAL UART --- */

void hal_uart_puts(uint64_t base, const char *str)
{
    (void)base;
    (void)str;
}

void hal_uart_putc(uint64_t base, char c)
{
    (void)base;
    (void)c;
}

/* --- 调度器: 获取当前线程 ID --- */

/**
 * @brief 模拟 kthread_get_current_tid() 返回固定线程 ID
 *
 * @details syscall_dispatch.c 通过 extern kthread_get_current_tid() 获取线程 ID。
 *          宿主机测试返回模拟值 1。
 */
static uint32_t g_mock_current_tid = 1U;

uint32_t kthread_get_current_tid(void)
{
    return g_mock_current_tid;
}

/* --- IPC 通道（匹配 ipc_channel.h 签名）--- */

static uint32_t g_last_ch_owner = 0U;

kernel_status_t ipc_channel_create(uint32_t owner_tid, uint32_t *ch_id)
{
    g_last_ch_owner = owner_tid;
    if (ch_id != NULL)
    {
        *ch_id = 100U;
    }
    return KERNEL_OK;
}

kernel_status_t ipc_channel_destroy(uint32_t ch_id)
{
    (void)ch_id;
    return KERNEL_OK;
}

/* --- IPC 连接 --- */

static uint32_t g_last_conn_client = 0U;
static uint32_t g_last_conn_ch = 0U;

kernel_status_t ipc_connect_attach(uint32_t client_tid,
                                    uint32_t ch_id,
                                    uint32_t *conn_id)
{
    g_last_conn_client = client_tid;
    g_last_conn_ch = ch_id;
    if (conn_id != NULL)
    {
        *conn_id = 200U;
    }
    return KERNEL_OK;
}

kernel_status_t ipc_connect_detach(uint32_t conn_id)
{
    (void)conn_id;
    return KERNEL_OK;
}

/* --- IPC 消息（匹配 ipc_endpoint.h 签名）--- */

kernel_status_t ipc_msg_send(uint32_t ep_id,
                              uint64_t tag_value,
                              const void *send_buf,
                              uint32_t send_size,
                              void *recv_buf,
                              uint32_t recv_size)
{
    (void)ep_id;
    (void)tag_value;
    (void)send_buf;
    (void)send_size;
    (void)recv_buf;
    (void)recv_size;
    return KERNEL_OK;
}

kernel_status_t ipc_msg_receive(uint32_t ep_id,
                                 uint64_t *tag_value,
                                 void *recv_buf,
                                 uint32_t recv_size)
{
    (void)ep_id;
    (void)tag_value;
    (void)recv_buf;
    (void)recv_size;
    return KERNEL_OK;
}

kernel_status_t ipc_msg_reply(uint32_t ep_id,
                               int32_t status,
                               const void *reply_buf,
                               uint32_t reply_size)
{
    (void)ep_id;
    (void)status;
    (void)reply_buf;
    (void)reply_size;
    return KERNEL_OK;
}

/* --- IPC Pulse（匹配 ipc_channel.h 签名）--- */

kernel_status_t ipc_pulse_send(uint32_t conn_id,
                                uint8_t prio,
                                int32_t code,
                                int32_t value)
{
    (void)conn_id;
    (void)prio;
    (void)code;
    (void)value;
    return KERNEL_OK;
}

/* --- IPC 通知（匹配 ipc_notification.h 签名）--- */

kernel_status_t ipc_notification_signal(uint32_t notif_id, uint64_t signals)
{
    (void)notif_id;
    (void)signals;
    return KERNEL_OK;
}

kernel_status_t ipc_notification_wait(uint32_t notif_id,
                                       uint64_t waited_mask,
                                       uint64_t *triggered)
{
    (void)notif_id;
    (void)waited_mask;
    if (triggered != NULL)
    {
        *triggered = 0x1ULL;
    }
    return KERNEL_OK;
}

/* --- CSpace（匹配 cspace.h 签名）--- */

/**
 * @brief 模拟 cspace_t 结构（仅包含 syscall_dispatch.c 访问的字段）
 */
typedef struct
{
    struct
    {
        uint32_t type;
        uint32_t id;
        int32_t ref_count;
        uint32_t parent_id;
        void *children_next;
        void *children_prev;
        void *sibling_next;
        void *sibling_prev;
        void *global_next;
        void *global_prev;
    } header;
    void     *cap_table;
    uint32_t  capacity;
    uint32_t  used_count;
    uint32_t  root_slot;
    uint32_t  free_head;
    void     *child_cspaces_next;
    void     *child_cspaces_prev;
    void     *cspace_node_next;
    void     *cspace_node_prev;
    uint32_t  lock_next_ticket;
    uint32_t  lock_serving_ticket;
    uint32_t  lock_cpu_id;
    uint32_t  lock_nest_count;
} mock_cspace_t;

static uint32_t g_last_cspace_capacity = 0U;
static mock_cspace_t g_mock_cspace;

kernel_status_t cspace_create(uint32_t capacity, void **out_cspace)
{
    g_last_cspace_capacity = capacity;
    if (out_cspace != NULL)
    {
        kernel_memset(&g_mock_cspace, 0, sizeof(g_mock_cspace));
        g_mock_cspace.header.id = 42U;
        g_mock_cspace.capacity = capacity;
        *out_cspace = &g_mock_cspace;
    }
    return KERNEL_OK;
}

/* --- 能力操作（匹配 capability.h 签名，使用 uint32_t 作为 cap_slot_t）--- */

static uint32_t g_last_revoke_cspace = 0U;
static uint32_t g_last_revoke_slot = 0U;

kernel_status_t cap_copy(uint32_t src_cs, uint32_t src_slot,
                          uint32_t dst_cs, uint32_t dst_slot,
                          uint8_t rights_mask)
{
    (void)src_cs;
    (void)src_slot;
    (void)dst_cs;
    (void)dst_slot;
    (void)rights_mask;
    return KERNEL_OK;
}

kernel_status_t cap_move(uint32_t src_cs, uint32_t src_slot,
                          uint32_t dst_cs, uint32_t dst_slot)
{
    (void)src_cs;
    (void)src_slot;
    (void)dst_cs;
    (void)dst_slot;
    return KERNEL_OK;
}

kernel_status_t cap_revoke(uint32_t cspace_root, uint32_t slot)
{
    g_last_revoke_cspace = cspace_root;
    g_last_revoke_slot = slot;
    return KERNEL_OK;
}

kernel_status_t cap_delete(uint32_t cspace_root, uint32_t slot)
{
    (void)cspace_root;
    (void)slot;
    return KERNEL_OK;
}

/* ========================================================================
 * 测试用例 1：syscall_frame_t 布局验证
 * ======================================================================== */

static void test_frame_layout(void)
{
    printf("测试: syscall_frame_t 布局...\n");

    TEST_ASSERT_EQ(sizeof(syscall_frame_t), 72U);

    TEST_ASSERT_EQ(offsetof(syscall_frame_t, x0), 0U);
    TEST_ASSERT_EQ(offsetof(syscall_frame_t, x1), 8U);
    TEST_ASSERT_EQ(offsetof(syscall_frame_t, x2), 16U);
    TEST_ASSERT_EQ(offsetof(syscall_frame_t, x3), 24U);
    TEST_ASSERT_EQ(offsetof(syscall_frame_t, x4), 32U);
    TEST_ASSERT_EQ(offsetof(syscall_frame_t, x5), 40U);
    TEST_ASSERT_EQ(offsetof(syscall_frame_t, x6), 48U);
    TEST_ASSERT_EQ(offsetof(syscall_frame_t, x7), 56U);
    TEST_ASSERT_EQ(offsetof(syscall_frame_t, x8), 64U);

    printf("  通过: syscall_frame_t 布局正确 (72 字节, 偏移 x0-x8 匹配)\n");
}

/* ========================================================================
 * 测试用例 2：NULL frame 安全处理
 * ======================================================================== */

static void test_null_frame(void)
{
    printf("测试: NULL frame 安全处理...\n");

    syscall_handler(NULL);

    TEST_ASSERT_TRUE(true);

    printf("  通过: syscall_handler(NULL) 安全返回\n");
}

/* ========================================================================
 * 测试用例 3：SYS_THREAD_YIELD 分发
 * ======================================================================== */

static void test_thread_yield(void)
{
    syscall_frame_t frame;

    printf("测试: SYS_THREAD_YIELD 分发...\n");

    g_schedule_called = 0U;
    kernel_memset(&frame, 0, sizeof(frame));
    frame.x8 = SYS_THREAD_YIELD;

    syscall_handler(&frame);

    TEST_ASSERT_EQ(g_schedule_called, 1U);
    TEST_ASSERT_EQ(frame.x0, 0U);

    printf("  通过: SYS_THREAD_YIELD 调用 schedule()，返回 0\n");
}

/* ========================================================================
 * 测试用例 4：SYS_THREAD_GET_ID 分发（GREEN — 已实现）
 * ======================================================================== */

/**
 * @brief 验证 SYS_THREAD_GET_ID 返回正数线程 ID
 *
 * @details GREEN 阶段：已通过 kthread_get_current_tid() 获取当前线程 ID。
 *          测试期望返回正数（> 0）的线程 ID。
 */
static void test_thread_get_id(void)
{
    syscall_frame_t frame;

    printf("测试: SYS_THREAD_GET_ID 分发...\n");

    kernel_memset(&frame, 0, sizeof(frame));
    frame.x8 = SYS_THREAD_GET_ID;

    syscall_handler(&frame);

    /* GREEN: 期望返回正数线程 ID（> 0） */
    TEST_ASSERT_GT((int64_t)frame.x0, 0);

    printf("  通过: SYS_THREAD_GET_ID 返回线程 ID = %lu\n",
           (unsigned long)frame.x0);
}

/* ========================================================================
 * 测试用例 5：无效系统调用类别返回 -ENOSYS
 * ======================================================================== */

static void test_invalid_category(void)
{
    syscall_frame_t frame;

    printf("测试: 无效系统调用类别返回 -ENOSYS...\n");

    kernel_memset(&frame, 0, sizeof(frame));
    frame.x8 = 0xFF00U;

    syscall_handler(&frame);

    TEST_ASSERT_EQ((int64_t)frame.x0, -(int64_t)ENOSYS);

    printf("  通过: 无效类别 0xFF00 返回 -ENOSYS\n");
}

/* ========================================================================
 * 测试用例 6：未实现系统调用返回 -ENOSYS
 * ======================================================================== */

static void test_unimplemented_syscall(void)
{
    syscall_frame_t frame;

    printf("测试: 未实现系统调用返回 -ENOSYS...\n");

    kernel_memset(&frame, 0, sizeof(frame));
    frame.x8 = SYS_THREAD_CREATE;

    syscall_handler(&frame);

    TEST_ASSERT_EQ((int64_t)frame.x0, -(int64_t)ENOSYS);

    printf("  通过: SYS_THREAD_CREATE 返回 -ENOSYS\n");
}

/* ========================================================================
 * 测试用例 7：SYS_DEBUG_PRINT NULL 指针保护
 * ======================================================================== */

static void test_debug_print_null(void)
{
    syscall_frame_t frame;

    printf("测试: SYS_DEBUG_PRINT NULL 指针保护...\n");

    kernel_memset(&frame, 0, sizeof(frame));
    frame.x8 = SYS_DEBUG_PRINT;
    frame.x0 = 0U;
    frame.x1 = 10U;

    syscall_handler(&frame);

    TEST_ASSERT_EQ((int64_t)frame.x0, -(int64_t)EINVAL);

    printf("  通过: NULL 字符串返回 -EINVAL\n");
}

/* ========================================================================
 * 测试用例 8：IPC 通道创建参数传递
 * ======================================================================== */

static void test_ipc_channel_create_dispatch(void)
{
    syscall_frame_t frame;

    printf("测试: IPC 通道创建参数传递...\n");

    g_last_ch_owner = 0U;
    kernel_memset(&frame, 0, sizeof(frame));
    frame.x8 = SYS_CHANNEL_CREATE;
    frame.x0 = 5U;

    syscall_handler(&frame);

    TEST_ASSERT_EQ(g_last_ch_owner, 5U);
    TEST_ASSERT_EQ(frame.x0, 100U);

    printf("  通过: owner_tid=%u 正确传递, ch_id=%lu 返回\n",
           g_last_ch_owner, (unsigned long)frame.x0);
}

/* ========================================================================
 * 测试用例 9：CSpace 创建返回 header.id
 * ======================================================================== */

static void test_cspace_create_dispatch(void)
{
    syscall_frame_t frame;

    printf("测试: CSpace 创建分发...\n");

    g_last_cspace_capacity = 0U;
    kernel_memset(&frame, 0, sizeof(frame));
    frame.x8 = SYS_CSPACE_CREATE;
    frame.x0 = 32U;

    syscall_handler(&frame);

    TEST_ASSERT_EQ(g_last_cspace_capacity, 32U);
    TEST_ASSERT_EQ(frame.x0, 42U);

    printf("  通过: capacity=%u 传递, cspace_id=%lu 返回\n",
           g_last_cspace_capacity, (unsigned long)frame.x0);
}

/* ========================================================================
 * 测试用例 10：能力撤销参数传递
 * ======================================================================== */

static void test_capability_param_passing(void)
{
    syscall_frame_t frame;

    printf("测试: 能力撤销参数传递...\n");

    g_last_revoke_cspace = 0U;
    g_last_revoke_slot = 0U;
    kernel_memset(&frame, 0, sizeof(frame));
    frame.x8 = SYS_CAP_REVOKE;
    frame.x0 = 10U;
    frame.x1 = 3U;

    syscall_handler(&frame);

    TEST_ASSERT_EQ(g_last_revoke_cspace, 10U);
    TEST_ASSERT_EQ(g_last_revoke_slot, 3U);
    TEST_ASSERT_EQ(frame.x0, 0U);

    printf("  通过: cspace_root=%u, slot=%u 正确传递\n",
           g_last_revoke_cspace, g_last_revoke_slot);
}

/* ========================================================================
 * 测试主函数
 * ======================================================================== */

int main(void)
{
    printf("\n=== SVC 切换与系统调用分发器测试 ===\n\n");

    TEST_RESET();

    test_frame_layout();
    test_null_frame();
    test_thread_yield();
    test_thread_get_id();       /* GREEN: 现已通过 */
    test_invalid_category();
    test_unimplemented_syscall();
    test_debug_print_null();
    test_ipc_channel_create_dispatch();
    test_cspace_create_dispatch();
    test_capability_param_passing();

    TEST_SUMMARY("test_svc_switch");

    return TEST_RESULT();
}
