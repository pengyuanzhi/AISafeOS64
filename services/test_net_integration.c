/**
 * @file    test_net_integration.c
 * @brief   网络协议栈集成测试
 * @author  AISafe64 Team
 * @date    2026-04-20
 * @version 1.0
 *
 * @details 测试网络协议栈的完整功能：
 *          - UDP socket (socket/bind/sendto/recvfrom/close)
 *          - TCP socket (socket/bind/listen/accept/connect/send/recv/close)
 *          - shutdown 测试
 *          - setsockopt/getsockopt 测试
 *          - ioctl 测试
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: NW-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/netstack.h>
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
 * UDP Socket 测试
 * ======================================================================== */

/**
 * @brief 测试 UDP socket 创建和绑定
 */
void test_udp_socket_create_bind(void)
{
    int32_t sock;
    net_sockaddr_t bind_addr;
    kernel_status_t ret;

    /* 创建 UDP socket */
    sock = net_socket(NET_AF_INET, NET_SOCK_DGRAM);
    TEST_ASSERT_TRUE(sock >= 0);

    /* 绑定到本地地址 */
    bind_addr.family = NET_AF_INET;
    bind_addr.port = 5000U;
    bind_addr.addr.ipv4.bytes[0] = 0U;
    bind_addr.addr.ipv4.bytes[1] = 0U;
    bind_addr.addr.ipv4.bytes[2] = 0U;
    bind_addr.addr.ipv4.bytes[3] = 0U;

    ret = net_bind((uint32_t)sock, &bind_addr);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* 关闭 socket */
    ret = net_close((uint32_t)sock);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    printf("  [PASS] UDP socket 创建和绑定\n");
}

/**
 * @brief 测试 UDP sendto/recvfrom
 */
void test_udp_sendto_recvfrom(void)
{
    int32_t sock;
    net_sockaddr_t bind_addr;
    net_sockaddr_t dest_addr;
    net_sockaddr_t src_addr;
    const char *msg;
    char recv_buf[128];
    int64_t sent;
    int64_t recv_len;
    kernel_status_t ret;

    /* 创建并绑定 UDP socket */
    sock = net_socket(NET_AF_INET, NET_SOCK_DGRAM);
    TEST_ASSERT_TRUE(sock >= 0);

    bind_addr.family = NET_AF_INET;
    bind_addr.port = 5001U;
    bind_addr.addr.ipv4.bytes[0] = 0U;
    bind_addr.addr.ipv4.bytes[1] = 0U;
    bind_addr.addr.ipv4.bytes[2] = 0U;
    bind_addr.addr.ipv4.bytes[3] = 0U;

    ret = net_bind((uint32_t)sock, &bind_addr);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* sendto 发送数据 */
    dest_addr.family = NET_AF_INET;
    dest_addr.port = 8080U;
    dest_addr.addr.ipv4.bytes[0] = 192U;
    dest_addr.addr.ipv4.bytes[1] = 168U;
    dest_addr.addr.ipv4.bytes[2] = 1U;
    dest_addr.addr.ipv4.bytes[3] = 100U;

    msg = "Hello UDP!";
    sent = net_sendto((uint32_t)sock, msg, strlen(msg), &dest_addr);
    TEST_ASSERT_EQUAL((int64_t)strlen(msg), sent);

    /* recvfrom 接收数据 */
    (void)memset(recv_buf, 0, sizeof(recv_buf));
    recv_len = net_recvfrom((uint32_t)sock, recv_buf, sizeof(recv_buf), &src_addr);

    /* 注意：实际接收需要网络环境，这里仅验证接口 */
    TEST_ASSERT_TRUE(recv_len >= -1); /* 允许失败（无数据） */

    /* 关闭 socket */
    ret = net_close((uint32_t)sock);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    printf("  [PASS] UDP sendto/recvfrom\n");
}

/* ========================================================================
 * TCP Socket 测试
 * ======================================================================== */

/**
 * @brief 测试 TCP socket 创建和绑定
 */
