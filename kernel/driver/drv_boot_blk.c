/**
 * @file    drv_boot_blk.c
 * @brief   引导期 virtio-blk 轮询读取器（极简版）
 * @author  AISafeOS Team
 * @date    2026-07-04
 * @version 1.1
 *
 * @details 启动期最小 virtio-mmio block 读取器，仅用于读取磁盘上的
 *          用户态驱动 ELF。轮询模式（无中断），只读，Legacy 模式。
 *          virtqueue 用连续内存（Legacy 要求 desc/avail/used 在同一区域）。
 */

#include <kernel/types.h>
#include <arch/arm64/hal.h>
#include <kernel/virt_phys.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ========================================================================
 * virtio-mmio 寄存器偏移（Legacy 模式）
 * ======================================================================== */

#define VIRTIO_MMIO_MAGIC_VALUE       0x000U
#define VIRTIO_MMIO_VERSION           0x004U
#define VIRTIO_MMIO_DEVICE_ID         0x008U
#define VIRTIO_MMIO_GUEST_FEATURES    0x020U
#define VIRTIO_MMIO_GUEST_PAGE_SIZE   0x028U
#define VIRTIO_MMIO_QUEUE_SEL         0x030U
#define VIRTIO_MMIO_QUEUE_NUM_MAX     0x034U
#define VIRTIO_MMIO_QUEUE_NUM         0x038U
#define VIRTIO_MMIO_QUEUE_ALIGN       0x03CU
#define VIRTIO_MMIO_QUEUE_PFN         0x040U
#define VIRTIO_MMIO_QUEUE_NOTIFY      0x050U
#define VIRTIO_MMIO_INTERRUPT_STATUS  0x060U
#define VIRTIO_MMIO_INTERRUPT_ACK     0x064U
#define VIRTIO_MMIO_STATUS            0x070U

#define VIRTIO_STATUS_ACKNOWLEDGE     0x01U
#define VIRTIO_STATUS_DRIVER          0x02U
#define VIRTIO_STATUS_DRIVER_OK       0x04U
#define VIRTIO_STATUS_FEATURES_OK     0x08U

#define VIRTIO_BLK_T_IN               0U
#define VIRTIO_BLK_S_OK               0U

#define VIRTQ_DESC_F_NEXT             (1U << 0U)
#define VIRTQ_DESC_F_WRITE            (1U << 1U)

#define VIRTIO_MAGIC                  0x74726976U
/* TTBR1 高地址线性映射：物理 0x0A000000 → 虚拟 0xFFFF00000A000000 */
#define VIRTIO_MMIO_BASE              0xFFFF00000A000000ULL
#define VIRTIO_MMIO_SLOT_SIZE         0x200ULL
#define VIRTIO_MMIO_SLOT_COUNT        32U

/* ========================================================================
 * virtqueue 数据结构
 * ======================================================================== */

#define BOOT_QUEUE_SIZE  3U  /* req → data → resp 三描述符链 */

/** @brief 描述符（16 字节） */
typedef struct
{
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) boot_desc_t;

/** @brief virtio-blk 请求头 */
typedef struct
{
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed)) boot_blk_req_t;

/* ========================================================================
 * 连续 virtqueue 内存（Legacy 要求）
 *
 * 布局：desc_table(3*16=48B) → avail_ring → [pad to 4K] → used_ring
 * QUEUE_PFN = &s_vq_mem >> 12
 * ======================================================================== */

/** @brief 描述符表大小 */
#define DESC_TABLE_SIZE   ((uint32_t)(BOOT_QUEUE_SIZE * sizeof(boot_desc_t)))

/** @brief 可用环大小（flags + idx + 3 entries + used_event） */
#define AVAIL_RING_SIZE   (4U + BOOT_QUEUE_SIZE * 2U + 2U)

/** @brief 使用环大小 */
#define USED_RING_SIZE    (4U + BOOT_QUEUE_SIZE * 8U + 2U)

/** @brief 整个 virtqueue 区域（页对齐，足够大） */
static uint8_t s_vq_mem[8192] __attribute__((aligned(4096)));

/* virtqueue 指针（在 s_vq_mem 内的偏移） */
static boot_desc_t *s_desc;       /* desc_table @ offset 0 */
static uint16_t *s_avail_flags;   /* avail flags @ offset DESC_TABLE_SIZE */
static uint16_t *s_avail_idx;
static uint16_t *s_avail_ring;    /* avail entries */
static uint16_t *s_avail_event;
static uint16_t *s_used_flags;    /* used @ 4K boundary */
static uint16_t *s_used_idx;
static uint32_t *s_used_ring;     /* used entries (id+len pairs) */

