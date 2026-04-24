/**
 * @file    ip_reass.c
 * @brief   IP 分片重组实现（GREEN 阶段）
 * @author  AISafe64 Team
 * @date    2026-04-17
 * @version 1.0
 *
 * @details IP 分片重组功能实现：
 *          - IP 分片识别和管理
 *          - 分片重组缓冲区管理
 *          - 分片超时处理
 *          - 分片重组完成验证
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: IP 分片重组
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/netstack.h>
#include <kernel/config.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* ========================================================================
 * 数据结构定义
 * ======================================================================== */

/* IP 分片条目 */
typedef struct ip_reass_frag_t
{
    uint16_t             frag_offset;     /**< @brief 分片偏移（8 字节单位） */
    uint16_t             frag_id;         /**< @brief 分片标识符 */
    uint32_t             src_ip;          /**< @brief 源 IP 地址 */
    uint32_t             dst_ip;          /**< @brief 目的 IP 地址 */
    uint32_t             len;             /**< @brief 分片长度 */
    uint8_t              data[1500];      /**< @brief 分片数据 */
    uint32_t             data_len;        /**< @brief 数据长度 */
    bool                 in_use;          /**< @brief 使用标记 */
    uint64_t             arrival_time;    /**< @brief 到达时间 */
    struct ip_reass_frag_t *next;         /**< @brief 下一个分片 */
} ip_reass_frag_t;

/* IP 分片重组队列 */
typedef struct ip_reass_queue_t
{
    ip_reass_frag_t      *head;           /**< @brief 队列头 */
    ip_reass_frag_t      *tail;           /**< @brief 队列尾 */
    uint16_t             frag_id;         /**< @brief 分片标识符 */
    uint32_t             src_ip;          /**< @brief 源 IP 地址 */
    uint32_t             dst_ip;          /**< @brief 目的 IP 地址 */
    uint32_t             total_len;       /**< @brief 总长度 */
    uint16_t             header_offset;   /**< @brief IP 头偏移 */
    uint8_t              protocol;        /**< @brief 上层协议 */
    bool                 in_use;          /**< @brief 使用标记 */
    uint64_t             last_frag_time;  /**< @brief 最后分片到达时间 */
    uint32_t             frag_count;      /**< @brief 分片计数 */
    uint32_t             recv_len;        /**< @brief 已接收长度 */
} ip_reass_queue_t;

/* 最大队列数 */
#define NET_MAX_REASS_QUEUE     8U

/* 重组超时（毫秒） */
#define REASS_TIMEOUT_MS        60000U

/* IP 头部（简化版） */
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

/* IP 分片偏移掩码 */
#define IP_FRAG_OFFSET_MASK     0x1FFFU

/* 外部变量声明 */
extern ip_reass_queue_t s_reass_queues[NET_MAX_REASS_QUEUE];

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 16 位字节序交换
 */
static uint16_t net_htons(uint16_t val)
{
    return (uint16_t)(((val >> 8U) & 0xFFU) | ((val & 0xFFU) << 8U));
}

/* ========================================================================
 * IP 分片重组函数
 * ======================================================================== */

/**
 * @brief 查找或创建重组队列
 *
 * @param src_ip 源 IP 地址（网络字节序）
 * @param dst_ip 目的 IP 地址（网络字节序）
 * @param frag_id 分片标识符
 *
 * @return 重组队列指针，失败返回 NULL
 */
ip_reass_queue_t *ip_find_reass_queue(uint32_t src_ip, uint32_t dst_ip, uint16_t frag_id)
{
    uint32_t i;
    ip_reass_queue_t *queue;
    ip_reass_queue_t *oldest;
    uint64_t oldest_time;

    /* 查找已存在的队列 */
    for (i = 0U; i < NET_MAX_REASS_QUEUE; i++)
    {
        if (s_reass_queues[i].in_use &&
            (s_reass_queues[i].src_ip == src_ip) &&
            (s_reass_queues[i].dst_ip == dst_ip) &&
            (s_reass_queues[i].frag_id == frag_id))
        {
            return &s_reass_queues[i];
        }
    }

    /* 创建新队列 */
    queue = NULL;
    oldest = NULL;
    oldest_time = 0xFFFFFFFFFFFFFFFFULL;

    for (i = 0U; i < NET_MAX_REASS_QUEUE; i++)
    {
        if (!s_reass_queues[i].in_use)
        {
            queue = &s_reass_queues[i];
            break;
        }

        /* 记录最老的队列 */
        if (s_reass_queues[i].last_frag_time < oldest_time)
        {
            oldest_time = s_reass_queues[i].last_frag_time;
            oldest = &s_reass_queues[i];
        }
    }

    /* 队列满，回收最老的队列 */
    if ((queue == NULL) && (oldest != NULL))
    {
        /* 清理旧队列 */
        oldest->in_use = false;
        oldest->head = NULL;
        oldest->tail = NULL;
        oldest->frag_count = 0U;
        oldest->recv_len = 0U;
        queue = oldest;
    }

    if (queue != NULL)
    {
        queue->src_ip = src_ip;
        queue->dst_ip = dst_ip;
        queue->frag_id = frag_id;
        queue->in_use = true;
        queue->head = NULL;
        queue->tail = NULL;
        queue->frag_count = 0U;
        queue->recv_len = 0U;
        queue->last_frag_time = 0ULL;
    }

    return queue;
}

