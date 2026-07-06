/**
 * @file    driver_core.c
 * @brief   驱动核心实现
 * @author  AISafe64 Team
 * @date    2026-04-08
 * @version 1.0
 *
 * @details 本文件实现了 AISafeOS64 驱动框架的核心功能：
 *          - 驱动/设备的注册与注销
 *          - 驱动与设备的匹配（compatible 字符串或 PCI ID）
 *          - 设备 probe 流程
 *          - 设备文件操作（open/close/read/write/ioctl）
 *          - 驱动子系统统计
 *
 * @note MISRA-C:2012 合规
 * @note kernel/ 非 arch/ 目录禁止使用 __asm__/msr/mrs/isb
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========== 头文件包含 ========== */

#include <kernel/driver.h>
#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <kernel/errno.h>
#include <hal.h>
#include <stdint.h>
#include <stddef.h>

/* ========== 外部函数声明（kernel_string.c 提供） ========== */

extern void *kernel_memcpy(void *dest, const void *src, size_t n);
extern void *kernel_memset(void *s, int32_t c, size_t n);
extern void  kernel_memzero(void *s, size_t n);
extern size_t kernel_strlen(const char *s);
extern char *kernel_strcpy(char *dest, const char *src);
extern char *kernel_strncpy(char *dest, const char *src, size_t n);
extern int32_t kernel_strncmp(const char *s1, const char *s2, size_t n);

/* ========== 静态断言 ========== */

/* 验证最大驱动数配置 */
static_assert((CONFIG_MAX_DRIVERS >= 1U) && (CONFIG_MAX_DRIVERS <= 256U),
              "CONFIG_MAX_DRIVERS must be in range [1, 256]");

/* 验证最大设备数配置 */
static_assert((CONFIG_MAX_DEVICES >= 1U) && (CONFIG_MAX_DEVICES <= 256U),
              "CONFIG_MAX_DEVICES must be in range [1, 256]");

/* ========== 内部常量 ========== */

/** @brief 设备名称最大长度（不含终止符） */
#define DRIVER_NAME_MAX_LEN     31U

/** @brief 无效设备 ID */
#define DEVICE_ID_INVALID       0xFFFFFFFFU

/** @brief 无效驱动 ID */
#define DRIVER_ID_INVALID       0xFFFFFFFFU

/* ========== 静态存储池 ========== */

/** @brief 驱动描述符静态池 */
static driver_desc_t s_drivers[CONFIG_MAX_DRIVERS];

/** @brief 设备描述符静态池 */
static device_desc_t s_devices[CONFIG_MAX_DEVICES];

/* ========== 全局链表 ========== */

/** @brief 全局驱动链表头 */
static struct list_head s_driver_list;

/** @brief 全局设备链表头 */
static struct list_head s_device_list;

/* ========== 全局锁 ========== */

/** @brief 驱动子系统全局自旋锁 */
static TicketLock_t s_driver_lock;

/* ========== 计数器 ========== */

/** @brief 下一个驱动 ID（单调递增） */
static uint32_t s_next_drv_id;

/** @brief 下一个设备 ID（单调递增） */
static uint32_t s_next_dev_id;

/* ========== 统计信息 ========== */

/** @brief 驱动子系统统计 */
static driver_stats_t s_stats;

/* ========== 外部声明（driver_module.c 提供） ========== */

/** @brief 模块加载器初始化 */

/* ========== 内部辅助函数声明 ========== */

static driver_desc_t *alloc_driver_slot(void);
static device_desc_t *alloc_device_slot(void);
static int32_t match_driver_device(const driver_desc_t *drv,
                                    const device_desc_t *dev);
static driver_desc_t *find_driver_for_device(const device_desc_t *dev);
driver_desc_t *find_driver_by_id_internal(uint32_t drv_id);

/* ========================================================================
 * 驱动子系统 API 实现
 * ======================================================================== */