/* ========================================================================
 * DMA 缓冲区
 * ======================================================================== */

static boot_blk_req_t s_req __attribute__((aligned(64)));
static uint8_t s_data_buf[512] __attribute__((aligned(64)));
static uint8_t s_resp_status __attribute__((aligned(64)));

/* ========================================================================
 * MMIO 辅助
 * ======================================================================== */

static uint32_t mmio_read32(uint64_t base, uint32_t offset)
{
    volatile uint32_t *reg = (volatile uint32_t *)(uintptr_t)(base + (uint64_t)offset);
    return *reg;
}

static void mmio_write32(uint64_t base, uint32_t offset, uint32_t value)
{
    volatile uint32_t *reg = (volatile uint32_t *)(uintptr_t)(base + (uint64_t)offset);
    *reg = value;
}

/* ========================================================================
 * 状态
 * ======================================================================== */

static uint64_t s_boot_mmio_base = 0ULL;

/* ========================================================================
 * 初始化 virtqueue 指针
 * ======================================================================== */

static void init_vq_pointers(void)
{
    uint8_t *base = s_vq_mem;
    uint32_t offset = DESC_TABLE_SIZE;

    s_desc = (boot_desc_t *)(void *)base;

    s_avail_flags = (uint16_t *)(void *)(base + offset);
    s_avail_idx = s_avail_flags + 1;
    s_avail_ring = s_avail_flags + 2;
    s_avail_event = s_avail_ring + BOOT_QUEUE_SIZE;

    /* used ring 对齐到 4K */
    offset = 4096U;
    s_used_flags = (uint16_t *)(void *)(base + offset);
    s_used_idx = s_used_flags + 1;
    s_used_ring = (uint32_t *)(void *)(s_used_flags + 2);
}

/* ========================================================================
 * 设备发现与初始化
 * ======================================================================== */

static uint64_t boot_find_device(void)
{
    uint32_t slot;
    for (slot = 0U; slot < VIRTIO_MMIO_SLOT_COUNT; slot++)
    {
        uint64_t base = VIRTIO_MMIO_BASE + (uint64_t)slot * VIRTIO_MMIO_SLOT_SIZE;
        if ((mmio_read32(base, VIRTIO_MMIO_MAGIC_VALUE) == VIRTIO_MAGIC) &&
            (mmio_read32(base, VIRTIO_MMIO_DEVICE_ID) == 2U))
        {
            return base;
        }
    }
    return 0ULL;
}

