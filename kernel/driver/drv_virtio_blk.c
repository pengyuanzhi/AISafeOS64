/**
 * @file    drv_virtio_blk.c
 * @brief   VirtIO Block 设备内核驱动（适配新驱动框架）
 * @author  AISafe64 Team
 * @date    2026-04-08
 * @version 1.0
 *
 * @details VirtIO MMIO Block 设备驱动：
 *          - VirtIO MMIO 寄存器操作
 *          - 设备初始化和特性协商
 *          - 块设备容量查询
 *          - 使用新 driver.h 框架注册
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: DV-020~023
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/driver.h>
#include <hal.h>
#include <stdint.h>

/* ========================================================================
 * VirtIO MMIO 寄存器偏移
 * ======================================================================== */

#define VIRTIO_MMIO_MAGIC_VALUE     0x000U   /**< @brief Magic 值寄存器 */
#define VIRTIO_MMIO_VERSION         0x004U   /**< @brief 版本寄存器 */
#define VIRTIO_MMIO_DEVICE_ID       0x008U   /**< @brief 设备 ID 寄存器 */
#define VIRTIO_MMIO_VENDOR_ID       0x00CU   /**< @brief 厂商 ID 寄存器 */
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010U   /**< @brief 设备特性寄存器 */
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020U   /**< @brief 驱动特性寄存器 */
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028U   /**< @brief Guest 页大小 */
#define VIRTIO_MMIO_QUEUE_SEL       0x030U   /**< @brief 队列选择寄存器 */
#define VIRTIO_MMIO_QUEUE_NUM_MAX   0x034U   /**< @brief 队列最大长度 */
#define VIRTIO_MMIO_QUEUE_NUM       0x038U   /**< @brief 队列长度 */
#define VIRTIO_MMIO_QUEUE_READY     0x044U   /**< @brief 队列就绪标志 */
#define VIRTIO_MMIO_QUEUE_NOTIFY    0x050U   /**< @brief 队列通知寄存器 */
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060U  /**< @brief 中断状态 */
#define VIRTIO_MMIO_INTERRUPT_ACK   0x064U   /**< @brief 中断确认 */
#define VIRTIO_MMIO_STATUS          0x070U   /**< @brief 设备状态寄存器 */
#define VIRTIO_MMIO_QUEUE_DESC_LOW  0x080U   /**< @brief 描述符表低32位 */
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084U   /**< @brief 描述符表高32位 */
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW 0x090U   /**< @brief Available 环低32位 */
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094U  /**< @brief Available 环高32位 */
#define VIRTIO_MMIO_QUEUE_USED_LOW  0x0A0U   /**< @brief Used 环低32位 */
#define VIRTIO_MMIO_QUEUE_USED_HIGH 0x0A4U   /**< @brief Used 环高32位 */
#define VIRTIO_MMIO_CONFIG          0x100U   /**< @brief 设备配置空间 */

/* ========================================================================
 * VirtIO 常量
 * ======================================================================== */

/** @brief VirtIO Magic 值 ('virt' 小端) */
#define VIRTIO_MAGIC                0x74726976U

/** @brief VirtIO Block 设备类型 ID */
#define VIRTIO_BLK_DEVICE_ID        2U

/** @brief 块设备扇区大小 */
#define VIRTIO_BLK_SECTOR_SIZE      512U

/* VirtIO 状态位 */
#define VIRTIO_STATUS_ACKNOWLEDGE   0x01U   /**< @brief 已确认设备 */
#define VIRTIO_STATUS_DRIVER        0x02U   /**< @brief 驱动已找到 */
#define VIRTIO_STATUS_FEATURES_OK   0x08U   /**< @brief 特性协商完成 */
#define VIRTIO_STATUS_DRIVER_OK     0x04U   /**< @brief 驱动就绪 */
#define VIRTIO_STATUS_FAILED        0x80U   /**< @brief 设备故障 */

/* ========================================================================
 * MMIO 寄存器访问辅助
 * ======================================================================== */

/**
 * @brief 从 MMIO 地址读取 32 位值
 *
 * @param base   MMIO 基地址
 * @param offset 寄存器偏移
 *
 * @return 读取的值
 */
static uint32_t mmio_read32(uint64_t base, uint32_t offset)
{
    volatile uint32_t *reg;
    reg = (volatile uint32_t *)(base + (uint64_t)offset);
    return *reg;
}

