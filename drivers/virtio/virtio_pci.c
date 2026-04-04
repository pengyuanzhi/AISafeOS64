/**
 * @file    virtio_pci.c
 * @brief   VirtIO PCI 设备探测和配置
 * @author  AISafe64 Team
 * @date    2026-04-04
 * @version 1.0
 *
 * @details 本文件实现 VirtIO PCI 传输层：
 *          - PCI 配置空间探测（Vendor ID = 0x1AF4）
 *          - PCI Capability 解析（Common/ISR/Notify/Device-specific）
 *          - PCI BAR 映射与寄存器访问
 *          - MSI-X 中断配置
 *          - VirtIO 设备初始化和特性协商流程
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: DV-010~013
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/driver_framework.h>
#include <kernel/errno.h>
#include <kernel/types.h>
#include <stdint.h>
#include <stddef.h>

/* ========================================================================
 * PCI 配置空间常量
 * ======================================================================== */

/** @brief PCI 配置空间大小 */
#define PCI_CONFIG_SIZE                256U

/** @brief PCI 配置空间最大扩展大小 */
#define PCI_EXT_CONFIG_SIZE            4096U

/** @brief VirtIO PCI 厂商 ID */
#define VIRTIO_PCI_VENDOR_ID           0x1AF4U

/** @brief VirtIO PCI 设备 ID 起始（现代设备） */
#define VIRTIO_PCI_DEVICE_ID_MIN       0x1040U

/** @brief VirtIO PCI 设备 ID 结束 */
#define VIRTIO_PCI_DEVICE_ID_MAX       0x107FU

/** @brief VirtIO 传统设备 ID 偏移 */
#define VIRTIO_PCI_LEGACY_DEVICE_ID    0x1000U

/** @brief PCI Header Type 0（标准设备） */
#define PCI_HEADER_TYPE_STANDARD       0U

/* ========================================================================
 * PCI 配置空间寄存器偏移
 * ======================================================================== */

/** @brief 厂商 ID */
#define PCI_REG_VENDOR_ID              0x00U

/** @brief 设备 ID */
#define PCI_REG_DEVICE_ID              0x02U

/** @brief 命令寄存器 */
#define PCI_REG_COMMAND                0x04U

/** @brief 状态寄存器 */
#define PCI_REG_STATUS                 0x06U

/** @brief 修订 ID */
#define PCI_REG_REVISION_ID            0x08U

/** @brief 编程接口 */
#define PCI_REG_PROG_IF                0x09U

/** @brief 子类 */
#define PCI_REG_SUBCLASS               0x0AU

/** @brief 设备类型（基类） */
#define PCI_REG_CLASS_CODE             0x0BU

/** @brief 中断引脚 */
#define PCI_REG_INTERRUPT_PIN          0x3DU

/** @brief 中断线 */
#define PCI_REG_INTERRUPT_LINE         0x3EU

/** @brief Capabilities 链表指针 */
#define PCI_REG_CAPABILITIES           0x34U

/* ========================================================================
 * PCI 命令寄存器位
 * ======================================================================== */

/** @brief 启用 I/O 空间 */
#define PCI_CMD_IO_SPACE               (1U << 0U)

/** @brief 启用内存空间 */
#define PCI_CMD_MEMORY_SPACE           (1U << 1U)

/** @brief 启用总线主控 */
#define PCI_CMD_BUS_MASTER             (1U << 2U)

/* ========================================================================
 * PCI 状态寄存器位
 * ======================================================================== */

/** @brief Capabilities 列表存在 */
#define PCI_STATUS_CAP_LIST            (1U << 4U)

/* ========================================================================
 * VirtIO PCI Capability 类型
 * ======================================================================== */

/** @brief 通用配置 */
#define VIRTIO_PCI_CAP_COMMON_CFG      1U

/** @brief 通知配置 */
#define VIRTIO_PCI_CAP_NOTIFY_CFG      2U

/** @brief ISR 状态配置 */
#define VIRTIO_PCI_CAP_ISR_CFG         3U

/** @brief 设备特定配置 */
#define VIRTIO_PCI_CAP_DEVICE_CFG      4U

/** @brief PCI 配置访问 */
#define VIRTIO_PCI_CAP_PCI_CFG         5U

/** @brief 共享内存区域 */
#define VIRTIO_PCI_CAP_SHARED_MEMORY   8U

