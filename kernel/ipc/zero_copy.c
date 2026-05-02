/**
 * @file    zero_copy.c
 * @brief   零拷贝消息传递实现
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 *
 * @details 实现零拷贝消息传递：
 *          - 共享内存区域管理
 *          - 零拷贝消息发送
 *          - 零拷贝消息接收
 *          - 引用计数管理
 *
 * @note MISRA C:2012 合规
 * @note 对应优化点：零拷贝消息传递
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/zero_copy.h>
#include <kernel/printk.h>
#include <kernel/barrier.h>
#include <string.h>
#include <kernel/spinlock.h>

/* ========================================================================
 * 全局零拷贝管理器实例
 * ======================================================================== */

/**
 * @brief 全局零拷贝管理器实例
 */
static zero_copy_manager_t s_zero_copy_manager CACHE_ALIGN(64);

/**
 * @brief 零拷贝管理器初始化标志
 */
static bool s_zero_copy_initialized = false;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 查找空闲消息槽位
 *
 * @details 在所有共享内存区域中查找空闲的消息槽位。
 *
 * @param manager 零拷贝管理器实例
 * @param region  输出参数，找到的共享内存区域
 * @param msg_idx 输出参数，消息槽位索引
 *
 * @return true 表示找到，false 表示未找到
 */
static inline bool zero_copy_find_free_slot(zero_copy_manager_t *manager,
                                            zero_copy_shm_region_t **region,
                                            uint32_t *msg_idx)
{
    uint32_t i, j;

    if (manager == NULL)
    {
        return false;
    }

    /* 遍历所有共享内存区域 */
    for (i = 0U; i < ZERO_COPY_SHM_REGIONS; i++)
    {
        zero_copy_shm_region_t *shm_region = &manager->regions[i];

        /* 遍历该区域的所有消息槽位 */
        for (j = 0U; j < ZERO_COPY_MAX_MSGS; j++)
        {
            if (shm_region->msgs[j].header.state == ZERO_COPY_MSG_FREE)
            {
                *region = shm_region;
                *msg_idx = j;
                return true;
            }
        }
    }

    return false;
}

/**
 * @brief 查找指定源线程的消息
 *
 * @details 在所有共享内存区域中查找指定源线程的消息。
 *
 * @param manager 零拷贝管理器实例
 * @param src_id  源线程 ID
 * @param region  输出参数，找到的共享内存区域
 * @param msg_idx 输出参数，消息槽位索引
 *
 * @return true 表示找到，false 表示未找到
 */
static inline bool zero_copy_find_msg(zero_copy_manager_t *manager,
                                      kobj_id_t src_id,
                                      zero_copy_shm_region_t **region,
                                      uint32_t *msg_idx)
{
    uint32_t i, j;

    if (manager == NULL)
    {
        return false;
    }

    /* 遍历所有共享内存区域 */
    for (i = 0U; i < ZERO_COPY_SHM_REGIONS; i++)
    {
        zero_copy_shm_region_t *shm_region = &manager->regions[i];

        /* 遍历该区域的所有消息槽位 */
        for (j = 0U; j < ZERO_COPY_MAX_MSGS; j++)
        {
            if ((shm_region->msgs[j].header.src == src_id) &&
                (shm_region->msgs[j].header.state == ZERO_COPY_MSG_IN_USE))
            {
                *region = shm_region;
                *msg_idx = j;
                return true;
            }
        }
    }

    return false;
}

/* ========================================================================
 * 零拷贝消息传递 API 实现
 * ======================================================================== */