static int32_t boot_init_device(uint64_t mmio_base)
{
    uint32_t qnum_max;

    /* 设置 Guest 页大小 */
    mmio_write32(mmio_base, VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096U);

    /* 状态协商 */
    mmio_write32(mmio_base, VIRTIO_MMIO_STATUS, 0U);
    mmio_write32(mmio_base, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    mmio_write32(mmio_base, VIRTIO_MMIO_STATUS,
                 VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    mmio_write32(mmio_base, VIRTIO_MMIO_GUEST_FEATURES, 0U);
    mmio_write32(mmio_base, VIRTIO_MMIO_STATUS,
                 VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER
                 | VIRTIO_STATUS_FEATURES_OK);

    /* 队列 0 */
    mmio_write32(mmio_base, VIRTIO_MMIO_QUEUE_SEL, 0U);
    qnum_max = mmio_read32(mmio_base, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (qnum_max == 0U)
    {
        return -1;
    }

    mmio_write32(mmio_base, VIRTIO_MMIO_QUEUE_NUM, BOOT_QUEUE_SIZE);

    /* Legacy：QUEUE_ALIGN + QUEUE_PFN（整个 vq_mem 的物理地址页帧号） */
    mmio_write32(mmio_base, VIRTIO_MMIO_QUEUE_ALIGN, 4096U);
    mmio_write32(mmio_base, VIRTIO_MMIO_QUEUE_PFN,
                 (uint32_t)((uint64_t)virt_to_phys(s_vq_mem) >> 12U));

    /* DRIVER_OK */
    mmio_write32(mmio_base, VIRTIO_MMIO_STATUS,
                 VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER
                 | VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    /* 清中断 */
    mmio_write32(mmio_base, VIRTIO_MMIO_INTERRUPT_ACK, 1U);

    return 0;
}

/* ========================================================================
 * 公共接口
 * ======================================================================== */

int32_t boot_blk_init(void)
{
    uint64_t mmio_base;

    init_vq_pointers();

    /* 清零 virtqueue */
    {
        uint32_t i;
        for (i = 0U; i < sizeof(s_vq_mem); i++)
        {
            s_vq_mem[i] = 0U;
        }
    }
    *s_avail_idx = 0U;
    *s_used_idx = 0U;

    mmio_base = boot_find_device();
    if (mmio_base == 0ULL)
    {
        return -1;
    }

    if (boot_init_device(mmio_base) != 0)
    {
        return -1;
    }

    s_boot_mmio_base = mmio_base;
    return 0;
}

int32_t boot_blk_read_sector(uint64_t sector, void *buf)
{
    uint64_t mmio_base;
    uint16_t avail_idx;
    uint16_t used_idx;
    uint32_t timeout;

    if ((s_boot_mmio_base == 0ULL) || (buf == NULL))
    {
        return -1;
    }
    mmio_base = s_boot_mmio_base;

    /* 构建请求 */
    s_req.type = VIRTIO_BLK_T_IN;
    s_req.reserved = 0U;
    s_req.sector = sector;
    s_resp_status = 0xFFU;

    /* 描述符链：[0]req → [1]data(write) → [2]resp(write) */
    s_desc[0].addr = virt_to_phys(&s_req);
    s_desc[0].len = (uint32_t)sizeof(boot_blk_req_t);
    s_desc[0].flags = VIRTQ_DESC_F_NEXT;
    s_desc[0].next = 1U;

    s_desc[1].addr = virt_to_phys(s_data_buf);
    s_desc[1].len = 512U;
    s_desc[1].flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;
    s_desc[1].next = 2U;

    s_desc[2].addr = virt_to_phys(&s_resp_status);
    s_desc[2].len = 1U;
    s_desc[2].flags = VIRTQ_DESC_F_WRITE;
    s_desc[2].next = 0U;

    /* 提交到可用环 */
    avail_idx = *s_avail_idx;
    s_avail_ring[avail_idx % BOOT_QUEUE_SIZE] = 0U;
    *s_avail_idx = (uint16_t)(avail_idx + 1U);

    __asm__ volatile("dsb sy" ::: "memory");

    /* 通知设备 */
    mmio_write32(mmio_base, VIRTIO_MMIO_QUEUE_NOTIFY, 0U);

    /* 轮询 used ring */
    used_idx = *s_used_idx;
    timeout = 10000000U;
    while ((*s_used_idx == used_idx) && (timeout > 0U))
    {
        __asm__ volatile("dsb sy" ::: "memory");
        timeout--;
    }

    if (timeout == 0U)
    {
        return -1;
    }

    /* DMA 一致性：invalidate data_buf 和 resp 的 cache，
     * 确保读到设备写入的数据（非一致性 DMA） */
    hal_dcache_invalidate((uint64_t)(uintptr_t)s_data_buf, 512U);
    hal_dcache_invalidate((uint64_t)(uintptr_t)&s_resp_status, 8U);
    __asm__ volatile("dsb sy" ::: "memory");

    if (s_resp_status != VIRTIO_BLK_S_OK)
    {
        return -1;
    }

    /* 拷贝数据 */
    {
        uint8_t *dst = (uint8_t *)buf;
        uint32_t i;
        for (i = 0U; i < 512U; i++)
        {
            dst[i] = s_data_buf[i];
        }
    }

    return 0;
}

int32_t boot_blk_read(uint64_t offset, void *buf, uint32_t size)
{
    uint64_t sector = offset / 512ULL;
    uint32_t nsectors = size / 512U;
    uint8_t *dst = (uint8_t *)buf;
    uint32_t s;

    if ((offset % 512ULL != 0ULL) || (size % 512U != 0U))
    {
        return -1;
    }

    for (s = 0U; s < nsectors; s++)
    {
        if (boot_blk_read_sector(sector + (uint64_t)s, dst + s * 512U) != 0)
        {
            return -1;
        }
    }
    return 0;
}

bool boot_blk_is_ready(void)
{
    return (s_boot_mmio_base != 0ULL);
}