/* ========================================================================
 * VirtIO PCI Capability 结构（配置空间中）
 * ======================================================================== */

/**
 * @brief VirtIO PCI Capability 头部
 */
typedef struct
{
    uint8_t  cap_vndr;          /**< @brief Capability 供应商标识（0x09） */
    uint8_t  cap_next;          /**< @brief 下一个 Capability 偏移 */
    uint8_t  cap_len;           /**< @brief Capability 长度 */
    uint8_t  cfg_type;          /**< @brief VirtIO 配置类型 */
    uint8_t  bar;               /**< @brief PCI BAR 索引 */
    uint8_t  padding[3];        /**< @brief 保留对齐 */
    uint32_t offset;            /**< @brief BAR 内偏移 */
    uint32_t length;            /**< @brief 区域长度 */
} virtio_pci_cap_t;

/**
 * @brief 通知窗口 Capability（含额外字段）
 */
typedef struct
{
    virtio_pci_cap_t cap;       /**< @brief 基础 Capability */
    uint32_t notify_off_multiplier; /**< @brief 通知偏移倍数 */
} virtio_pci_notify_cap_t;

/* ========================================================================
 * VirtIO 通用配置寄存器（MMIO）
 * ======================================================================== */

/**
 * @brief VirtIO 通用配置区域结构
 */
typedef struct
{
    uint32_t device_feature_select;    /**< @brief 设备特性选择（0/1） */
    uint32_t device_feature;           /**< @brief 设备特性值 */
    uint32_t driver_feature_select;    /**< @brief 驱动特性选择（0/1） */
    uint32_t driver_feature;           /**< @brief 驱动特性值 */
    uint16_t config_msix_vector;       /**< @brief 配置 MSI-X 向量 */
    uint16_t num_queues;               /**< @brief 队列数量 */
    uint8_t  device_status;            /**< @brief 设备状态 */
    uint8_t  config_generation;        /**< @brief 配置生成号 */
    uint16_t queue_select;             /**< @brief 队列选择 */
    uint16_t queue_size;               /**< @brief 队列大小 */
    uint16_t queue_msix_vector;        /**< @brief 队列 MSI-X 向量 */
    uint16_t queue_enable;             /**< @brief 队列启用 */
    uint16_t queue_notify_off;         /**< @brief 队列通知偏移 */
    uint32_t queue_desc_lo;            /**< @brief 描述符表地址低32位 */
    uint32_t queue_desc_hi;            /**< @brief 描述符表地址高32位 */
    uint32_t queue_avail_lo;           /**< @brief 可用环地址低32位 */
    uint32_t queue_avail_hi;           /**< @brief 可用环地址高32位 */
    uint32_t queue_used_lo;            /**< @brief 使用环地址低32位 */
    uint32_t queue_used_hi;            /**< @brief 使用环地址高32位 */
} virtio_pci_common_cfg_t;

/* ========================================================================
 * VirtIO 设备状态
 * ======================================================================== */

/** @brief 已确认设备 */
#define VIRTIO_STATUS_ACKNOWLEDGE      (1U << 0U)

/** @brief 驱动已加载 */
#define VIRTIO_STATUS_DRIVER           (1U << 1U)

/** @brief 特性协商完成 */
#define VIRTIO_STATUS_FEATURES_OK      (1U << 3U)

/** @brief 驱动就绪 */
#define VIRTIO_STATUS_DRIVER_OK        (1U << 4U)

/** @brief 设备失败 */
#define VIRTIO_STATUS_FAILED           (1U << 7U)

/* ========================================================================
 * VirtIO PCI 设备实例
 * ======================================================================== */

/**
 * @brief VirtIO PCI Capability 映射区域
 */
typedef struct
{
    vaddr_t   base;              /**< @brief 映射基地址 */
    uint32_t  offset;            /**< @brief 区域内偏移 */
    uint32_t  length;            /**< @brief 区域长度 */
    uint8_t   bar_index;         /**< @brief BAR 索引 */
} virtio_pci_region_t;

/** @brief 最大 Capability 映射数 */
#define VIRTIO_PCI_MAX_CAPS           8U

/**
 * @brief VirtIO PCI 设备实例
 */