/**
 * @brief 初始化驱动子系统
 *
 * @details 清零静态池，初始化链表头和自旋锁
 */
kernel_status_t driver_subsys_init(void)
{
    uint32_t i;

    /* 清零驱动池 */
    kernel_memzero(s_drivers, sizeof(s_drivers));
    for (i = 0U; i < CONFIG_MAX_DRIVERS; i++)
    {
        s_drivers[i].in_use = 0U;
    }

    /* 清零设备池 */
    kernel_memzero(s_devices, sizeof(s_devices));

    /* 初始化链表头 */
    INIT_LIST_HEAD(&s_driver_list);
    INIT_LIST_HEAD(&s_device_list);

    /* 初始化自旋锁 */
    ticket_lock_init(&s_driver_lock);

    /* 重置计数器 */
    s_next_drv_id = 1U;
    s_next_dev_id = 1U;

    /* 清零统计 */
    kernel_memzero(&s_stats, sizeof(s_stats));

    /* 初始化模块加载器 */

    return KERNEL_OK;
}

/**
 * @brief 注册驱动到子系统
 *
 * @details 从静态池分配一个空闲槽位，填充驱动信息并加入全局链表
 */
kernel_status_t driver_register_kern(const char *name, uint32_t type,
                                      const driver_match_t *match,
                                      const driver_ops_t *ops)
{
    driver_desc_t *drv;
    size_t name_len;

    /* 参数有效性检查 */
    if ((name == NULL) || (match == NULL) || (ops == NULL))
    {
        return -22; /* EINVAL */
    }

    if (type >= DRIVER_TYPE_COUNT)
    {
        return -22; /* EINVAL */
    }

    /* 检查同名驱动是否已存在 */
    if (driver_find_by_name(name) != NULL)
    {
        return -17; /* EEXIST */
    }

    name_len = kernel_strlen(name);
    if (name_len == 0U)
    {
        return -22; /* EINVAL */
    }

    /* 分配槽位 */
    drv = alloc_driver_slot();
    if (drv == NULL)
    {
        return -12; /* ENOMEM */
    }

    /* 填充驱动信息 */
    drv->drv_id = s_next_drv_id++;
    kernel_strncpy(drv->name, name, DRIVER_NAME_MAX_LEN);
    drv->name[DRIVER_NAME_MAX_LEN] = '\0';
    drv->type = type;
    kernel_memcpy(&drv->match, match, sizeof(driver_match_t));
    drv->ops = ops;
    drv->state = DRIVER_STATE_INITIALIZED;
    drv->ref_count = 0U;
    drv->device_count = 0U;
    INIT_LIST_HEAD(&drv->devices);
    drv->in_use = 1U;

    /* 加入全局驱动链表 */
    ticket_lock_acquire(&s_driver_lock);
    list_add_tail(&drv->node, &s_driver_list);
    s_stats.total_drivers++;
    ticket_lock_release(&s_driver_lock);

    return KERNEL_OK;
}

/**
 * @brief 注销驱动
 *
 * @details 将驱动从全局链表移除，释放槽位。
 *          如果驱动仍绑定设备，拒绝注销。
 */
kernel_status_t driver_unregister_kern(const char *name)
{
    driver_desc_t *drv;
    size_t name_len;

    if (name == NULL)
    {
        return -22; /* EINVAL */
    }

    name_len = kernel_strlen(name);
    if (name_len == 0U)
    {
        return -22; /* EINVAL */
    }

    drv = driver_find_by_name(name);
    if (drv == NULL)
    {
        return -2; /* ENOENT */
    }

    /* 检查是否仍有绑定设备 */
    if (drv->device_count > 0U)
    {
        return -16; /* EBUSY */
    }

    ticket_lock_acquire(&s_driver_lock);

    /* 从全局链表移除 */
    list_del(&drv->node);

    /* 释放槽位 */
    drv->in_use = 0U;
    drv->state = DRIVER_STATE_REMOVED;

    s_stats.total_drivers--;

    ticket_lock_release(&s_driver_lock);

    return KERNEL_OK;
}

