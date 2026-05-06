/**
 * @file    test_net_socket.c
 * @brief   网络套接字 API 单元测试
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details 网络套接字 API 单元测试：
 *          - socket() 测试
 *          - bind() 测试
 *          - listen() 测试
 *          - accept() 测试
 *          - connect() 测试
 *          - recv/send() 测试
 *          - close() 测试
 *
 * @note MISRA-C:2012 合规
 * @note TDD: RED - 先写测试
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ========================================================================
 * 简易测试框架
 * ======================================================================== */

static uint32_t s_total   = 0U;
static uint32_t s_passed  = 0U;
static uint32_t s_failed  = 0U;

#define TEST_ASSERT_EQ(a, b)                                               \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if ((int64_t)(a) == (int64_t)(b)) { s_passed++; }                  \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 %lld 实际 %lld\n",                   \
                   __FILE__, __LINE__,                                      \
                   (long long)(int64_t)(b), (long long)(int64_t)(a));       \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_GE(a, b)                                               \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if ((int64_t)(a) >= (int64_t)(b)) { s_passed++; }                  \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 %lld >= %lld\n",                     \
                   __FILE__, __LINE__,                                      \
                   (long long)(int64_t)(a), (long long)(int64_t)(b));       \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_TRUE(cond)                                             \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if (cond) { s_passed++; }                                          \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 true: %s\n",                         \
                   __FILE__, __LINE__, #cond);                              \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_FALSE(cond)                                            \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if (!(cond)) { s_passed++; }                                       \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 false: %s\n",                        \
                   __FILE__, __LINE__, #cond);                              \
        }                                                                  \
    } while (0)

#define TEST_RUN(name)                                                     \
    do                                                                     \
    {                                                                      \
        printf("  [RUN] %s\n", #name);                                     \
        test_##name();                                                     \
    } while (0)

/* ========================================================================
 * 常量定义
 * ======================================================================== */

#define NET_AF_INET            2U
#define NET_SOCK_STREAM       1U
#define NET_SOCK_DGRAM        2U
#define NET_IPPROTO_TCP       6U
#define NET_IPPROTO_UDP       17U
#define NET_MAX_SOCKETS       64U

typedef enum
{
    NET_TCP_CLOSED    = 0U,
    NET_TCP_LISTEN    = 1U,
    NET_TCP_SYN_SENT  = 2U,
    NET_TCP_SYN_RCVD  = 3U,
    NET_TCP_ESTABLISHED = 4U
} net_tcp_state_t;

typedef struct
{
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    char     sin_zero[8];
} net_sockaddr_in_t;

typedef struct
{
    net_sockaddr_in_t addr;
    uint32_t          ip;
    uint16_t          port;
    uint32_t          state;
    bool              in_use;
} net_socket_mock_t;

/* ========================================================================
 * Mock API 实现
 * ======================================================================== */

static net_socket_mock_t s_sockets[NET_MAX_SOCKETS];
static bool               s_socket_initialized = false;

extern int32_t net_socket_init(void);
extern int32_t net_socket(int32_t domain, int32_t type, int32_t protocol);
extern int32_t net_bind(int32_t fd, const net_sockaddr_in_t *addr, uint32_t addr_len);
extern int32_t net_listen(int32_t fd, int32_t backlog);
extern int32_t net_accept(int32_t fd, net_sockaddr_in_t *addr, uint32_t *addr_len);
extern int32_t net_connect(int32_t fd, const net_sockaddr_in_t *addr, uint32_t addr_len);
extern int32_t net_recv(int32_t fd, void *buf, uint32_t len);
extern int32_t net_send(int32_t fd, const void *buf, uint32_t len);
extern int32_t net_close(int32_t fd);

/* ========================================================================
 * 测试用例
 * ======================================================================== */

void test_socket_init(void)
{
    int32_t ret = net_socket_init();
    TEST_ASSERT_EQ(ret, 0);
}

void test_socket_create_tcp(void)
{
    int32_t fd = net_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TCP);
    TEST_ASSERT_GE(fd, 3);
}

void test_socket_create_udp(void)
{
    int32_t fd = net_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
    TEST_ASSERT_GE(fd, 3);
}

void test_socket_invalid_domain(void)
{
    int32_t fd = net_socket(999, NET_SOCK_STREAM, NET_IPPROTO_TCP);
    TEST_ASSERT_TRUE(fd < 0);
}

void test_socket_invalid_type(void)
{
    int32_t fd = net_socket(NET_AF_INET, 999, NET_IPPROTO_TCP);
    TEST_ASSERT_TRUE(fd < 0);
}

void test_socket_bind(void)
{
    int32_t fd;
    net_sockaddr_in_t addr = {0};

    fd = net_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TCP);
    TEST_ASSERT_GE(fd, 3);

    addr.sin_family = NET_AF_INET;
    addr.sin_port = 8080;
    addr.sin_addr = 0x0A000001U; /* 10.0.0.1 */

    int32_t ret = net_bind(fd, &addr, sizeof(addr));
    TEST_ASSERT_EQ(ret, 0);

    net_close(fd);
}