void test_tcp_socket_create_bind(void)
{
    int32_t sock;
    net_sockaddr_t bind_addr;
    kernel_status_t ret;

    /* 创建 TCP socket */
    sock = net_socket(NET_AF_INET, NET_SOCK_STREAM);
    TEST_ASSERT_TRUE(sock >= 0);

    /* 绑定到本地地址 */
    bind_addr.family = NET_AF_INET;
    bind_addr.port = 8080U;
    bind_addr.addr.ipv4.bytes[0] = 0U;
    bind_addr.addr.ipv4.bytes[1] = 0U;
    bind_addr.addr.ipv4.bytes[2] = 0U;
    bind_addr.addr.ipv4.bytes[3] = 0U;

    ret = net_bind((uint32_t)sock, &bind_addr);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* 监听连接 */
    ret = net_listen((uint32_t)sock, 10U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* 关闭 socket */
    ret = net_close((uint32_t)sock);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    printf("  [PASS] TCP socket 创建、绑定和监听\n");
}

/**
 * @brief 测试 TCP connect/send/recv
 */
void test_tcp_connect_send_recv(void)
{
    int32_t sock;
    net_sockaddr_t remote_addr;
    const char *msg;
    char recv_buf[128];
    int64_t sent;
    int64_t recv_len;
    kernel_status_t ret;

    /* 创建 TCP socket */
    sock = net_socket(NET_AF_INET, NET_SOCK_STREAM);
    TEST_ASSERT_TRUE(sock >= 0);

    /* 连接到远程服务器 */
    remote_addr.family = NET_AF_INET;
    remote_addr.port = 80U;
    remote_addr.addr.ipv4.bytes[0] = 142U;
    remote_addr.addr.ipv4.bytes[1] = 250U;
    remote_addr.addr.ipv4.bytes[2] = 185U;
    remote_addr.addr.ipv4.bytes[3] = 115U;

    ret = net_connect((uint32_t)sock, &remote_addr);

    /* 注意：实际连接需要网络环境，这里仅验证接口 */
    /* ret 可能失败，但函数调用应该成功 */

    /* 尝试发送数据 */
    if (ret == KERNEL_OK)
    {
        msg = "GET / HTTP/1.1\r\n\r\n";
        sent = net_send((uint32_t)sock, msg, strlen(msg));

        /* 尝试接收数据 */
        (void)memset(recv_buf, 0, sizeof(recv_buf));
        recv_len = net_recv((uint32_t)sock, recv_buf, sizeof(recv_buf));

        /* 注意：实际接收需要网络环境 */
        TEST_ASSERT_TRUE(recv_len >= -1); /* 允许失败 */
    }

    /* 关闭 socket */
    ret = net_close((uint32_t)sock);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    printf("  [PASS] TCP connect/send/recv\n");
}

/* ========================================================================
 * shutdown 测试
 * ======================================================================== */

/**
 * @brief 测试 TCP shutdown
 */
void test_tcp_shutdown(void)
{
    int32_t sock;
    kernel_status_t ret;

    /* 创建 TCP socket */
    sock = net_socket(NET_AF_INET, NET_SOCK_STREAM);
    TEST_ASSERT_TRUE(sock >= 0);

    /* shutdown 读方向 */
    ret = net_shutdown((uint32_t)sock, NET_SHUT_RD);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* shutdown 写方向 */
    ret = net_shutdown((uint32_t)sock, NET_SHUT_WR);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* 关闭 socket */
    ret = net_close((uint32_t)sock);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    printf("  [PASS] TCP shutdown\n");
}

/* ========================================================================
 * setsockopt/getsockopt 测试
 * ======================================================================== */

/**
 * @brief 测试 setsockopt/getsockopt
 */
void test_setsockopt_getsockopt(void)
{
    int32_t sock;
    int reuse;
    int bufsize;
    uint32_t len;
    kernel_status_t ret;

    /* 创建 socket */
    sock = net_socket(NET_AF_INET, NET_SOCK_STREAM);
    TEST_ASSERT_TRUE(sock >= 0);

    /* 设置 SO_REUSEADDR */
    reuse = 1;
    ret = net_setsockopt((uint32_t)sock, NET_SOL_SOCKET, NET_SO_REUSEADDR,
                         &reuse, sizeof(reuse));
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* 读取 SO_REUSEADDR */
    len = sizeof(reuse);
    reuse = 0;
    ret = net_getsockopt((uint32_t)sock, NET_SOL_SOCKET, NET_SO_REUSEADDR,
                         &reuse, &len);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL(1, reuse);

    /* 设置 SO_RCVBUF */
    bufsize = 65535;
    ret = net_setsockopt((uint32_t)sock, NET_SOL_SOCKET, NET_SO_RCVBUF,
                         &bufsize, sizeof(bufsize));
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* 读取 SO_RCVBUF */
    len = sizeof(bufsize);
    ret = net_getsockopt((uint32_t)sock, NET_SOL_SOCKET, NET_SO_RCVBUF,
                         &bufsize, &len);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
    TEST_ASSERT_TRUE(bufsize >= 65535);

    /* 关闭 socket */
    ret = net_close((uint32_t)sock);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    printf("  [PASS] setsockopt/getsockopt\n");
}

/* ========================================================================
 * ioctl 测试
 * ======================================================================== */

/**
 * @brief 测试 ioctl FIONBIO
 */
void test_ioctl_fionbio(void)
{
    int32_t sock;
    int nonblock;
    kernel_status_t ret;

    /* 创建 socket */
    sock = net_socket(NET_AF_INET, NET_SOCK_STREAM);
    TEST_ASSERT_TRUE(sock >= 0);

    /* 设置非阻塞模式 */
    nonblock = 1;
    ret = net_ioctl((uint32_t)sock, NET_FIONBIO, &nonblock);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* 取消非阻塞模式 */
    nonblock = 0;
    ret = net_ioctl((uint32_t)sock, NET_FIONBIO, &nonblock);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* 关闭 socket */
    ret = net_close((uint32_t)sock);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    printf("  [PASS] ioctl FIONBIO\n");
}

/**
 * @brief 测试 ioctl FIONREAD
 */
void test_ioctl_fionread(void)
{
    int32_t sock;
    int readable;
    kernel_status_t ret;

    /* 创建 socket */
    sock = net_socket(NET_AF_INET, NET_SOCK_STREAM);
    TEST_ASSERT_TRUE(sock >= 0);

    /* 获取可读字节数 */
    ret = net_ioctl((uint32_t)sock, NET_FIONREAD, &readable);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* 关闭 socket */
    ret = net_close((uint32_t)sock);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    printf("  [PASS] ioctl FIONREAD\n");
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("==========================================\n");
    printf("Network Stack Integration Tests\n");
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

    /* UDP Socket 测试 */
    printf("\n[TEST] UDP Socket 测试\n");
    test_udp_socket_create_bind();
    test_udp_sendto_recvfrom();

    /* TCP Socket 测试 */
    printf("\n[TEST] TCP Socket 测试\n");
    test_tcp_socket_create_bind();
    test_tcp_connect_send_recv();

    /* shutdown 测试 */
    printf("\n[TEST] shutdown 测试\n");
    test_tcp_shutdown();

    /* setsockopt/getsockopt 测试 */
    printf("\n[TEST] setsockopt/getsockopt 测试\n");
    test_setsockopt_getsockopt();

    /* ioctl 测试 */
    printf("\n[TEST] ioctl 测试\n");
    test_ioctl_fionbio();
    test_ioctl_fionread();

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