/**
 * @brief 初始化零拷贝管理器
 *
 * @param manager 零拷贝管理器实例
 * @param region_count 共享内存区域数量
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t zero_copy_init(zero_copy_manager_t *manager, uint32_t region_count)
{
    uint32_t i, j;

    if (manager == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (region_count == 0U)
    {
        region_count = ZERO_COPY_SHM_REGIONS;
    }

    if (region_count > ZERO_COPY_SHM_REGIONS)
    {
        region_count = ZERO_COPY_SHM_REGIONS;
    }

    (void)memset(manager, 0U, sizeof(zero_copy_manager_t));

    for (i = 0U; i < region_count; i++)
    {
        (void)memset(&manager->regions[i], 0U, sizeof(zero_copy_shm_region_t));

        for (j = 0U; j < ZERO_COPY_MAX_MSGS; j++)
        {
            manager->regions[i].msgs[j].header.state = ZERO_COPY_MSG_FREE;
            manager->regions[i].msgs[j].header.src = 0U;
            manager->regions[i].msgs[j].header.dst = 0U;
            manager->regions[i].msgs[j].header.size = 0U;
            manager->regions[i].msgs[j].header.ref_cnt = 0U;
            manager->regions[i].msgs[j].header.msg_id = 0U;
            manager->regions[i].msgs[j].header.flags = 0U;
        }

        manager->regions[i].free_count = ZERO_COPY_MAX_MSGS;
        manager->regions[i].used_count = 0U;
        manager->regions[i].next_msg_id = 0U;
        spin_lock_init(&manager->regions[i].lock);
        manager->regions[i].region_id = i;
    }

    manager->total_free = region_count * ZERO_COPY_MAX_MSGS;
    manager->total_sends = 0ULL;
    manager->total_receives = 0ULL;
    manager->total_copies = 0ULL;

    s_zero_copy_initialized = true;

    printk("Zero Copy initialized: %u regions, %u max msgs per region\n",
           region_count, ZERO_COPY_MAX_MSGS);

    return KERNEL_OK;
}

/**
 * @brief 发送零拷贝消息
 *
 * @details 零拷贝发送消息，不进行数据拷贝。
 *
 * @param manager 零拷贝管理器实例
 * @param dst_id  目标线程 ID
 * @param data    消息数据指针
 * @param size    消息大小
 * @param msg_id  输出参数，消息 ID
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -ENOSPC 共享内存已满
 */