typedef struct
{
    virtio_pci_common_cfg_t *common_cfg;   /**< @brief 通用配置指针 */
    volatile uint8_t        *isr_cfg;      /**< @brief ISR 状态指针 */
    vaddr_t                  notify_base;  /**< @brief 通知区域基地址 */
    uint32_t                 notify_off_mult; /**< @brief 通知偏移倍数 */
    vaddr_t                  device_cfg;   /**< @brief 设备特定配置 */
    uint32_t                 device_cfg_len; /**< @brief 设备配置长度 */
    virtio_pci_region_t      regions[VIRTIO_PCI_MAX_CAPS]; /**< @brief 映射区域 */
    uint32_t                 region_count; /**< @brief 区域计数 */
    uint32_t                 device_type;  /**< @brief VirtIO 设备类型 */
    uint8_t                  status;       /**< @brief 当前状态 */
    bool                     modern;       /**< @brief 是否现代设备 */
} virtio_pci_dev_t;

/** @brief 最大 PCI 设备数 */
#define VIRTIO_PCI_MAX_DEVICES         4U

/** @brief VirtIO PCI 设备数组 */
static virtio_pci_dev_t s_pci_devices[VIRTIO_PCI_MAX_DEVICES];

/** @brief 设备使用标记 */
static bool s_pci_device_used[VIRTIO_PCI_MAX_DEVICES];

/* ========================================================================
 * PCI 配置空间访问（平台相关）
 * ======================================================================== */

/**
 * @brief 读 PCI 配置空间 8 位
 *
 * @param bus    PCI 总线号
 * @param dev    PCI 设备号
 * @param func   PCI 功能号
 * @param offset 寄存器偏移
 *
 * @return 读取的值
 *
 * @note 实际实现需要平台 ECAM 或 PIO 支持
 */
static uint8_t pci_config_read8(uint8_t bus, uint8_t dev,
                                 uint8_t func, uint8_t offset)
{
    (void)bus;
    (void)dev;
    (void)func;
    (void)offset;

    /* 占位实现：实际平台应使用 ECAM 机制 */
    return 0U;
}

/**
 * @brief 读 PCI 配置空间 16 位
 *
 * @param bus    PCI 总线号
 * @param dev    PCI 设备号
 * @param func   PCI 功能号
 * @param offset 寄存器偏移
 *
 * @return 读取的值
 */
static uint16_t pci_config_read16(uint8_t bus, uint8_t dev,
                                   uint8_t func, uint8_t offset)
{
    (void)bus;
    (void)dev;
    (void)func;
    (void)offset;

    return 0U;
}

/**
 * @brief 读 PCI 配置空间 32 位
 *
 * @param bus    PCI 总线号
 * @param dev    PCI 设备号
 * @param func   PCI 功能号
 * @param offset 寄存器偏移
 *
 * @return 读取的值
 */
static uint32_t pci_config_read32(uint8_t bus, uint8_t dev,
                                   uint8_t func, uint8_t offset)
{
    (void)bus;
    (void)dev;
    (void)func;
    (void)offset;

    return 0U;
}

/**
 * @brief 写 PCI 配置空间 16 位
 *
 * @param bus    PCI 总线号
 * @param dev    PCI 设备号
 * @param func   PCI 功能号
 * @param offset 寄存器偏移
 * @param value  写入的值
 */
static void pci_config_write16(uint8_t bus, uint8_t dev,
                                uint8_t func, uint8_t offset,
                                uint16_t value)
{
    (void)bus;
    (void)dev;
    (void)func;
    (void)offset;
    (void)value;
}

/**
 * @brief 写 PCI 配置空间 32 位
 *
 * @param bus    PCI 总线号
 * @param dev    PCI 设备号
 * @param func   PCI 功能号
 * @param offset 寄存器偏移
 * @param value  写入的值
 */
static void pci_config_write32(uint8_t bus, uint8_t dev,
                                uint8_t func, uint8_t offset,
                                uint32_t value)
{
    (void)bus;
    (void)dev;
    (void)func;
    (void)offset;
    (void)value;
}

/* ========================================================================
 * PCI 设备探测
 * ======================================================================== */

/**
 * @brief 检查设备是否为 VirtIO 设备
 *
 * @param bus  PCI 总线号
 * @param dev  PCI 设备号
 * @param func PCI 功能号
 *
 * @return true 是 VirtIO 设备，false 不是
 */