/**
 * @brief 注册设备
 *
 * @details 从静态池分配设备槽位，填充设备信息并加入全局链表
 */
kernel_status_t device_register(const char *name, uint32_t type,
                                 paddr_t mmio_base, uint64_t mmio_size,
                                 uint32_t irq, void *priv)
{
    device_desc_t *dev;
    size_t name_len;

    /* 参数有效性检查 */
    if (name == NULL)
    {
        return -22; /* EINVAL */
    }

    if (type >= DRIVER_TYPE_COUNT)
    {
        return -22; /* EINVAL */
    }

    name_len = kernel_strlen(name);
    if (name_len == 0U)
    {
        return -22; /* EINVAL */
    }

    /* 分配槽位 */
    dev = alloc_device_slot();
    if (dev == NULL)
    {
        return -12; /* ENOMEM */
    }

    /* 填充设备信息 */
    dev->dev_id = s_next_dev_id++;
    kernel_strncpy(dev->name, name, DRIVER_NAME_MAX_LEN);
    dev->name[DRIVER_NAME_MAX_LEN] = '\0';
    dev->type = type;
    dev->mmio_base = mmio_base;
    dev->mmio_size = mmio_size;
    dev->irq = irq;
    dev->priv = priv;
    dev->state = DRIVER_STATE_UNINITIALIZED;
    dev->drv_id = 0U;

    /* 加入全局设备链表 */
    ticket_lock_acquire(&s_driver_lock);
    list_add_tail(&dev->node, &s_device_list);
    INIT_LIST_HEAD(&dev->drv_node);
    s_stats.total_devices++;
    ticket_lock_release(&s_driver_lock);

    return KERNEL_OK;
}

/**
 * @brief 注销设备
 *
 * @details 将设备从全局链表和驱动链表移除，释放槽位
 */
kernel_status_t device_unregister(uint32_t dev_id)
{
    device_desc_t *dev;

    if (dev_id == DEVICE_ID_INVALID)
    {
        return -22; /* EINVAL */
    }

    dev = device_find_by_id(dev_id);
    if (dev == NULL)
    {
        return -2; /* ENOENT */
    }

    /* 如果设备正在运行，需要先调用 remove */
    if (dev->state == DRIVER_STATE_RUNNING)
    {
        driver_desc_t *drv;

        ticket_lock_acquire(&s_driver_lock);
        drv = find_driver_by_id_internal(dev->drv_id);
        if ((drv != NULL) && (drv->ops != NULL) && (drv->ops->remove != NULL))
        {
            (void)drv->ops->remove(dev->priv);
        }
        if (drv != NULL)
        {
            /* 递减驱动设备计数 */
            if (drv->device_count > 0U)
            {
                drv->device_count--;
            }
            /* 递减引用计数 */
            if (drv->ref_count > 0U)
            {
                drv->ref_count--;
            }
        }
        ticket_lock_release(&s_driver_lock);
    }

    ticket_lock_acquire(&s_driver_lock);

    /* 从驱动设备链表移除 */
    if (dev->drv_id != 0U)
    {
        list_del(&dev->drv_node);
    }

    /* 从全局设备链表移除 */
    list_del(&dev->node);

    /* 标记状态 */
    dev->state = DRIVER_STATE_REMOVED;
    dev->dev_id = DEVICE_ID_INVALID;
    dev->drv_id = 0U;

    s_stats.total_devices--;

    ticket_lock_release(&s_driver_lock);

    return KERNEL_OK;
}

/**
 * @brief 对所有未绑定设备执行 probe
 *
 * @details 遍历所有设备，为未绑定设备查找匹配驱动并调用 probe
 */