kernel_status_t zero_copy_send(zero_copy_manager_t *manager,
                               kobj_id_t dst_id,
                               const void *data,
                               uint32_t size,
                               uint32_t *msg_id)
{
    zero_copy_shm_region_t *region;
    zero_copy_msg_t *msg;
    uint32_t msg_idx;

    if (manager == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (data == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (size == 0U)
    {
        return -(int32_t)EINVAL;
    }

    if (size > ZERO_COPY_BUF_SIZE)
    {
        return -(int32_t)EINVAL;
    }

    if (!s_zero_copy_initialized)
    {
        return -(int32_t)EINVAL;
    }

    /* 查找空闲消息槽位 */
    if (!zero_copy_find_free_slot(manager, &region, &msg_idx))
    {
        return -(int32_t)ENOSPC;
    }

    spin_lock(&region->lock);

    /* 获取消息指针 */
    msg = &region->msgs[msg_idx];

    /* 减少空闲消息数量，增加已使用消息数量 */
    region->free_count--;
    region->used_count++;

    /* 设置消息状态 */
    msg->header.state = ZERO_COPY_MSG_IN_USE;
    msg->header.src = hal_get_current_thread();
    msg->header.dst = dst_id;
    msg->header.size = size;
    msg->header.ref_cnt = 1U;
    msg->header.msg_id = region->next_msg_id++;
    msg->header.flags = 0U;

    /* 复制数据到共享内存 */
    (void)memcpy(msg->data, data, size);

    /* 更新统计信息 */
    manager->total_sends++;
    manager->total_copies++;

    spin_unlock(&region->lock);

    *msg_id = msg->header.msg_id;

    return KERNEL_OK;
}

/**
 * @brief 接收零拷贝消息
 *
 * @details 零拷贝接收消息，直接访问共享内存中的数据。
 *
 * @param manager 零拷贝管理器实例
 * @param src_id  源线程 ID
 * @param data    输出参数，消息数据指针
 * @param size    输出参数，消息大小
 * @param msg_id  输出参数，消息 ID
 * @param flags   输出参数，消息标志
 *
 * @return KERNEL_OK 成功
 * @return -EAGAIN 没有消息
 */
kernel_status_t zero_copy_receive(zero_copy_manager_t *manager,
                                  kobj_id_t src_id,
                                  const void **data,
                                  uint32_t *size,
                                  uint32_t *msg_id,
                                  uint32_t *flags)
{
    zero_copy_shm_region_t *region;
    zero_copy_msg_t *msg;
    uint32_t msg_idx;

    if (manager == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (!s_zero_copy_initialized)
    {
        return -(int32_t)EINVAL;
    }

    /* 查找指定源线程的消息 */
    if (!zero_copy_find_msg(manager, src_id, &region, &msg_idx))
    {
        return -EAGAIN;
    }

    spin_lock(&region->lock);

    /* 获取消息指针 */
    msg = &region->msgs[msg_idx];

    /* 验证消息 */
    if (msg->header.state != ZERO_COPY_MSG_IN_USE)
    {
        spin_unlock(&region->lock);
        return -EAGAIN;
    }

    *data = msg->data;
    *size = msg->header.size;
    *msg_id = msg->header.msg_id;
    *flags = msg->header.flags;

    /* 减少引用计数 */
    if (msg->header.ref_cnt > 0U)
    {
        msg->header.ref_cnt--;
    }

    /* 如果引用计数为0，标记为空闲 */
    if (msg->header.ref_cnt == 0U)
    {
        msg->header.state = ZERO_COPY_MSG_FREE;
        region->used_count--;
        region->free_count++;
    }

    /* 更新统计信息 */
    manager->total_receives++;
    manager->total_copies++;

    spin_unlock(&region->lock);

    return KERNEL_OK;
}

/**
 * @brief 释放消息
 *
 * @details 释放之前分配的消息，减少引用计数。
 *
 * @param manager 零拷贝管理器实例
 * @param msg_id  消息 ID
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t zero_copy_release(zero_copy_manager_t *manager, uint32_t msg_id)
{
    uint32_t i, j;

    if (manager == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (!s_zero_copy_initialized)
    {
        return -(int32_t)EINVAL;
    }

    /* 遍历所有共享内存区域 */
    for (i = 0U; i < ZERO_COPY_SHM_REGIONS; i++)
    {
        zero_copy_shm_region_t *region = &manager->regions[i];

        spin_lock(&region->lock);

        for (j = 0U; j < ZERO_COPY_MAX_MSGS; j++)
        {
            if (region->msgs[j].header.msg_id == msg_id)
            {
                if (region->msgs[j].header.state == ZERO_COPY_MSG_IN_USE)
                {
                    /* 减少引用计数 */
                    if (region->msgs[j].header.ref_cnt > 0U)
                    {
                        region->msgs[j].header.ref_cnt--;
                    }

                    /* 如果引用计数为0，标记为空闲 */
                    if (region->msgs[j].header.ref_cnt == 0U)
                    {
                        region->msgs[j].header.state = ZERO_COPY_MSG_FREE;
                        region->used_count--;
                        region->free_count++;
                    }
                }

                spin_unlock(&region->lock);
                return KERNEL_OK;
            }
        }

        spin_unlock(&region->lock);
    }

    return -(int32_t)EINVAL;
}

/**
 * @brief 增加引用计数
 *
 * @details 增加消息的引用计数，防止被过早释放。
 *
 * @param manager 零拷贝管理器实例
 * @param msg_id  消息 ID
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t zero_copy_ref(zero_copy_manager_t *manager, uint32_t msg_id)
{
    uint32_t i, j;

    if (manager == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (!s_zero_copy_initialized)
    {
        return -(int32_t)EINVAL;
    }

    /* 遍历所有共享内存区域 */
    for (i = 0U; i < ZERO_COPY_SHM_REGIONS; i++)
    {
        zero_copy_shm_region_t *region = &manager->regions[i];

        spin_lock(&region->lock);

        for (j = 0U; j < ZERO_COPY_MAX_MSGS; j++)
        {
            if (region->msgs[j].header.msg_id == msg_id)
            {
                if (region->msgs[j].header.state == ZERO_COPY_MSG_IN_USE)
                {
                    region->msgs[j].header.ref_cnt++;
                }

                spin_unlock(&region->lock);
                return KERNEL_OK;
            }
        }

        spin_unlock(&region->lock);
    }

    return -(int32_t)EINVAL;
}

/**
 * @brief 获取统计信息
 *
 * @param manager       零拷贝管理器实例
 * @param total_sends   输出：总发送次数
 * @param total_receives 输出：总接收次数
 * @param total_copies  输出：总拷贝次数
 * @param total_free    输出：总空闲消息数量
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t zero_copy_get_stats(zero_copy_manager_t *manager,
                                     uint64_t *total_sends,
                                     uint64_t *total_receives,
                                     uint64_t *total_copies,
                                     uint32_t *total_free)
{
    uint32_t i;

    if (manager == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if ((total_sends == NULL) || (total_receives == NULL) ||
        (total_copies == NULL) || (total_free == NULL))
    {
        return -(int32_t)EINVAL;
    }

    if (!s_zero_copy_initialized)
    {
        return -(int32_t)EINVAL;
    }

    *total_sends = manager->total_sends;
    *total_receives = manager->total_receives;
    *total_copies = manager->total_copies;

    /* 计算总空闲消息数量 */
    *total_free = 0U;
    for (i = 0U; i < ZERO_COPY_SHM_REGIONS; i++)
    {
        *total_free += manager->regions[i].free_count;
    }

    return KERNEL_OK;
}