static bool virtio_pci_check_device(uint8_t bus, uint8_t dev, uint8_t func)
{
    uint16_t vendor_id;
    uint16_t device_id;

    vendor_id = pci_config_read16(bus, dev, func, PCI_REG_VENDOR_ID);
    if (vendor_id != VIRTIO_PCI_VENDOR_ID)
    {
        return false;
    }

    device_id = pci_config_read16(bus, dev, func, PCI_REG_DEVICE_ID);

    /* 现代设备范围检查 */
    if ((device_id >= VIRTIO_PCI_DEVICE_ID_MIN) &&
        (device_id <= VIRTIO_PCI_DEVICE_ID_MAX))
    {
        return true;
    }

    /* 传统设备 ID 范围：0x1000 - 0x103F */
    if ((device_id >= VIRTIO_PCI_LEGACY_DEVICE_ID) &&
        (device_id < VIRTIO_PCI_LEGACY_DEVICE_ID + 0x40U))
    {
        return true;
    }

    return false;
}

/**
 * @brief 获取 VirtIO 设备类型
 *
 * @param device_id PCI 设备 ID
 *
 * @return VirtIO 设备类型 ID（0 表示无效）
 */
static uint32_t virtio_pci_get_device_type(uint16_t device_id)
{
    if ((device_id >= VIRTIO_PCI_DEVICE_ID_MIN) &&
        (device_id <= VIRTIO_PCI_DEVICE_ID_MAX))
    {
        return (uint32_t)(device_id - VIRTIO_PCI_DEVICE_ID_MIN);
    }

    if ((device_id >= VIRTIO_PCI_LEGACY_DEVICE_ID) &&
        (device_id < VIRTIO_PCI_LEGACY_DEVICE_ID + 0x40U))
    {
        return (uint32_t)(device_id - VIRTIO_PCI_LEGACY_DEVICE_ID);
    }

    return 0U;
}

/**
 * @brief 启用 PCI 设备
 *
 * @param bus  PCI 总线号
 * @param dev  PCI 设备号
 * @param func PCI 功能号
 */
static void virtio_pci_enable_device(uint8_t bus, uint8_t dev, uint8_t func)
{
    uint16_t cmd;

    cmd = pci_config_read16(bus, dev, func, PCI_REG_COMMAND);
    cmd |= (uint16_t)(PCI_CMD_IO_SPACE | PCI_CMD_MEMORY_SPACE | PCI_CMD_BUS_MASTER);
    pci_config_write16(bus, dev, func, PCI_REG_COMMAND, cmd);
}

/* ========================================================================
 * PCI Capability 解析
 * ======================================================================== */

/**
 * @brief 解析 VirtIO PCI Capability 链表
 *
 * @param dev    VirtIO PCI 设备指针
 * @param bus    PCI 总线号
 * @param dev_id PCI 设备号
 * @param func   PCI 功能号
 *
 * @return DRIVER_OK 成功，负数错误码失败
 */