kernel_status_t device_probe_all(void)
{
    struct list_head *pos;
    struct list_head *n;

    ticket_lock_acquire(&s_driver_lock);

    list_for_each_safe(pos, n, &s_device_list)
    {
        device_desc_t *dev;
        driver_desc_t *drv;

        dev = container_of(pos, device_desc_t, node);

        /* 跳过已绑定的设备 */
        if (dev->drv_id != 0U)
        {
            continue;
        }

        /* 跳过非 UNINITIALIZED 状态 */
        if (dev->state != DRIVER_STATE_UNINITIALIZED)
        {
            continue;
        }

        /* 查找匹配驱动 */
        drv = find_driver_for_device(dev);
        if (drv == NULL)
        {
            /* 无匹配驱动，跳过 */
            continue;
        }

        /* 调用 probe */
        if ((drv->ops != NULL) && (drv->ops->probe != NULL))
        {
            kernel_status_t ret;

            ret = drv->ops->probe((void *)dev);
            if (ret == KERNEL_OK)
            {
                /* probe 成功：绑定设备到驱动 */
                dev->drv_id = drv->drv_id;
                dev->state = DRIVER_STATE_RUNNING;
                list_add_tail(&dev->drv_node, &drv->devices);
                drv->device_count++;
                drv->ref_count++;
                s_stats.probe_count++;
            }
            else
            {
                /* probe 失败 */
                dev->state = DRIVER_STATE_FAILED;
                s_stats.probe_fail_count++;
            }
        }
    }

    ticket_lock_release(&s_driver_lock);

    return KERNEL_OK;
}

/**
 * @brief 通过 compatible 字符串查找驱动
 */
driver_desc_t *driver_find_by_compatible(const char *compatible)
{
    struct list_head *pos;

    if (compatible == NULL)
    {
        return NULL;
    }

    list_for_each(pos, &s_driver_list)
    {
        driver_desc_t *drv;

        drv = container_of(pos, driver_desc_t, node);
        if (drv->match.compatible[0] != '\0')
        {
            if (kernel_strncmp(drv->match.compatible, compatible,
                               sizeof(drv->match.compatible) - 1U) == 0)
            {
                return drv;
            }
        }
    }

    return NULL;
}

/**
 * @brief 通过名称查找驱动
 */
driver_desc_t *driver_find_by_name(const char *name)
{
    struct list_head *pos;

    if (name == NULL)
    {
        return NULL;
    }

    list_for_each(pos, &s_driver_list)
    {
        driver_desc_t *drv;

        drv = container_of(pos, driver_desc_t, node);
        if (kernel_strncmp(drv->name, name, sizeof(drv->name) - 1U) == 0)
        {
            return drv;
        }
    }

    return NULL;
}

/**
 * @brief 通过设备 ID 查找设备
 */
device_desc_t *device_find_by_id(uint32_t dev_id)
{
    struct list_head *pos;

    if (dev_id == DEVICE_ID_INVALID)
    {
        return NULL;
    }

    list_for_each(pos, &s_device_list)
    {
        device_desc_t *dev;

        dev = container_of(pos, device_desc_t, node);
        if (dev->dev_id == dev_id)
        {
            return dev;
        }
    }

    return NULL;
}

/**
 * @brief 打开设备
 *
 * @details 递增引用计数，首次打开时将设备状态设为 RUNNING
 */
kernel_status_t device_open(uint32_t dev_id)
{
    device_desc_t *dev;
    driver_desc_t *drv;

    dev = device_find_by_id(dev_id);
    if (dev == NULL)
    {
        return -2; /* ENOENT */
    }

    ticket_lock_acquire(&s_driver_lock);

    if (dev->drv_id == 0U)
    {
        ticket_lock_release(&s_driver_lock);
        return -6; /* ENXIO: 无驱动绑定 */
    }

    drv = find_driver_by_id_internal(dev->drv_id);
    if (drv == NULL)
    {
        ticket_lock_release(&s_driver_lock);
        return -6; /* ENXIO */
    }

    drv->ref_count++;
    dev->state = DRIVER_STATE_RUNNING;

    ticket_lock_release(&s_driver_lock);

    return KERNEL_OK;
}