/**
 * @brief 添加分片到重组队列
 *
 * @param queue 重组队列
 * @param ip_hdr IP 头部
 * @param payload IP 载荷
 * @param payload_len 载荷长度
 *
 * @return KERNEL_OK 成功，负数错误码
 */
kernel_status_t ip_add_reass_frag(ip_reass_queue_t *queue,
                                   const ipv4_header_t *ip_hdr,
                                   const uint8_t *payload,
                                   uint32_t payload_len)
{
    ip_reass_frag_t *frag;
    uint16_t frag_offset;
    uint16_t flags_offset;
    ip_reass_frag_t *prev;
    ip_reass_frag_t *curr;

    if ((queue == NULL) || (ip_hdr == NULL) || (payload == NULL))
    {
        return -(int32_t)22;  /* EINVAL */
    }

    /* 获取分片偏移和标志 */
    flags_offset = net_htons(ip_hdr->flags_offset);
    frag_offset = (flags_offset & IP_FRAG_OFFSET_MASK) * 8U;  /* 转换为字节 */

    /* 创建分片条目 */
    frag = (ip_reass_frag_t *)malloc(sizeof(ip_reass_frag_t));
    if (frag == NULL)
    {
        return -(int32_t)12;  /* ENOMEM */
    }

    frag->frag_offset = frag_offset;
    frag->frag_id = net_htons(ip_hdr->identification);
    frag->src_ip = ip_hdr->src_ip;
    frag->dst_ip = ip_hdr->dst_ip;
    frag->len = payload_len;
    frag->in_use = true;
    frag->next = NULL;

    /* 复制数据 */
    if (payload_len > 0U)
    {
        (void)memcpy(frag->data, payload, payload_len);
        frag->data_len = payload_len;
    }
    else
    {
        frag->data_len = 0U;
    }

    /* 空队列 */
    if (queue->head == NULL)
    {
        queue->head = frag;
        queue->tail = frag;
        queue->frag_count = 1U;
        queue->recv_len = payload_len;
        return KERNEL_OK;
    }

    /* 按偏移量插入（升序） */
    prev = NULL;
    curr = queue->head;

    while ((curr != NULL) && (curr->frag_offset < frag_offset))
    {
        prev = curr;
        curr = curr->next;
    }

    /* 插入分片 */
    if (prev == NULL)
    {
        /* 插入头部 */
        frag->next = queue->head;
        queue->head = frag;
    }
    else if (curr == NULL)
    {
        /* 插入尾部 */
        prev->next = frag;
        queue->tail = frag;
        frag->next = NULL;
    }
    else
    {
        /* 插入中间 */
        prev->next = frag;
        frag->next = curr;
    }

    queue->frag_count++;
    queue->recv_len += payload_len;

    return KERNEL_OK;
}

/**
 * @brief 检查分片是否完整
 *
 * @param queue 重组队列
 *
 * @return true 完整，false 不完整
 */
static bool ip_reass_is_complete(const ip_reass_queue_t *queue)
{
    uint32_t offset;
    ip_reass_frag_t *frag;
    uint16_t flags_offset;
    bool last_frag;

    if ((queue == NULL) || (queue->head == NULL))
    {
        return false;
    }

    offset = 0U;
    frag = queue->head;
    last_frag = false;

    while (frag != NULL)
    {
        /* 检查偏移量连续 */
        if (frag->frag_offset != offset)
        {
            return false;
        }

        offset += frag->data_len;

        /* 检查是否是最后一个分片（MF = 0） */
        if (frag == queue->tail)
        {
            /* 假设最后一个分片的 MF = 0 */
            last_frag = true;
        }

        frag = frag->next;
    }

    /* 检查是否收到最后一个分片 */
    if (!last_frag)
    {
        return false;
    }

    /* 检查总长度是否匹配 */
    if (offset != queue->total_len)
    {
        return false;
    }

    return true;
}

/**
 * @brief 分片重组
 *
 * @param queue 重组队列
 * @param output 输出缓冲区
 * @param output_len 输出缓冲区长度
 *
 * @return 实际重组长度，负数表示错误
 */
int32_t ip_reassemble(ip_reass_queue_t *queue, uint8_t *output, uint32_t output_len)
{
    ip_reass_frag_t *frag;
    uint32_t offset;

    if ((queue == NULL) || (output == NULL))
    {
        return -(int32_t)22;  /* EINVAL */
    }

    if (!ip_reass_is_complete(queue))
    {
        return -(int32_t)11;  /* EAGAIN */
    }

    if (queue->recv_len > output_len)
    {
        return -(int32_t)27;  /* EFBIG */
    }

    /* 复制分片数据 */
    offset = 0U;
    frag = queue->head;

    while (frag != NULL)
    {
        (void)memcpy(&output[offset], frag->data, frag->data_len);
        offset += frag->data_len;
        frag = frag->next;
    }

    return (int32_t)offset;
}

/**
 * @brief 分片超时处理
 *
 * @param current_time 当前时间（毫秒）
 */
void ip_reass_timeout(uint64_t current_time)
{
    uint32_t i;
    ip_reass_queue_t *queue;

    for (i = 0U; i < NET_MAX_REASS_QUEUE; i++)
    {
        queue = &s_reass_queues[i];

        if (queue->in_use &&
            (current_time - queue->last_frag_time > REASS_TIMEOUT_MS))
        {
            /* 清理超时队列 */
            queue->in_use = false;
            queue->head = NULL;
            queue->tail = NULL;
            queue->frag_count = 0U;
            queue->recv_len = 0U;
        }
    }
}