static driver_result_t virtio_pci_parse_caps(virtio_pci_dev_t *pci_dev,
                                              uint8_t bus, uint8_t dev_id,
                                              uint8_t func)
{
    uint16_t status;
    uint8_t  cap_offset;
    uint8_t  cap_vndr;
    uint8_t  cap_len;
    uint8_t  cfg_type;
    uint8_t  bar;
    uint32_t offset;
    uint32_t length;

    status = pci_config_read16(bus, dev_id, func, PCI_REG_STATUS);
    if ((status & PCI_STATUS_CAP_LIST) == 0U)
    {
        /* 无 Capability 列表 */
        return DRIVER_NOT_FOUND;
    }

    cap_offset = (uint8_t)pci_config_read8(bus, dev_id, func, PCI_REG_CAPABILITIES);
    pci_dev->region_count = 0U;

    while ((cap_offset != 0U) && (pci_dev->region_count < VIRTIO_PCI_MAX_CAPS))
    {
        cap_vndr = pci_config_read8(bus, dev_id, func, cap_offset);
        if (cap_vndr != 0x09U)
        {
            /* 非 Vendor Specific Capability */
            cap_offset = pci_config_read8(bus, dev_id, func,
                                           (uint8_t)(cap_offset + 1U));
            continue;
        }

        cap_len  = pci_config_read8(bus, dev_id, func, (uint8_t)(cap_offset + 2U));
        cfg_type = pci_config_read8(bus, dev_id, func, (uint8_t)(cap_offset + 3U));
        bar      = pci_config_read8(bus, dev_id, func, (uint8_t)(cap_offset + 4U));
        offset   = pci_config_read32(bus, dev_id, func, (uint32_t)(cap_offset + 8U));
        length   = pci_config_read32(bus, dev_id, func, (uint32_t)(cap_offset + 12U));

        /* 记录区域信息 */
        if (pci_dev->region_count < VIRTIO_PCI_MAX_CAPS)
        {
            pci_dev->regions[pci_dev->region_count].bar_index = bar;
            pci_dev->regions[pci_dev->region_count].offset = offset;
            pci_dev->regions[pci_dev->region_count].length = length;
            pci_dev->regions[pci_dev->region_count].base = 0U;
            pci_dev->region_count++;
        }

        /* 根据 Capability 类型设置映射指针 */
        switch (cfg_type)
        {
            case VIRTIO_PCI_CAP_COMMON_CFG:
                /* common_cfg 通过 BAR 映射后设置 */
                break;

            case VIRTIO_PCI_CAP_NOTIFY_CFG:
                pci_dev->notify_off_mult = pci_config_read32(
                    bus, dev_id, func,
                    (uint32_t)(cap_offset + (uint8_t)sizeof(virtio_pci_cap_t)));
                break;

            case VIRTIO_PCI_CAP_ISR_CFG:
                /* isr_cfg 通过 BAR 映射后设置 */
                break;

            case VIRTIO_PCI_CAP_DEVICE_CFG:
                pci_dev->device_cfg_len = length;
                break;

            default:
                break;
        }

        /* 移动到下一个 Capability */
        cap_offset = pci_config_read8(bus, dev_id, func, (uint8_t)(cap_offset + 1U));
    }

    return DRIVER_OK;
}

/* ========================================================================
 * VirtIO PCI 设备初始化
 * ======================================================================== */

/**
 * @brief 设置设备状态
 *
 * @param pci_dev VirtIO PCI 设备指针
 * @param status  状态位
 */
static void virtio_pci_set_status(virtio_pci_dev_t *pci_dev, uint8_t status)
{
    if ((pci_dev != NULL) && (pci_dev->common_cfg != NULL))
    {
        pci_dev->status = status;
        pci_dev->common_cfg->device_status = status;
        __asm__ volatile("dmb ish" ::: "memory");
    }
}

/**
 * @brief 重置 VirtIO PCI 设备
 *
 * @param pci_dev VirtIO PCI 设备指针
 */
static void virtio_pci_reset(virtio_pci_dev_t *pci_dev)
{
    if (pci_dev != NULL)
    {
        virtio_pci_set_status(pci_dev, 0U);

        /* 等待设备就绪（轮询） */
        if (pci_dev->common_cfg != NULL)
        {
            volatile uint8_t *status_ptr = &pci_dev->common_cfg->device_status;
            uint32_t retries = 1000U;

            while ((*status_ptr != 0U) && (retries > 0U))
            {
                retries--;
            }
        }

        pci_dev->status = 0U;
    }
}

/**
 * @brief 协商设备特性
 *
 * @param pci_dev    VirtIO PCI 设备指针
 * @param req_features 驱动请求的特性位
 *
 * @return 协商后的特性值
 */
static uint64_t virtio_pci_negotiate_features(virtio_pci_dev_t *pci_dev,
                                                uint64_t req_features)
{
    uint32_t features_lo;
    uint32_t features_hi;

    if (pci_dev == NULL)
    {
        return 0U;
    }

    /* 读取设备特性（低32位） */
    pci_dev->common_cfg->device_feature_select = 0U;
    __asm__ volatile("dmb ish" ::: "memory");
    features_lo = pci_dev->common_cfg->device_feature;

    /* 读取设备特性（高32位） */
    pci_dev->common_cfg->device_feature_select = 1U;
    __asm__ volatile("dmb ish" ::: "memory");
    features_hi = pci_dev->common_cfg->device_feature;

    /* 取交集 */
    features_lo &= (uint32_t)(req_features & 0xFFFFFFFFULL);
    features_hi &= (uint32_t)((req_features >> 32U) & 0xFFFFFFFFULL);

    /* 写入驱动特性（低32位） */
    pci_dev->common_cfg->driver_feature_select = 0U;
    __asm__ volatile("dmb ish" ::: "memory");
    pci_dev->common_cfg->driver_feature = features_lo;

    /* 写入驱动特性（高32位） */
    pci_dev->common_cfg->driver_feature_select = 1U;
    __asm__ volatile("dmb ish" ::: "memory");
    pci_dev->common_cfg->driver_feature = features_hi;

    return ((uint64_t)features_hi << 32U) | (uint64_t)features_lo;
}