/**
 * @brief 关闭设备
 *
 * @details 递减引用计数，最后一次关闭时将设备状态设为 INITIALIZED
 */
kernel_status_t device_close(uint32_t dev_id)
{
    device_desc_t *dev;
    driver_desc_t *drv;

    dev = device_find_by_id(dev_id);
    if (dev == NULL)
    {
        return -2; /* ENOENT */
    }

    ticket_lock_acquire(&s_driver_lock);

    if (dev->drv_id == 0U)
    {
        ticket_lock_release(&s_driver_lock);
        return -6; /* ENXIO */
    }

    drv = find_driver_by_id_internal(dev->drv_id);
    if (drv == NULL)
    {
        ticket_lock_release(&s_driver_lock);
        return -6; /* ENXIO */
    }

    if (drv->ref_count > 0U)
    {
        drv->ref_count--;
    }

    if (drv->ref_count == 0U)
    {
        dev->state = DRIVER_STATE_INITIALIZED;
    }

    ticket_lock_release(&s_driver_lock);

    return KERNEL_OK;
}

/**
 * @brief 从设备读取数据
 */
int64_t device_read(uint32_t dev_id, void *buf,
                    uint64_t size, uint64_t offset)
{
    device_desc_t *dev;
    driver_desc_t *drv;

    if (buf == NULL)
    {
        return -22; /* EINVAL */
    }

    dev = device_find_by_id(dev_id);
    if (dev == NULL)
    {
        return -2; /* ENOENT */
    }

    if (dev->drv_id == 0U)
    {
        return -6; /* ENXIO */
    }

    ticket_lock_acquire(&s_driver_lock);
    drv = find_driver_by_id_internal(dev->drv_id);
    ticket_lock_release(&s_driver_lock);

    if ((drv == NULL) || (drv->ops == NULL) || (drv->ops->read == NULL))
    {
        return -38; /* ENOSYS */
    }

    return drv->ops->read(dev->priv, buf, size, offset);
}

/**
 * @brief 向设备写入数据
 */
int64_t device_write(uint32_t dev_id, const void *buf,
                     uint64_t size, uint64_t offset)
{
    device_desc_t *dev;
    driver_desc_t *drv;

    if (buf == NULL)
    {
        return -22; /* EINVAL */
    }

    dev = device_find_by_id(dev_id);
    if (dev == NULL)
    {
        return -2; /* ENOENT */
    }

    if (dev->drv_id == 0U)
    {
        return -6; /* ENXIO */
    }

    ticket_lock_acquire(&s_driver_lock);
    drv = find_driver_by_id_internal(dev->drv_id);
    ticket_lock_release(&s_driver_lock);

    if ((drv == NULL) || (drv->ops == NULL) || (drv->ops->write == NULL))
    {
        return -38; /* ENOSYS */
    }

    return drv->ops->write(dev->priv, buf, size, offset);
}

/**
 * @brief 设备控制命令
 */
kernel_status_t device_ioctl(uint32_t dev_id, uint32_t cmd, void *arg)
{
    device_desc_t *dev;
    driver_desc_t *drv;

    dev = device_find_by_id(dev_id);
    if (dev == NULL)
    {
        return -2; /* ENOENT */
    }

    if (dev->drv_id == 0U)
    {
        return -6; /* ENXIO */
    }

    ticket_lock_acquire(&s_driver_lock);
    drv = find_driver_by_id_internal(dev->drv_id);
    ticket_lock_release(&s_driver_lock);

    if ((drv == NULL) || (drv->ops == NULL) || (drv->ops->ioctl == NULL))
    {
        return -38; /* ENOSYS */
    }

    return drv->ops->ioctl(dev->priv, cmd, arg);
}

/**
 * @brief 获取驱动子系统统计信息
 */