/**
 * @brief 向 MMIO 地址写入 32 位值
 *
 * @param base   MMIO 基地址
 * @param offset 寄存器偏移
 * @param value  写入的值
 */
static void mmio_write32(uint64_t base, uint32_t offset, uint32_t value)
{
    volatile uint32_t *reg;
    reg = (volatile uint32_t *)(base + (uint64_t)offset);
    *reg = value;
}

/* ========================================================================
 * 驱动私有数据
 * ======================================================================== */

/**
 * @brief VirtIO Block 驱动私有数据
 */
typedef struct
{
    uint64_t    mmio_base;      /**< @brief VirtIO MMIO 基地址 */
    uint32_t    irq;            /**< @brief 中断号 */
    uint64_t    capacity;       /**< @brief 块设备容量（扇区数） */
    uint32_t    sector_size;    /**< @brief 扇区大小 */
    uint32_t    initialized;    /**< @brief 初始化标志 */
} virtio_blk_priv_t;

/** @brief VirtIO Block 驱动私有数据实例 */
static virtio_blk_priv_t s_blk_priv;

/* ========================================================================
 * 驱动操作函数实现
 * ======================================================================== */

/**
 * @brief 探测并初始化 VirtIO Block 设备
 *
 * @details 执行 VirtIO MMIO 初始化序列：
 *          1. 验证 Magic 值
 *          2. 检查设备类型
 *          3. 执行状态转换: ACKNOWLEDGE → DRIVER → FEATURES_OK → DRIVER_OK
 *          4. 读取设备容量
 *
 * @param dev_data 设备描述符指针
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t virtio_blk_probe(void *dev_data)
{
    device_desc_t *dev = (device_desc_t *)dev_data;
    uint32_t magic;
    uint32_t device_id;
    uint32_t status;

    if (dev == NULL)
    {
        return -22; /* EINVAL */
    }

    s_blk_priv.mmio_base = (uint64_t)dev->mmio_base;
    s_blk_priv.irq = dev->irq;
    s_blk_priv.sector_size = VIRTIO_BLK_SECTOR_SIZE;
    s_blk_priv.initialized = 0U;

    /* 步骤 1: 验证 VirtIO Magic */
    magic = mmio_read32(s_blk_priv.mmio_base, VIRTIO_MMIO_MAGIC_VALUE);
    if (magic != VIRTIO_MAGIC)
    {
        return -6; /* ENXIO: 不是 VirtIO 设备 */
    }

    /* 步骤 2: 检查设备类型 (block = 2) */
    device_id = mmio_read32(s_blk_priv.mmio_base, VIRTIO_MMIO_DEVICE_ID);
    if (device_id != VIRTIO_BLK_DEVICE_ID)
    {
        return -6; /* ENXIO: 不是 block 设备 */
    }

    /* 步骤 3: VirtIO 初始化序列 */
    /* ACKNOWLEDGE: 确认设备存在 */
    status = VIRTIO_STATUS_ACKNOWLEDGE;
    mmio_write32(s_blk_priv.mmio_base, VIRTIO_MMIO_STATUS, status);

    /* DRIVER: 声明驱动存在 */
    status |= VIRTIO_STATUS_DRIVER;
    mmio_write32(s_blk_priv.mmio_base, VIRTIO_MMIO_STATUS, status);

    /* 不协商任何特性（简化实现） */
    mmio_write32(s_blk_priv.mmio_base, VIRTIO_MMIO_DRIVER_FEATURES, 0U);

    /* FEATURES_OK: 特性协商完成 */
    status |= VIRTIO_STATUS_FEATURES_OK;
    mmio_write32(s_blk_priv.mmio_base, VIRTIO_MMIO_STATUS, status);

    /* 验证设备接受了 FEATURES_OK */
    if ((mmio_read32(s_blk_priv.mmio_base, VIRTIO_MMIO_STATUS)
         & VIRTIO_STATUS_FEATURES_OK) == 0U)
    {
        mmio_write32(s_blk_priv.mmio_base, VIRTIO_MMIO_STATUS,
                     VIRTIO_STATUS_FAILED);
        return -5; /* EIO */
    }

    /* DRIVER_OK: 驱动就绪 */
    status |= VIRTIO_STATUS_DRIVER_OK;
    mmio_write32(s_blk_priv.mmio_base, VIRTIO_MMIO_STATUS, status);

    /* 步骤 4: 读取设备容量（配置空间前 8 字节） */
    s_blk_priv.capacity = (uint64_t)mmio_read32(
        s_blk_priv.mmio_base, VIRTIO_MMIO_CONFIG);
    s_blk_priv.capacity |= ((uint64_t)mmio_read32(
        s_blk_priv.mmio_base, VIRTIO_MMIO_CONFIG + 4U)) << 32U;

    s_blk_priv.initialized = 1U;

    return KERNEL_OK;
}