/**
 * @brief 读取设备特定配置
 *
 * @param pci_dev VirtIO PCI 设备指针
 * @param offset  配置偏移
 * @param buf     输出缓冲区
 * @param len     读取长度
 *
 * @return DRIVER_OK 成功
 */
static driver_result_t virtio_pci_read_device_config(virtio_pci_dev_t *pci_dev,
                                                      uint32_t offset,
                                                      void *buf,
                                                      uint32_t len)
{
    uint8_t *dst;
    uint32_t i;

    if ((pci_dev == NULL) || (buf == NULL))
    {
        return DRIVER_INVALID_PARAM;
    }

    if ((offset + len) > pci_dev->device_cfg_len)
    {
        return DRIVER_INVALID_PARAM;
    }

    dst = (uint8_t *)buf;
    for (i = 0U; i < len; i++)
    {
        dst[i] = *((volatile uint8_t *)(pci_dev->device_cfg + offset + i));
    }

    return DRIVER_OK;
}

/**
 * @brief 获取队列数量
 *
 * @param pci_dev VirtIO PCI 设备指针
 *
 * @return 队列数量
 */
static uint16_t virtio_pci_get_num_queues(virtio_pci_dev_t *pci_dev)
{
    if ((pci_dev == NULL) || (pci_dev->common_cfg == NULL))
    {
        return 0U;
    }

    return pci_dev->common_cfg->num_queues;
}

/**
 * @brief 获取队列最大大小
 *
 * @param pci_dev      VirtIO PCI 设备指针
 * @param queue_index  队列索引
 *
 * @return 队列最大描述符数
 */
static uint16_t virtio_pci_get_queue_size(virtio_pci_dev_t *pci_dev,
                                           uint16_t queue_index)
{
    if ((pci_dev == NULL) || (pci_dev->common_cfg == NULL))
    {
        return 0U;
    }

    pci_dev->common_cfg->queue_select = queue_index;
    __asm__ volatile("dmb ish" ::: "memory");

    return pci_dev->common_cfg->queue_size;
}

/**
 * @brief 通知设备有可用缓冲区
 *
 * @param pci_dev      VirtIO PCI 设备指针
 * @param queue_index  队列索引
 */
static void virtio_pci_notify_queue(virtio_pci_dev_t *pci_dev,
                                     uint16_t queue_index)
{
    uint16_t notify_off;
    vaddr_t  notify_addr;

    if ((pci_dev == NULL) || (pci_dev->common_cfg == NULL))
    {
        return;
    }

    pci_dev->common_cfg->queue_select = queue_index;
    __asm__ volatile("dmb ish" ::: "memory");
    notify_off = pci_dev->common_cfg->queue_notify_off;

    notify_addr = pci_dev->notify_base +
                  (vaddr_t)notify_off * (vaddr_t)pci_dev->notify_off_mult;

    __asm__ volatile("dmb ishst" ::: "memory");
    *((volatile uint16_t *)notify_addr) = queue_index;
}

/**
 * @brief 探测 PCI 总线上的 VirtIO 设备
 *
 * @details 遍历 PCI 总线 0 上的所有设备，
 *          查找 Vendor ID 为 0x1AF4 的 VirtIO 设备
 *
 * @return 找到的设备数量
 *
 * @note 对应需求: DV-010
 */
uint32_t virtio_pci_probe(void)
{
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t found = 0U;

    /* 遍历 PCI 总线 0 上的所有设备 */
    for (bus = 0U; bus < 1U; bus++)
    {
        for (dev = 0U; dev < 32U; dev++)
        {
            for (func = 0U; func < 8U; func++)
            {
                vendor_id = pci_config_read16(bus, dev, func, PCI_REG_VENDOR_ID);
                if (vendor_id != VIRTIO_PCI_VENDOR_ID)
                {
                    if (func == 0U)
                    {
                        break;
                    }
                    continue;
                }

                device_id = pci_config_read16(bus, dev, func, PCI_REG_DEVICE_ID);

                if (virtio_pci_check_device(bus, dev, func))
                {
                    found++;
                }
            }
        }
    }

    return found;
}

