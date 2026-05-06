/**
 * @file    test_net_config.c
 * @brief   网络配置单元测试
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details 网络配置单元测试：
 *          - IP 地址配置
 *          - 子网掩码配置
 *          - 网关配置
 *          - MAC 地址配置
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

#define NET_CONFIG_INVALID_IP   0U
#define NET_CONFIG_TEST_IP      0x0A000001U  /* 10.0.0.1 */
#define NET_CONFIG_TEST_NETMASK  0xFFFFFF00U  /* 255.255.255.0 */
#define NET_CONFIG_TEST_GATEWAY  0x0A00000FU  /* 10.0.0.15 */

/* ========================================================================
 * Mock API 实现
 * ======================================================================== */

extern int32_t net_config_init(void);
extern int32_t net_config_set_ip(uint32_t ip_addr);
extern uint32_t net_config_get_ip(void);
extern int32_t net_config_set_netmask(uint32_t netmask);
extern uint32_t net_config_get_netmask(void);
extern int32_t net_config_set_gateway(uint32_t gateway);
extern uint32_t net_config_get_gateway(void);
extern int32_t net_config_set_mac(const uint8_t *mac_addr);
extern int32_t net_config_get_mac(uint8_t *mac_addr);
extern int32_t net_config_up(void);
extern int32_t net_config_down(void);
extern bool   net_config_is_up(void);

/* ========================================================================
 * 测试用例
 * ======================================================================== */

void test_config_init(void)
{
    int32_t ret = net_config_init();
    TEST_ASSERT_EQ(ret, 0);
}

void test_config_set_ip(void)
{
    int32_t ret;

    ret = net_config_set_ip(NET_CONFIG_TEST_IP);
    TEST_ASSERT_EQ(ret, 0);
}

void test_config_get_ip(void)
{
    uint32_t ip;

    (void)net_config_set_ip(NET_CONFIG_TEST_IP);

    ip = net_config_get_ip();
    TEST_ASSERT_EQ(ip, NET_CONFIG_TEST_IP);
}

void test_config_set_invalid_ip(void)
{
    int32_t ret;

    ret = net_config_set_ip(NET_CONFIG_INVALID_IP);
    TEST_ASSERT_TRUE(ret < 0);
}

void test_config_set_netmask(void)
{
    int32_t ret;

    ret = net_config_set_netmask(NET_CONFIG_TEST_NETMASK);
    TEST_ASSERT_EQ(ret, 0);
}

void test_config_get_netmask(void)
{
    uint32_t netmask;

    (void)net_config_set_netmask(NET_CONFIG_TEST_NETMASK);

    netmask = net_config_get_netmask();
    TEST_ASSERT_EQ(netmask, NET_CONFIG_TEST_NETMASK);
}

void test_config_set_gateway(void)
{
    int32_t ret;

    ret = net_config_set_gateway(NET_CONFIG_TEST_GATEWAY);
    TEST_ASSERT_EQ(ret, 0);
}

void test_config_get_gateway(void)
{
    uint32_t gateway;

    (void)net_config_set_gateway(NET_CONFIG_TEST_GATEWAY);

    gateway = net_config_get_gateway();
    TEST_ASSERT_EQ(gateway, NET_CONFIG_TEST_GATEWAY);
}

void test_config_set_mac(void)
{
    int32_t ret;
    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

    ret = net_config_set_mac(mac);
    TEST_ASSERT_EQ(ret, 0);
}

void test_config_get_mac(void)
{
    int32_t ret;
    uint8_t mac[6] = {0};
    uint8_t expected_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

    (void)net_config_set_mac(expected_mac);

    ret = net_config_get_mac(mac);
    TEST_ASSERT_EQ(ret, 0);

    TEST_ASSERT_EQ(memcmp(mac, expected_mac, 6U), 0);
}

void test_config_up(void)
{
    int32_t ret;

    ret = net_config_up();
    TEST_ASSERT_EQ(ret, 0);
}

void test_config_is_up(void)
{
    bool up;

    (void)net_config_up();

    up = net_config_is_up();
    TEST_ASSERT_TRUE(up);
}

void test_config_down(void)
{
    int32_t ret;

    ret = net_config_down();
    TEST_ASSERT_EQ(ret, 0);
}

void test_config_is_down(void)
{
    bool up;

    (void)net_config_down();

    up = net_config_is_up();
    TEST_ASSERT_FALSE(up);
}

void test_config_up_down(void)
{
    bool up;

    (void)net_config_up();
    up = net_config_is_up();
    TEST_ASSERT_TRUE(up);

    (void)net_config_down();
    up = net_config_is_up();
    TEST_ASSERT_FALSE(up);

    (void)net_config_up();
    up = net_config_is_up();
    TEST_ASSERT_TRUE(up);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("\n");
    printf("============================================\n");
    printf("  网络配置单元测试\n");
    printf("============================================\n");
    printf("\n");

    printf("=== 配置初始化 ===\n");
    TEST_RUN(config_init);

    printf("\n=== IP 地址配置 ===\n");
    TEST_RUN(config_set_ip);
    TEST_RUN(config_get_ip);
    TEST_RUN(config_set_invalid_ip);

    printf("\n=== 子网掩码配置 ===\n");
    TEST_RUN(config_set_netmask);
    TEST_RUN(config_get_netmask);

    printf("\n=== 网关配置 ===\n");
    TEST_RUN(config_set_gateway);
    TEST_RUN(config_get_gateway);

    printf("\n=== MAC 地址配置 ===\n");
    TEST_RUN(config_set_mac);
    TEST_RUN(config_get_mac);

    printf("\n=== 接口状态 ===\n");
    TEST_RUN(config_up);
    TEST_RUN(config_is_up);
    TEST_RUN(config_down);
    TEST_RUN(config_is_down);
    TEST_RUN(config_up_down);

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