void test_socket_bind_invalid_addr(void)
{
    int32_t fd;
    net_sockaddr_in_t addr = {0};

    fd = net_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TCP);
    TEST_ASSERT_GE(fd, 3);

    addr.sin_family = NET_AF_INET;
    addr.sin_port = 0U; /* 无效端口 */

    int32_t ret = net_bind(fd, &addr, sizeof(addr));
    TEST_ASSERT_TRUE(ret < 0);

    net_close(fd);
}

void test_socket_listen(void)
{
    int32_t fd;
    net_sockaddr_in_t addr = {0};

    fd = net_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TCP);
    TEST_ASSERT_GE(fd, 3);

    addr.sin_family = NET_AF_INET;
    addr.sin_port = 8080;
    addr.sin_addr = 0x0A000001U; /* 10.0.0.1 */

    (void)net_bind(fd, &addr, sizeof(addr));

    int32_t ret = net_listen(fd, 10);
    TEST_ASSERT_EQ(ret, 0);

    net_close(fd);
}

void test_socket_listen_udp(void)
{
    int32_t fd;

    fd = net_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
    TEST_ASSERT_GE(fd, 3);

    int32_t ret = net_listen(fd, 10);
    TEST_ASSERT_TRUE(ret < 0);

    net_close(fd);
}

void test_socket_accept(void)
{
    int32_t listen_fd;
    int32_t conn_fd;
    net_sockaddr_in_t addr = {0};
    net_sockaddr_in_t client_addr = {0};
    uint32_t addr_len = sizeof(client_addr);

    /* 创建监听套接字 */
    listen_fd = net_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TCP);
    TEST_ASSERT_GE(listen_fd, 3);

    addr.sin_family = NET_AF_INET;
    addr.sin_port = 8080;
    addr.sin_addr = 0x0A000001U; /* 10.0.0.1 */

    (void)net_bind(listen_fd, &addr, sizeof(addr));
    (void)net_listen(listen_fd, 10);

    /* 接受连接 */
    conn_fd = net_accept(listen_fd, &client_addr, &addr_len);
    TEST_ASSERT_GE(conn_fd, 3);

    net_close(conn_fd);
    net_close(listen_fd);
}

void test_socket_connect(void)
{
    int32_t fd;
    net_sockaddr_in_t addr = {0};

    fd = net_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TCP);
    TEST_ASSERT_GE(fd, 3);

    addr.sin_family = NET_AF_INET;
    addr.sin_port = 8080;
    addr.sin_addr = 0x0A000002U; /* 10.0.0.2 */

    int32_t ret = net_connect(fd, &addr, sizeof(addr));
    TEST_ASSERT_EQ(ret, 0);

    net_close(fd);
}

void test_socket_recv_send(void)
{
    int32_t fd;
    char buf[256];
    const char *msg = "Hello, Network!";
    int32_t sent, received;

    /* 创建套接字 */
    fd = net_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TCP);
    TEST_ASSERT_GE(fd, 3);

    /* 模拟连接 */
    /* 省略 net_connect */

    /* 发送数据 */
    sent = net_send(fd, msg, (uint32_t)strlen(msg));
    TEST_ASSERT_EQ(sent, (int32_t)strlen(msg));

    /* 接收数据 */
    received = net_recv(fd, buf, sizeof(buf));
    TEST_ASSERT_GE(received, (int32_t)strlen(msg));

    net_close(fd);
}

void test_socket_close(void)
{
    int32_t fd;

    fd = net_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TCP);
    TEST_ASSERT_GE(fd, 3);

    int32_t ret = net_close(fd);
    TEST_ASSERT_EQ(ret, 0);
}

void test_socket_multiple(void)
{
    int32_t fds[10];
    uint32_t i;

    for (i = 0U; i < 10U; i++)
    {
        fds[i] = net_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TCP);
        TEST_ASSERT_GE(fds[i], 3);
    }

    for (i = 0U; i < 10U; i++)
    {
        net_close(fds[i]);
    }
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("\n");
    printf("============================================\n");
    printf("  网络套接字 API 单元测试\n");
    printf("============================================\n");
    printf("\n");

    printf("=== 套接字初始化 ===\n");
    TEST_RUN(socket_init);

    printf("\n=== 套接字创建 ===\n");
    TEST_RUN(socket_create_tcp);
    TEST_RUN(socket_create_udp);
    TEST_RUN(socket_invalid_domain);
    TEST_RUN(socket_invalid_type);

    printf("\n=== 套接字绑定 ===\n");
    TEST_RUN(socket_bind);
    TEST_RUN(socket_bind_invalid_addr);

    printf("\n=== 套接字监听 ===\n");
    TEST_RUN(socket_listen);
    TEST_RUN(socket_listen_udp);

    printf("\n=== 套接字连接 ===\n");
    TEST_RUN(socket_accept);
    TEST_RUN(socket_connect);

    printf("\n=== 数据收发 ===\n");
    TEST_RUN(socket_recv_send);

    printf("\n=== 套接字关闭 ===\n");
    TEST_RUN(socket_close);
    TEST_RUN(socket_multiple);

    printf("\n");
    printf("============================================\n");
    printf("  测试总结\n");
    printf("============================================\n");
    printf("  总测试数: %u\n", s_total);
    printf("  通过: %u\n", s_passed);
    printf("  失败: %u\n", s_failed);
    printf("============================================\n");
    printf("\n");

    return (s_failed > 0) ? 1 : 0;
}