void driver_get_stats(driver_stats_t *stats)
{
    if (stats == NULL)
    {
        return;
    }

    ticket_lock_acquire(&s_driver_lock);
    kernel_memcpy(stats, &s_stats, sizeof(driver_stats_t));
    ticket_lock_release(&s_driver_lock);
}

/* ========================================================================
 * 内部辅助函数实现
 * ======================================================================== */

/**
 * @brief 分配一个空闲的驱动槽位
 *
 * @return 驱动描述符指针，无空闲时返回 NULL
 */
static driver_desc_t *alloc_driver_slot(void)
{
    uint32_t i;

    for (i = 0U; i < CONFIG_MAX_DRIVERS; i++)
    {
        if (s_drivers[i].in_use == 0U)
        {
            return &s_drivers[i];
        }
    }

    return NULL;
}

/**
 * @brief 分配一个空闲的设备槽位
 *
 * @details 查找 s_devices 数组中 dev_id 为 0（未使用）的槽位
 *
 * @return 设备描述符指针，无空闲时返回 NULL
 */
static device_desc_t *alloc_device_slot(void)
{
    uint32_t i;

    for (i = 0U; i < CONFIG_MAX_DEVICES; i++)
    {
        if (s_devices[i].dev_id == 0U)
        {
            return &s_devices[i];
        }
    }

    return NULL;
}

/**
 * @brief 检查驱动与设备是否匹配
 *
 * @details 匹配规则：
 *          1. compatible 字符串匹配（优先）
 *          2. PCI vendor_id/device_id 匹配
 *
 * @retval 1 匹配成功
 * @retval 0 不匹配
 */
static int32_t match_driver_device(const driver_desc_t *drv,
                                    const device_desc_t *dev)
{
    /* 检查类型匹配 */
    if (drv->type != dev->type)
    {
        return 0;
    }

    /* compatible 字符串匹配 */
    if (drv->match.compatible[0] != '\0')
    {
        /* 驱动有 compatible 条件，检查设备名称是否匹配 */
        if (kernel_strncmp(drv->match.compatible, dev->name,
                           sizeof(drv->match.compatible) - 1U) == 0)
        {
            return 1;
        }
    }

    /* PCI vendor_id/device_id 匹配 */
    if ((drv->match.vendor_id != 0U) && (drv->match.device_id != 0U))
    {
        if ((dev->pci_id.bus != 0U) || (dev->pci_id.device != 0U))
        {
            /* 设备有 PCI 信息时匹配 vendor/device */
            return 1;
        }
    }

    /* class_code 匹配 */
    if (drv->match.class_code != 0U)
    {
        return 1;
    }

    return 0;
}

/**
 * @brief 为设备查找匹配的驱动
 *
 * @details 遍历所有驱动，找到第一个匹配的驱动
 *
 * @return 匹配的驱动描述符指针，未找到返回 NULL
 */
static driver_desc_t *find_driver_for_device(const device_desc_t *dev)
{
    struct list_head *pos;

    list_for_each(pos, &s_driver_list)
    {
        driver_desc_t *drv;

        drv = container_of(pos, driver_desc_t, node);
        if (match_driver_device(drv, dev) != 0)
        {
            return drv;
        }
    }

    return NULL;
}

/**
 * @brief 通过驱动 ID 查找驱动（内部版本，不加锁）
 *
 * @param drv_id 驱动 ID
 *
 * @return 驱动描述符指针，未找到返回 NULL
 */
driver_desc_t *find_driver_by_id_internal(uint32_t drv_id)
{
    struct list_head *pos;

    if (drv_id == 0U)
    {
        return NULL;
    }

    list_for_each(pos, &s_driver_list)
    {
        driver_desc_t *drv;

        drv = container_of(pos, driver_desc_t, node);
        if (drv->drv_id == drv_id)
        {
            return drv;
        }
    }

    return NULL;
}
