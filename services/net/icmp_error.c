/**
 * @file    icmp_error.c
 * @brief   ICMP 错误消息实现（GREEN 阶段）
 * @author  AISafe64 Team
 * @date    2026-04-17
 * @version 1.0
 *
 * @details ICMP 错误消息功能实现：
 *          - Destination Unreachable（类型 3）
 *          - Time Exceeded（类型 11）
 *          - Parameter Problem（类型 12）
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: ICMP 错误消息
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/netstack.h>
#include <kernel/config.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * ICMP 错误消息类型定义
 * ======================================================================== */

/** @brief ICMP 类型：目的不可达 */
#define ICMP_TYPE_DEST_UNREACH   3U

/** @brief ICMP 类型：超时 */
#define ICMP_TYPE_TIME_EXCEEDED  11U

/** @brief ICMP 类型：参数问题 */
#define ICMP_TYPE_PARAM_PROB     12U

/** @brief ICMP 代码：网络不可达 */
#define ICMP_CODE_NET_UNREACH    0U

/** @brief ICMP 代码：主机不可达 */
#define ICMP_CODE_HOST_UNREACH   1U

/** @brief ICMP 代码：端口不可达 */
#define ICMP_CODE_PORT_UNREACH   3U

/** @brief ICMP 代码：需要分片 */
#define ICMP_CODE_FRAG_NEEDED    4U

/** @brief ICMP 代码：TTL 过期 */
#define ICMP_CODE_TTL_EXPIRED    0U

/** @brief ICMP 代码：重组超时 */
#define ICMP_CODE_REASS_TIME_EXPIRED 1U

/** @brief ICMP 代码：坏的 IP 头 */
#define ICMP_CODE_BAD_HEADER     0U

/* ========================================================================
 * IP 头部（简化版）
 * ======================================================================== */

typedef struct
{
    uint8_t  version_ihl;   /**< @brief 版本 + 头长 */
    uint8_t  tos;           /**< @brief 服务类型 */
    uint16_t total_length;  /**< @brief 总长度 */
    uint16_t identification;/**< @brief 标识 */
    uint16_t flags_offset;  /**< @brief 标志 + 片偏移 */
    uint8_t  ttl;           /**< @brief 生存时间 */
    uint8_t  protocol;      /**< @brief 上层协议 */
    uint16_t checksum;      /**< @brief 校验和 */
    uint32_t src_ip;        /**< @brief 源 IP */
    uint32_t dst_ip;        /**< @brief 目的 IP */
} ipv4_header_t;

/* ========================================================================
 * ICMP 错误消息数据结构
 * ======================================================================== */

typedef struct
{
    uint8_t  type;          /**< @brief 类型 */
    uint8_t  code;          /**< @brief 代码 */
    uint16_t checksum;      /**< @brief 校验和 */
    uint8_t  unused[4];     /**< @brief 未使用 */
    uint8_t  orig_ip[20];   /**< @brief 原始 IP 头（20 字节） */
    uint8_t  orig_data[8];  /**< @brief 原始数据（8 字节） */
} icmp_error_message_t;

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 计算 Internet 校验和
 */
static uint16_t net_checksum(const void *data, uint32_t len)
{
    const uint8_t *buf = (const uint8_t *)data;
    uint32_t sum = 0U;
    uint32_t i;

    for (i = 0U; i < (len - 1U); i += 2U)
    {
        sum += ((uint32_t)buf[i] << 8U) | (uint32_t)buf[i + 1U];
    }

    if ((len & 1U) != 0U)
    {
        sum += (uint32_t)buf[len - 1U] << 8U;
    }

    while ((sum >> 16U) != 0U)
    {
        sum = (sum & 0xFFFFU) + (sum >> 16U);
    }

    return (uint16_t)(~sum);
}

/**
 * @brief 16 位字节序交换
 */
static uint16_t net_htons(uint16_t val)
{
    return (uint16_t)(((val >> 8U) & 0xFFU) | ((val & 0xFFU) << 8U));
}

/**
 * @brief 32 位字节序交换
 */
static uint32_t net_htonl(uint32_t val)
{
    return ((val >> 24U) & 0x000000FFU) |
           ((val >> 8U)  & 0x0000FF00U) |
           ((val << 8U)  & 0x00FF0000U) |
           ((val << 24U) & 0xFF000000U);
}

/* ========================================================================
 * ICMP 错误消息函数
 * ======================================================================== */

/**
 * @brief 构造 ICMP 错误消息
 *
 * @param type ICMP 类型
 * @param code ICMP 代码
 * @param orig_data 原始数据
 * @param orig_len 原始数据长度
 * @param output 输出缓冲区
 * @param output_len 输出缓冲区长度
 *
 * @return 实际构造的长度，负数表示错误
 */
static int64_t icmp_build_error_message(uint8_t type, uint8_t code,
                                         const uint8_t *orig_data, uint32_t orig_len,
                                         uint8_t *output, uint32_t output_len)
{
    icmp_error_message_t *msg;
    uint32_t copy_len;

    if ((orig_data == NULL) || (output == NULL))
    {
        return -(int64_t)22;  /* EINVAL */
    }

    if (output_len < sizeof(icmp_error_message_t))
    {
        return -(int64_t)22;  /* EINVAL */
    }

    (void)memset(output, 0, sizeof(icmp_error_message_t));
    msg = (icmp_error_message_t *)output;

    msg->type = type;
    msg->code = code;
    msg->checksum = 0U;

    /* 复制原始 IP 头（20 字节） */
    copy_len = orig_len;
    if (copy_len > 20U)
    {
        copy_len = 20U;
    }
    (void)memcpy(msg->orig_ip, orig_data, copy_len);

    /* 复制原始数据（8 字节） */
    if (orig_len > 20U)
    {
        copy_len = orig_len - 20U;
        if (copy_len > 8U)
        {
            copy_len = 8U;
        }
        (void)memcpy(msg->orig_data, &orig_data[20], copy_len);
    }

    /* 计算校验和 */
    msg->checksum = net_checksum(msg, sizeof(icmp_error_message_t));

    return (int64_t)sizeof(icmp_error_message_t);
}