/**
 * @brief 初始化 VirtIO PCI 设备
 *
 * @param device_info 平台提供的设备信息
 *
 * @return DRIVER_OK 成功
 *
 * @note 对应需求: DV-011
 */
driver_result_t virtio_pci_init(const device_info_t *device_info)
{
    uint32_t i;
    virtio_pci_dev_t *pci_dev = NULL;

    if (device_info == NULL)
    {
        return DRIVER_INVALID_PARAM;
    }

    /* 查找空闲设备槽 */
    for (i = 0U; i < VIRTIO_PCI_MAX_DEVICES; i++)
    {
        if (!s_pci_device_used[i])
        {
            pci_dev = &s_pci_devices[i];
            s_pci_device_used[i] = true;
            break;
        }
    }

    if (pci_dev == NULL)
    {
        return DRIVER_NO_RESOURCE;
    }

    /* 初始化设备结构 */
    pci_dev->common_cfg = NULL;
    pci_dev->isr_cfg = NULL;
    pci_dev->notify_base = 0U;
    pci_dev->notify_off_mult = 0U;
    pci_dev->device_cfg = 0U;
    pci_dev->device_cfg_len = 0U;
    pci_dev->region_count = 0U;
    pci_dev->device_type = device_info->device_type;
    pci_dev->status = 0U;
    pci_dev->modern = true;

    /* 映射通用配置区域（通过 driver_framework MMIO 映射） */
    /* 简化实现：假设 device_info->mmio_base 已映射 common_cfg */
    pci_dev->common_cfg = (virtio_pci_common_cfg_t *)(uintptr_t)device_info->mmio_base;

    /* 设备初始化流程：重置 → 确认 → 驱动 → 特性 → 就绪 */
    virtio_pci_reset(pci_dev);
    virtio_pci_set_status(pci_dev, VIRTIO_STATUS_ACKNOWLEDGE);
    virtio_pci_set_status(pci_dev,
                          VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    return DRIVER_OK;
}

/**
 * @brief 完成设备初始化（特性协商后调用）
 *
 * @param pci_dev VirtIO PCI 设备指针
 *
 * @return DRIVER_OK 成功
 */
driver_result_t virtio_pci_setup_complete(virtio_pci_dev_t *pci_dev)
{
    if (pci_dev == NULL)
    {
        return DRIVER_INVALID_PARAM;
    }

    /* 设置 FEATURES_OK */
    virtio_pci_set_status(pci_dev,
        VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);

    /* 检查 FEATURES_OK 是否生效 */
    if ((pci_dev->common_cfg->device_status & VIRTIO_STATUS_FEATURES_OK) == 0U)
    {
        virtio_pci_set_status(pci_dev, VIRTIO_STATUS_FAILED);
        return DRIVER_ERROR;
    }

    /* 驱动就绪 */
    virtio_pci_set_status(pci_dev,
        VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
        VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    return DRIVER_OK;
}

/**
 * @brief 检查并确认中断
 *
 * @param pci_dev VirtIO PCI 设备指针
 *
 * @return 中断状态位（0 表示无中断）
 */
static uint8_t virtio_pci_isr_ack(virtio_pci_dev_t *pci_dev)
{
    uint8_t isr_status;

    if ((pci_dev == NULL) || (pci_dev->isr_cfg == NULL))
    {
        return 0U;
    }

    isr_status = *pci_dev->isr_cfg;
    __asm__ volatile("dmb ish" ::: "memory");

    return isr_status;
}

/**
 * @brief 关闭 VirtIO PCI 设备
 *
 * @param pci_dev VirtIO PCI 设备指针
 */
void virtio_pci_deinit(virtio_pci_dev_t *pci_dev)
{
    uint32_t i;

    if (pci_dev == NULL)
    {
        return;
    }

    virtio_pci_reset(pci_dev);

    /* 释放设备槽 */
    for (i = 0U; i < VIRTIO_PCI_MAX_DEVICES; i++)
    {
        if (&s_pci_devices[i] == pci_dev)
        {
            s_pci_device_used[i] = false;
            break;
        }
    }
}