/**
 * @brief 移除 VirtIO Block 设备
 */
static kernel_status_t virtio_blk_remove(void *dev_data)
{
    (void)dev_data;

    if (s_blk_priv.initialized != 0U)
    {
        /* 重置设备 */
        mmio_write32(s_blk_priv.mmio_base, VIRTIO_MMIO_STATUS, 0U);
        s_blk_priv.initialized = 0U;
    }

    return KERNEL_OK;
}

/**
 * @brief 读取块设备容量信息
 *
 * @note 简化实现：返回设备总字节数
 */
static int64_t virtio_blk_read(void *dev_data, void *buf,
                                uint64_t size, uint64_t offset)
{
    (void)dev_data;
    (void)buf;
    (void)offset;

    if (s_blk_priv.initialized == 0U)
    {
        return -6; /* ENXIO */
    }

    /* 简化实现：返回容量信息 */
    return (int64_t)(s_blk_priv.capacity *
                     (uint64_t)s_blk_priv.sector_size);
}

/**
 * @brief 写入块设备（简化版）
 */
static int64_t virtio_blk_write(void *dev_data, const void *buf,
                                 uint64_t size, uint64_t offset)
{
    (void)dev_data;
    (void)buf;
    (void)offset;

    if (s_blk_priv.initialized == 0U)
    {
        return -6;
    }

    return (int64_t)size;
}

/**
 * @brief VirtIO Block 设备控制命令
 *
 * @param cmd 0=GET_CAPACITY, 1=GET_SECTOR_SIZE
 */
static kernel_status_t virtio_blk_ioctl(void *dev_data, uint32_t cmd,
                                         void *arg)
{
    (void)dev_data;

    if (arg == NULL)
    {
        return -22;
    }

    if (cmd == 0U) /* GET_CAPACITY */
    {
        *((uint64_t *)arg) = s_blk_priv.capacity;
        return KERNEL_OK;
    }

    if (cmd == 1U) /* GET_SECTOR_SIZE */
    {
        *((uint32_t *)arg) = s_blk_priv.sector_size;
        return KERNEL_OK;
    }

    return -22;
}

/**
 * @brief VirtIO Block 中断处理函数
 */
static void virtio_blk_irq(uint32_t irq, void *dev_data)
{
    (void)irq;
    (void)dev_data;
    /* 简化实现：实际应处理 virtqueue 完成中断 */
}

/* ========================================================================
 * 驱动操作函数表
 * ======================================================================== */

/** @brief VirtIO Block 驱动操作函数表 */
static const driver_ops_t s_drv_virtio_blk_ops =
{
    virtio_blk_probe,   /**< @brief probe */
    virtio_blk_remove,  /**< @brief remove */
    NULL,               /**< @brief suspend */
    NULL,               /**< @brief resume */
    virtio_blk_read,    /**< @brief read */
    virtio_blk_write,   /**< @brief write */
    virtio_blk_ioctl,   /**< @brief ioctl */
    virtio_blk_irq      /**< @brief irq_handler */
};

/* ========================================================================
 * 驱动注册入口
 * ======================================================================== */

/**
 * @brief 注册 VirtIO Block 驱动到驱动框架
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t drv_virtio_blk_register(void)
{
    driver_match_t match;
    uint32_t i;

    /* 清零 match 结构 */
    for (i = 0U; i < sizeof(match.compatible); i++)
    {
        match.compatible[i] = '\0';
    }

    /* compatible = "virtio,blk" */
    match.compatible[0U] = 'v';
    match.compatible[1U] = 'i';
    match.compatible[2U] = 'r';
    match.compatible[3U] = 't';
    match.compatible[4U] = 'i';
    match.compatible[5U] = 'o';
    match.compatible[6U] = ',';
    match.compatible[7U] = 'b';
    match.compatible[8U] = 'l';
    match.compatible[9U] = 'k';
    match.vendor_id = 0U;
    match.device_id = 0U;
    match.class_code = 0U;

    return driver_register_kern("virtio-blk", DRIVER_TYPE_BLOCK,
                                 &match, &s_drv_virtio_blk_ops);
}
