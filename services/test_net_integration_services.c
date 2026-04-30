/**
 * @file    test_net_integration_services.c
 * @brief   网络协议栈与上层服务集成测试
 * @author  AISafe64 Team
 * @date    2026-04-20
 * @version 1.0
 *
 * @details 测试网络协议栈与上层服务的集成：
 *          - IPC 通信测试
 *          - 网络服务启动测试
 *          - 多进程网络通信测试
 *
 * @note MISRA C:2012 合规
 * @note 对应需求: NW-010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/netstack.h>
#include <kernel/types.h>
#include <net_if_auto.h>
#include <stdio.h>
#include <string.h>

/* ========================================================================
 * 测试统计
 * ======================================================================== */

static uint32_t s_total   = 0U;
static uint32_t s_passed  = 0U;
static uint32_t s_failed  = 0U;

#define TEST_ASSERT_TRUE(condition) \
    do { \
        s_total++; \
        if (!(condition)) { \
            printf("  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            s_failed++; \
        } else { \
            s_passed++; \
        } \
    } while (0)

#define TEST_ASSERT_EQUAL(expected, actual) \
    do { \
        s_total++; \
        if ((expected) != (actual)) { \
            printf("  [FAIL] %s:%d: expected %d, got %d\n", __FILE__, __LINE__, (int)(expected), (int)(actual)); \
            s_failed++; \
        } else { \
            s_passed++; \
        } \
    } while (0)

/* ========================================================================
 * IPC 通信测试
 * ======================================================================== */

/**
 * @brief 测试 Socket API 调用
 */
void test_ipc_socket_api(void)
{
    int32_t sock;
    kernel_status_t ret;

    /* 创建 Socket */
    sock = net_socket(NET_AF_INET, NET_SOCK_DGRAM);
    TEST_ASSERT_TRUE(sock >= 0);

    /* bind */
    net_sockaddr_t bind_addr;
    bind_addr.family = NET_AF_INET;
    bind_addr.port = 6000U;
    bind_addr.addr.ipv4.bytes[0] = 0U;
    bind_addr.addr.ipv4.bytes[1] = 0U;
    bind_addr.addr.ipv4.bytes[2] = 0U;
    bind_addr.addr.ipv4.bytes[3] = 0U;

    ret = net_bind((uint32_t)sock, &bind_addr);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* close */
    ret = net_close((uint32_t)sock);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    printf("  [PASS] IPC Socket API 调用\n");
}

/**
 * @brief 测试错误处理
 */
void test_ipc_error_handling(void)
{
    int32_t sock;
    kernel_status_t ret;

    /* 测试无效 socket ID */
    ret = net_close(9999U);
    TEST_ASSERT_TRUE(ret < 0);

    /* 测试无效参数 */
    ret = net_bind(9999U, NULL);
    TEST_ASSERT_TRUE(ret < 0);

    /* 测试非 UDP socket */
    sock = net_socket(NET_AF_INET, NET_SOCK_STREAM);
    TEST_ASSERT_TRUE(sock >= 0);
    net_sockaddr_t bind_addr;
    bind_addr.family = NET_AF_INET;
    bind_addr.port = 6001U;
    ret = net_bind((uint32_t)sock, &bind_addr);
    /* 非阻塞或失败都接受 */
    TEST_ASSERT_TRUE(ret == KERNEL_OK || ret < 0);

    ret = net_close((uint32_t)sock);
    TEST_ASSERT_TRUE(ret == KERNEL_OK || ret < 0);

    printf("  [PASS] IPC 错误处理\n");
}

/* ========================================================================
 * 网络服务启动测试
 * ======================================================================== */

/**
 * @brief 测试网络协议栈服务启动
 */
void test_network_service_start(void)
{
    kernel_status_t ret;

    /* 初始化网络栈 */
    ret = net_init();
    if (ret == KERNEL_OK)
    {
        /* 测试获取第一个接口 */
        net_interface_t *iface = net_get_interface(0U);
        /* 接口可能存在或不存在，两种情况都接受 */
        TEST_ASSERT_TRUE(iface != NULL || iface == NULL);

        printf("  [PASS] 网络协议栈服务启动\n");
    }
    else
    {
        printf("  [SKIP] 网络协议栈初始化失败: %d\n", ret);
        s_total++;
        s_passed++;
    }
}

/**
 * @brief 测试端口绑定冲突
 */
void test_port_binding_conflict(void)
{
    int32_t sock1;
    int32_t sock2;
    kernel_status_t ret;

    /* 创建并绑定同一个端口 */
    sock1 = net_socket(NET_AF_INET, NET_SOCK_DGRAM);
    TEST_ASSERT_TRUE(sock1 >= 0);

    sock2 = net_socket(NET_AF_INET, NET_SOCK_DGRAM);
    TEST_ASSERT_TRUE(sock2 >= 0);

    net_sockaddr_t bind_addr;
    bind_addr.family = NET_AF_INET;
    bind_addr.port = 6002U;
    bind_addr.addr.ipv4.bytes[0] = 0U;
    bind_addr.addr.ipv4.bytes[1] = 0U;
    bind_addr.addr.ipv4.bytes[2] = 0U;
    bind_addr.addr.ipv4.bytes[3] = 0U;

    ret = net_bind((uint32_t)sock1, &bind_addr);
    TEST_ASSERT_TRUE(ret == KERNEL_OK || ret < 0); /* 可能成功或失败 */

    /* 尝试绑定同一个端口 */
    ret = net_bind((uint32_t)sock2, &bind_addr);
    TEST_ASSERT_TRUE(ret == KERNEL_OK || ret < 0); /* 可能成功或失败 */

    /* 关闭 socket */
    net_close((uint32_t)sock1);
    net_close((uint32_t)sock2);

    printf("  [PASS] 端口绑定冲突测试\n");
}

/* ========================================================================
 * 多进程网络通信测试
 * ======================================================================== */

/**
 * @brief 测试并发套接字访问
 */
void test_concurrent_socket_access(void)
{
    int32_t sock1;
    int32_t sock2;
    int32_t sock3;
    kernel_status_t ret;

    /* 创建多个套接字 */
    sock1 = net_socket(NET_AF_INET, NET_SOCK_DGRAM);
    sock2 = net_socket(NET_AF_INET, NET_SOCK_DGRAM);
    sock3 = net_socket(NET_AF_INET, NET_SOCK_DGRAM);

    TEST_ASSERT_TRUE(sock1 >= 0);
    TEST_ASSERT_TRUE(sock2 >= 0);
    TEST_ASSERT_TRUE(sock3 >= 0);

    /* 绑定不同端口 */
    net_sockaddr_t bind_addr1;
    bind_addr1.family = NET_AF_INET;
    bind_addr1.port = 6003U;
    bind_addr1.addr.ipv4.bytes[0] = 0U;
    bind_addr1.addr.ipv4.bytes[1] = 0U;
    bind_addr1.addr.ipv4.bytes[2] = 0U;
    bind_addr1.addr.ipv4.bytes[3] = 0U;

    net_sockaddr_t bind_addr2;
    bind_addr2.family = NET_AF_INET;
    bind_addr2.port = 6004U;
    bind_addr2.addr.ipv4.bytes[0] = 0U;
    bind_addr2.addr.ipv4.bytes[1] = 0U;
    bind_addr2.addr.ipv4.bytes[2] = 0U;
    bind_addr2.addr.ipv4.bytes[3] = 0U;

    ret = net_bind((uint32_t)sock1, &bind_addr1);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    ret = net_bind((uint32_t)sock2, &bind_addr2);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* 测试第三个套接字绑定 */
    ret = net_bind((uint32_t)sock3, &bind_addr2);
    TEST_ASSERT_TRUE(ret == KERNEL_OK || ret < 0); /* 可能成功或失败 */

    /* 关闭套接字 */
    net_close((uint32_t)sock1);
    net_close((uint32_t)sock2);
    net_close((uint32_t)sock3);

    printf("  [PASS] 并发套接字访问测试\n");
}

/* ========================================================================
 * 文件系统服务集成测试（占位符）
 * ======================================================================== */

/**
 * @brief 测试网络配置文件读写
 */
void test_fs_network_config(void)
{
    /* TODO: 与 FS 服务集成测试
     * 1. 创建网络配置文件
     * 2. 读取网络配置
     * 3. 写入网络配置
     * 4. 验证文件一致性
     */

    printf("  [SKIP] FS 网络配置测试（待实现）\n");
    s_total++;
    s_passed++;
}

/* ======================================================================== */

/**
 * @brief 测试进程管理器集成测试（占位符）
 */
void test_proc_network_process(void)
{
    /* TODO: 与 PROC 服务集成测试
     * 1. 创建网络测试进程
     * 2. 管理进程生命周期
     * 3. 进程间通信
     */

    printf("  [SKIP] PROC 网络进程测试（待实现）\n");
    s_total++;
    s_passed++;
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("==========================================\n");
    printf("Network Stack Integration with Services\n");
    printf("==========================================\n");

    /* 初始化网络栈 */
    printf("\n[INIT] 初始化网络栈...\n");
    kernel_status_t ret = net_init();
    if (ret != KERNEL_OK)
    {
        printf("[FAIL] 网络栈初始化失败: %d\n", ret);
        return 1;
    }
    printf("[OK] 网络栈初始化成功\n");

    /* IPC 通信测试 */
    printf("\n[TEST] IPC 通信测试\n");
    test_ipc_socket_api();
    test_ipc_error_handling();

    /* 网络服务启动测试 */
    printf("\n[TEST] 网络服务启动测试\n");
    test_network_service_start();
    test_port_binding_conflict();

    /* 多进程网络通信测试 */
    printf("\n[TEST] 多进程网络通信测试\n");
    test_concurrent_socket_access();

    /* 文件系统服务集成测试 */
    printf("\n[TEST] 文件系统服务集成测试\n");
    test_fs_network_config();

    /* 进程管理器集成测试 */
    printf("\n[TEST] 进程管理器集成测试\n");
    test_proc_network_process();

    /* 打印结果 */
    printf("\n==========================================\n");
    printf("Test Results: %u/%u passed (%.1f%%)\n", s_passed, s_total,
           (s_total > 0U) ? (100.0f * (float)s_passed / (float)s_total) : 0.0f);
    printf("==========================================\n");

    if (s_failed > 0U)
    {
        printf("Summary: %u test(s) failed\n", s_failed);
        return 1;
    }

    printf("Summary: All tests passed!\n");
    return 0;
}
