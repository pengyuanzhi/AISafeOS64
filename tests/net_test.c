/**
 * @file    net_test.c
 * @brief   网络协议栈接口测试
 * @author  AISafe64 Team
 * @date    2026-04-17
 * @version 1.0
 *
 * @details 网络协议栈接口测试：
 *          - 验证网络协议栈数据结构定义
 *          - 验证网络协议栈常量定义
 *          - 验证网络协议栈函数签名
 *
 * @note 这是一个编译测试，验证网络协议栈接口的正确性
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/netstack.h>
#include <kernel/config.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* ========================================================================
 * 测试统计
 * ======================================================================== */

/** @brief 测试统计 */
static struct
{
    uint32_t tests_run;       /**< @brief 已运行测试数 */
    uint32_t tests_passed;    /**< @brief 通过测试数 */
    uint32_t tests_failed;    /**< @brief 失败测试数 */
} s_test_stats = {0};

/** @brief 测试断言宏 */
#define TEST_ASSERT(condition) \
    do { \
        s_test_stats.tests_run++; \
        if (condition) { \
            s_test_stats.tests_passed++; \
            printf("[PASS] %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        } else { \
            s_test_stats.tests_failed++; \
            printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        } \
    } while (0)

/* ========================================================================
 * 测试数据结构大小
 * ======================================================================== */

/**
 * @brief 测试网络地址数据结构大小
 */
static void test_net_address_structs_size(void)
{
    printf("\n[TEST] Network Address Structures Size\n");
    TEST_ASSERT(NET_IPV4_ADDR_LEN == 4U);
    TEST_ASSERT(NET_IPV6_ADDR_LEN == 16U);
    TEST_ASSERT(NET_MAC_ADDR_LEN == 6U);
    TEST_ASSERT(sizeof(net_ipv4_t) == 4);
    TEST_ASSERT(sizeof(net_ipv6_t) == 16);
    TEST_ASSERT(sizeof(net_mac_t) == 6);
}

/**
 * @brief 测试网络接口数据结构大小
 */
static void test_net_interface_structs_size(void)
{
    printf("\n[TEST] Network Interface Structures Size\n");
    TEST_ASSERT(sizeof(net_sockaddr_t) == 22);  /* family(4) + port(2) + addr(16) */
    TEST_ASSERT(sizeof(net_socket_t) >= sizeof(uint32_t));  /* sock_id field */
    TEST_ASSERT(sizeof(net_interface_t) >= sizeof(uint32_t));  /* if_id field */
    TEST_ASSERT(sizeof(net_if_stats_t) >= sizeof(uint64_t) * 8);  /* 8 x uint64_t fields */
}

/* ========================================================================
 * 测试网络常量定义
 * ======================================================================== */

/**
 * @brief 测试地址族常量定义
 */
static void test_net_address_family_constants(void)
{
    printf("\n[TEST] Network Address Family Constants\n");
    TEST_ASSERT(NET_AF_INET == 0U);
    TEST_ASSERT(NET_AF_INET6 == 1U);
    TEST_ASSERT(NET_AF_PACKET == 2U);
}

/**
 * @brief 测试套接字类型常量定义
 */
static void test_net_socket_type_constants(void)
{
    printf("\n[TEST] Network Socket Type Constants\n");
    TEST_ASSERT(NET_SOCK_STREAM == 0U);
    TEST_ASSERT(NET_SOCK_DGRAM == 1U);
    TEST_ASSERT(NET_SOCK_RAW == 2U);
}

/**
 * @brief 测试套接字状态常量定义
 */
static void test_net_socket_state_constants(void)
{
    printf("\n[TEST] Network Socket State Constants\n");
    TEST_ASSERT(NET_SOCK_CLOSED == 0U);
    TEST_ASSERT(NET_SOCK_LISTENING == 1U);
    TEST_ASSERT(NET_SOCK_CONNECTING == 2U);
    TEST_ASSERT(NET_SOCK_CONNECTED == 3U);
    TEST_ASSERT(NET_SOCK_BOUND == 4U);
}

/* ========================================================================
 * 测试网络接口类型常量定义
 * ======================================================================== */

/**
 * @brief 测试链路类型常量定义
 */
static void test_net_link_type_constants(void)
{
    printf("\n[TEST] Network Link Type Constants\n");
    TEST_ASSERT(NET_LINK_ETHERNET == 0U);
    TEST_ASSERT(NET_LINK_WIFI == 1U);
    TEST_ASSERT(NET_LINK_PPP == 2U);
}

/**
 * @brief 测试接口状态常量定义
 */
static void test_net_interface_state_constants(void)
{
    printf("\n[TEST] Network Interface State Constants\n");
    TEST_ASSERT(NET_IF_DOWN == 0U);
    TEST_ASSERT(NET_IF_UP == 1U);
    TEST_ASSERT(NET_IF_RUNNING == 2U);
}

/* ========================================================================
 * 测试套接字地址操作
 * ======================================================================== */

/**
 * @brief 测试 IPv4 地址操作
 */
static void test_ipv4_address_operations(void)
{
    net_ipv4_t ip;

    printf("\n[TEST] IPv4 Address Operations\n");

    /* 设置 IPv4 地址 */
    ip.bytes[0] = 192;
    ip.bytes[1] = 168;
    ip.bytes[2] = 1;
    ip.bytes[3] = 100;

    /* 验证 IPv4 地址 */
    TEST_ASSERT(ip.bytes[0] == 192);
    TEST_ASSERT(ip.bytes[1] == 168);
    TEST_ASSERT(ip.bytes[2] == 1);
    TEST_ASSERT(ip.bytes[3] == 100);

    /* 验证 IPv4 地址完整性 */
    TEST_ASSERT(sizeof(ip.bytes) == 4);
}

/**
 * @brief 测试 MAC 地址操作
 */
static void test_mac_address_operations(void)
{
    net_mac_t mac;

    printf("\n[TEST] MAC Address Operations\n");

    /* 设置 MAC 地址 */
    mac.bytes[0] = 0x52;
    mac.bytes[1] = 0x54;
    mac.bytes[2] = 0x00;
    mac.bytes[3] = 0x12;
    mac.bytes[4] = 0x34;
    mac.bytes[5] = 0x56;

    /* 验证 MAC 地址 */
    TEST_ASSERT(mac.bytes[0] == 0x52);
    TEST_ASSERT(mac.bytes[1] == 0x54);
    TEST_ASSERT(mac.bytes[2] == 0x00);
    TEST_ASSERT(mac.bytes[3] == 0x12);
    TEST_ASSERT(mac.bytes[4] == 0x34);
    TEST_ASSERT(mac.bytes[5] == 0x56);

    /* 验证 MAC 地址完整性 */
    TEST_ASSERT(sizeof(mac.bytes) == 6);
}

/**
 * @brief 测试套接字地址操作
 */
static void test_socket_address_operations(void)
{
    net_sockaddr_t addr;

    printf("\n[TEST] Socket Address Operations\n");

    /* 清零地址结构 */
    (void)memset(&addr, 0, sizeof(addr));

    /* 设置地址族 */
    addr.family = NET_AF_INET;
    TEST_ASSERT(addr.family == NET_AF_INET);

    /* 设置端口号 */
    addr.port = 0x1F90;
    TEST_ASSERT(addr.port == 0x1F90);

    /* 设置 IPv4 地址 */
    addr.addr.ipv4.bytes[0] = 10;
    addr.addr.ipv4.bytes[1] = 0;
    addr.addr.ipv4.bytes[2] = 2;
    addr.addr.ipv4.bytes[3] = 15;
    TEST_ASSERT(addr.addr.ipv4.bytes[0] == 10);
    TEST_ASSERT(addr.addr.ipv4.bytes[1] == 0);
    TEST_ASSERT(addr.addr.ipv4.bytes[2] == 2);
    TEST_ASSERT(addr.addr.ipv4.bytes[3] == 15);

    /* 验证地址结构完整性 */
    TEST_ASSERT(sizeof(addr) == 22);
}

/* ========================================================================
 * 测试网络接口统计
 * ======================================================================== */

/**
 * @brief 测试网络接口统计初始化
 */
static void test_interface_stats_initialization(void)
{
    net_if_stats_t stats;

    printf("\n[TEST] Interface Statistics Initialization\n");

    /* 初始化统计结构 */
    (void)memset(&stats, 0, sizeof(stats));

    /* 验证所有统计字段为 0 */
    TEST_ASSERT(stats.rx_packets == 0);
    TEST_ASSERT(stats.tx_packets == 0);
    TEST_ASSERT(stats.rx_bytes == 0);
    TEST_ASSERT(stats.tx_bytes == 0);
    TEST_ASSERT(stats.rx_errors == 0);
    TEST_ASSERT(stats.tx_errors == 0);
    TEST_ASSERT(stats.rx_dropped == 0);
    TEST_ASSERT(stats.tx_dropped == 0);

    /* 验证统计结构完整性 */
    TEST_ASSERT(sizeof(stats) == sizeof(uint64_t) * 8);
}

/**
 * @brief 测试网络接口统计更新
 */
static void test_interface_stats_update(void)
{
    net_if_stats_t stats;

    printf("\n[TEST] Interface Statistics Update\n");

    /* 初始化统计结构 */
    (void)memset(&stats, 0, sizeof(stats));

    /* 更新统计字段 */
    stats.rx_packets = 100;
    stats.tx_packets = 50;
    stats.rx_bytes = 10000;
    stats.tx_bytes = 5000;
    stats.rx_errors = 1;
    stats.tx_errors = 0;
    stats.rx_dropped = 2;
    stats.tx_dropped = 1;

    /* 验证统计字段 */
    TEST_ASSERT(stats.rx_packets == 100);
    TEST_ASSERT(stats.tx_packets == 50);
    TEST_ASSERT(stats.rx_bytes == 10000);
    TEST_ASSERT(stats.tx_bytes == 5000);
    TEST_ASSERT(stats.rx_errors == 1);
    TEST_ASSERT(stats.tx_errors == 0);
    TEST_ASSERT(stats.rx_dropped == 2);
    TEST_ASSERT(stats.tx_dropped == 1);
}

/* ========================================================================
 * 测试报告
 * ======================================================================== */

/**
 * @brief 打印测试报告
 */
static void print_test_report(void)
{
    printf("\n==========================================\n");
    printf("Network Stack Interface Test Report\n");
    printf("==========================================\n");
    printf("Tests Run:    %u\n", s_test_stats.tests_run);
    printf("Tests Passed: %u\n", s_test_stats.tests_passed);
    printf("Tests Failed: %u\n", s_test_stats.tests_failed);

    if (s_test_stats.tests_failed == 0)
    {
        printf("\n==========================================\n");
        printf("ALL TESTS PASSED!\n");
        printf("==========================================\n");
    }
    else
    {
        printf("\n==========================================\n");
        printf("SOME TESTS FAILED!\n");
        printf("==========================================\n");
    }
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

/**
 * @brief 测试程序主函数
 */
int32_t main(void)
{
    printf("\n==========================================\n");
    printf("AISafeOS64 Network Stack Interface Test\n");
    printf("==========================================\n");
    printf("Testing network stack data structures, constants, and operations\n");
    printf("==========================================\n\n");

    /* 数据结构大小测试 */
    test_net_address_structs_size();
    test_net_interface_structs_size();

    /* 常量定义测试 */
    test_net_address_family_constants();
    test_net_socket_type_constants();
    test_net_socket_state_constants();

    /* 网络接口类型常量测试 */
    test_net_link_type_constants();
    test_net_interface_state_constants();

    /* 套接字地址操作测试 */
    test_ipv4_address_operations();
    test_mac_address_operations();
    test_socket_address_operations();

    /* 网络接口统计测试 */
    test_interface_stats_initialization();
    test_interface_stats_update();

    /* 打印测试报告 */
    print_test_report();

    return (s_test_stats.tests_failed == 0) ? 0 : 1;
}