/**
 * @brief 发送目的不可达消息
 *
 * @param if_id 接口 ID
 * @param src_ip 源 IP 地址（主机字节序）
 * @param dst_ip 目的 IP 地址（主机字节序）
 * @param ip_hdr 原始 IP 头部
 * @param code ICMP 代码
 */
void icmp_send_dest_unreachable(uint32_t if_id, uint32_t src_ip, uint32_t dst_ip,
                                const ipv4_header_t *ip_hdr, uint8_t code)
{
    uint8_t icmp_msg[sizeof(icmp_error_message_t)];
    int64_t len;

    if (ip_hdr == NULL)
    {
        return;
    }

    /* 构造 ICMP 错误消息 */
    len = icmp_build_error_message(ICMP_TYPE_DEST_UNREACH, code,
                                    (const uint8_t *)ip_hdr, sizeof(ipv4_header_t),
                                    icmp_msg, sizeof(icmp_msg));

    if (len < 0)
    {
        return;
    }

    /* 发送 ICMP 消息 */
    /* 注意：这里需要调用 ipv4_send()，但为了避免循环依赖，简化处理 */
    (void)if_id;
    (void)src_ip;
    (void)dst_ip;
    (void)len;

    /* TODO: 实际发送 ICMP 消息 */
}

/**
 * @brief 发送超时消息
 *
 * @param if_id 接口 ID
 * @param src_ip 源 IP 地址（主机字节序）
 * @param dst_ip 目的 IP 地址（主机字节序）
 * @param ip_hdr 原始 IP 头部
 * @param code ICMP 代码
 */
void icmp_send_time_exceeded(uint32_t if_id, uint32_t src_ip, uint32_t dst_ip,
                              const ipv4_header_t *ip_hdr, uint8_t code)
{
    uint8_t icmp_msg[sizeof(icmp_error_message_t)];
    int64_t len;

    if (ip_hdr == NULL)
    {
        return;
    }

    /* 构造 ICMP 错误消息 */
    len = icmp_build_error_message(ICMP_TYPE_TIME_EXCEEDED, code,
                                    (const uint8_t *)ip_hdr, sizeof(ipv4_header_t),
                                    icmp_msg, sizeof(icmp_msg));

    if (len < 0)
    {
        return;
    }

    /* 发送 ICMP 消息 */
    /* 注意：这里需要调用 ipv4_send()，但为了避免循环依赖，简化处理 */
    (void)if_id;
    (void)src_ip;
    (void)dst_ip;
    (void)len;

    /* TODO: 实际发送 ICMP 消息 */
}

/**
 * @brief 发送参数问题消息
 *
 * @param if_id 接口 ID
 * @param src_ip 源 IP 地址（主机字节序）
 * @param dst_ip 目的 IP 地址（主机字节序）
 * @param ip_hdr 原始 IP 头部
 * @param code ICMP 代码
 * @param ptr 指向错误的字节偏移
 */
void icmp_send_param_problem(uint32_t if_id, uint32_t src_ip, uint32_t dst_ip,
                              const ipv4_header_t *ip_hdr, uint8_t code, uint8_t ptr)
{
    uint8_t icmp_msg[sizeof(icmp_error_message_t)];
    int64_t len;
    icmp_error_message_t *msg;

    if (ip_hdr == NULL)
    {
        return;
    }

    /* 构造 ICMP 错误消息 */
    len = icmp_build_error_message(ICMP_TYPE_PARAM_PROB, code,
                                    (const uint8_t *)ip_hdr, sizeof(ipv4_header_t),
                                    icmp_msg, sizeof(icmp_msg));

    if (len < 0)
    {
        return;
    }

    /* 设置指针 */
    msg = (icmp_error_message_t *)icmp_msg;
    msg->unused[0] = ptr;
    msg->unused[1] = 0U;
    msg->unused[2] = 0U;
    msg->unused[3] = 0U;

    /* 重新计算校验和 */
    msg->checksum = 0U;
    msg->checksum = net_checksum(msg, sizeof(icmp_error_message_t));

    /* 发送 ICMP 消息 */
    /* 注意：这里需要调用 ipv4_send()，但为了避免循环依赖，简化处理 */
    (void)if_id;
    (void)src_ip;
    (void)dst_ip;
    (void)len;

    /* TODO: 实际发送 ICMP 消息 */
}

/**
 * @brief 处理 IP 分片超时
 *
 * @param if_id 接口 ID
 * @param src_ip 源 IP 地址（主机字节序）
 * @param dst_ip 目的 IP 地址（主机字节序）
 * @param frag_id 分片标识符
 */
void icmp_send_reass_timeout(uint32_t if_id, uint32_t src_ip, uint32_t dst_ip,
                             uint16_t frag_id)
{
    /* 构造虚拟 IP 头部用于 ICMP 错误消息 */
    ipv4_header_t ip_hdr;

    (void)memset(&ip_hdr, 0, sizeof(ipv4_header_t));

    ip_hdr.version_ihl = (4U << 4U) | 5U;
    ip_hdr.src_ip = net_htonl(src_ip);
    ip_hdr.dst_ip = net_htonl(dst_ip);
    ip_hdr.identification = net_htons(frag_id);

    /* 发送 ICMP 超时消息 */
    icmp_send_time_exceeded(if_id, src_ip, dst_ip, &ip_hdr, ICMP_CODE_REASS_TIME_EXPIRED);
}
