/**
 * @file    virtio_blk_driver.c
 * @brief   用户态 VirtIO Block 驱动（独立 ELF）
 * @author  AISafeOS Team
 * @date    2026-07-04
 * @version 1.0
 *
 * @details 完全在用户态实现的 virtio-blk 驱动，作为独立 ELF 加载。
 *          通过系统调用访问硬件（SYS_VM_MAP 映射 MMIO/DMA，
 *          SYS_INTERRUPT_ATTACH 绑定中断，SYS_NOTIFICATION_WAIT 等待）。
 *
 *          本版本使用轮询模式（简化验证），完整中断驱动模式后续实现。
 *
 *          验证流程：
 *          1. 映射 virtio-mmio MMIO 区域（0x0A000000）
 *          2. 扫描 32 slot 发现 block 设备
 *          3. 分配 DMA 内存（virtqueue），查询物理地址
 *          4. Legacy 协议初始化设备
 *          5. 读扇区 0，通过 UART 打印数据验证
 *
 * @note 编译为独立 ELF，不链接进内核镜像
 * @note 加载地址 0x400000（TTBR0 用户空间）
 */

#include <stdint.h>
#include <stddef.h>

/* ========================================================================
 * 系统调用号（与内核 include/kernel/syscall.h 一致）
 * ======================================================================== */

#define SYS_DEBUG_PRINT       0x0500U
#define SYS_VM_MAP            0x0202U
#define SYS_VIRT_TO_PHYS      0x0205U
#define SYS_NOTIFICATION_WAIT 0x0109U
#define SYS_INTERRUPT_ATTACH  0x0400U

/* ========================================================================
 * 系统调用封装
 * ======================================================================== */

static inline long svc1(unsigned long sysno, unsigned long a0)
{
    register long x0 __asm__("x0") = (long)a0;
    register long x8 __asm__("x8") = (long)sysno;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return x0;
}

static inline long svc2(unsigned long sysno, unsigned long a0, unsigned long a1)
{
    register long x0 __asm__("x0") = (long)a0;
    register long x1 __asm__("x1") = (long)a1;
    register long x8 __asm__("x8") = (long)sysno;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x8) : "memory");
    return x0;
}

static inline long svc3(unsigned long sysno, unsigned long a0,
                         unsigned long a1, unsigned long a2)
{
    register long x0 __asm__("x0") = (long)a0;
    register long x1 __asm__("x1") = (long)a1;
    register long x2 __asm__("x2") = (long)a2;
    register long x8 __asm__("x8") = (long)sysno;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
    return x0;
}

/* ========================================================================
 * UART 输出（通过内核 SYS_DEBUG_PRINT）
 * ======================================================================== */

static void uart_puts(const char *s)
{
    /* 计算字符串长度 */
    unsigned long len = 0U;
    while (s[len] != '\0')
    {
        len++;
    }
    (void)svc3(SYS_DEBUG_PRINT, (unsigned long)s, len, 0U);
}

/* 简单十六进制输出 */
static void uart_hex64(uint64_t val)
{
    static char buf[17];
    int i;
    for (i = 15; i >= 0; i--)
    {
        uint8_t nibble = (uint8_t)((val >> ((15 - i) * 4)) & 0xFU);
        buf[i] = (char)((nibble < 10U) ? ('0' + nibble) : ('a' + nibble - 10U));
    }
    buf[16] = '\0';
    uart_puts(buf);
}

/* ========================================================================
 * virtio-mmio 寄存器偏移（Legacy 模式）
 * ======================================================================== */

#define VIRTIO_MMIO_MAGIC_VALUE       0x000U
#define VIRTIO_MMIO_DEVICE_ID         0x008U
#define VIRTIO_MMIO_GUEST_FEATURES    0x020U
#define VIRTIO_MMIO_GUEST_PAGE_SIZE   0x028U
#define VIRTIO_MMIO_QUEUE_SEL         0x030U
#define VIRTIO_MMIO_QUEUE_NUM_MAX     0x034U
#define VIRTIO_MMIO_QUEUE_NUM         0x038U
#define VIRTIO_MMIO_QUEUE_ALIGN       0x03CU
#define VIRTIO_MMIO_QUEUE_PFN         0x040U
#define VIRTIO_MMIO_QUEUE_NOTIFY      0x050U
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
#define VIRTIO_MMIO_BASE              0x0A000000ULL
#define VIRTIO_MMIO_SLOT_SIZE         0x200ULL
#define VIRTIO_MMIO_SLOT_COUNT        32U

/* PAGE_PERM 定义（与内核 page_table.h 一致） */
#define PAGE_PERM_READ          (1U << 0)
#define PAGE_PERM_WRITE         (1U << 1)
#define PAGE_PERM_DEVICE        (1U << 4)
#define PAGE_PERM_DEVICE_RW     (PAGE_PERM_READ | PAGE_PERM_WRITE | PAGE_PERM_DEVICE)

/* ========================================================================
 * virtqueue 数据结构
 * ======================================================================== */

#define QUEUE_SIZE 3U

typedef struct
{
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) vq_desc_t;

typedef struct
{
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed)) blk_req_t;

/* ========================================================================
 * MMIO 读写（通过映射的虚拟地址）
 * ======================================================================== */

static volatile uint32_t *g_mmio = NULL;

static uint32_t mmio_read32(uint32_t offset)
{
    return g_mmio[offset / 4U];
}

static void mmio_write32(uint32_t offset, uint32_t value)
{
    g_mmio[offset / 4U] = value;
}

/* ========================================================================
 * DMA 内存（通过 SYS_VM_MAP 分配）
 * ======================================================================== */

/* virtqueue 内存指针（映射后） */
static vq_desc_t *g_desc = NULL;        /* 描述符表 */
static uint16_t *g_avail_idx = NULL;     /* 可用环 idx */
static uint16_t *g_avail_ring = NULL;    /* 可用环 entries */
static uint16_t *g_used_idx = NULL;      /* 使用环 idx */
static uint32_t *g_used_ring = NULL;     /* 使用环 entries (id+len) */

static blk_req_t *g_req = NULL;          /* 请求头 */
static uint8_t *g_data_buf = NULL;       /* 数据缓冲 */
static uint8_t *g_resp_status = NULL;    /* 响应状态 */

/* ========================================================================
 * 用户态 virtio-blk 驱动实现
 * ======================================================================== */

/**
 * @brief 分配并映射 DMA 内存
 *
 * @param size 需要的字节数
 * @return 用户虚拟地址，0=失败
 */
static uint64_t dma_alloc(uint64_t size)
{
    /* SYS_VM_MAP: x0=paddr(0=DMA分配), x1=size, x2=perm_flags */
    return (uint64_t)svc3(SYS_VM_MAP, 0U, size, (unsigned long)PAGE_PERM_DEVICE_RW);
}

/**
 * @brief 查询虚拟地址对应的物理地址
 */
static uint64_t virt_to_phys(uint64_t vaddr)
{
    return (uint64_t)svc1(SYS_VIRT_TO_PHYS, (unsigned long)vaddr);
}

/**
 * @brief 初始化 virtio-blk 设备（完全用户态）
 *
 * @return 0 成功，-1 失败
 */
static int32_t virtio_blk_init(void)
{
    uint32_t slot;
    uint64_t device_base = 0ULL;
    uint32_t qnum_max;

    /* 映射 MMIO 区域（0x0A000000 - 0x0A004000，32 slot * 0x200 = 0x4000） */
    g_mmio = (volatile uint32_t *)(unsigned long)
             svc3(SYS_VM_MAP, VIRTIO_MMIO_BASE, 0x4000ULL,
                  (unsigned long)PAGE_PERM_DEVICE_RW);
    if (g_mmio == NULL)
    {
        uart_puts("[drv] MMIO map FAIL\n");
        return -1;
    }
    uart_puts("[drv] MMIO mapped\n");

    /* 扫描 slot 找 block 设备 */
    for (slot = 0U; slot < VIRTIO_MMIO_SLOT_COUNT; slot++)
    {
        uint64_t base = (uint64_t)slot * (VIRTIO_MMIO_SLOT_SIZE / 4U);
        uint32_t magic = g_mmio[base + (VIRTIO_MMIO_MAGIC_VALUE / 4U)];
        uint32_t devid = g_mmio[base + (VIRTIO_MMIO_DEVICE_ID / 4U)];

        if ((magic == VIRTIO_MAGIC) && (devid == 2U))
        {
            device_base = base;
            uart_puts("[drv] Found block at slot ");
            uart_hex64((uint64_t)slot);
            uart_puts("\n");
            break;
        }
    }

    if (device_base == 0ULL)
    {
        uart_puts("[drv] No block device\n");
        return -1;
    }

    /* 设置 MMIO 读写基址为设备 slot */
    g_mmio = (volatile uint32_t *)((uint64_t)g_mmio + device_base * 4U);

    /* Legacy 协议初始化 */
    mmio_write32(VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096U);
    mmio_write32(VIRTIO_MMIO_STATUS, 0U);
    mmio_write32(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    mmio_write32(VIRTIO_MMIO_STATUS,
                 VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    mmio_write32(VIRTIO_MMIO_GUEST_FEATURES, 0U);
    mmio_write32(VIRTIO_MMIO_STATUS,
                 VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER
                 | VIRTIO_STATUS_FEATURES_OK);

    /* 队列 0 */
    mmio_write32(VIRTIO_MMIO_QUEUE_SEL, 0U);
    qnum_max = mmio_read32(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (qnum_max == 0U)
    {
        uart_puts("[drv] No queue\n");
        return -1;
    }

    mmio_write32(VIRTIO_MMIO_QUEUE_NUM, QUEUE_SIZE);

    /* 分配 virtqueue DMA 内存（8KB，含 desc+avail+used） */
    {
        uint64_t vq_vaddr = dma_alloc(8192ULL);
        uint64_t vq_paddr;
        uint8_t *base;

        if (vq_vaddr == 0ULL)
        {
            uart_puts("[drv] vq alloc FAIL\n");
            return -1;
        }
        vq_paddr = virt_to_phys(vq_vaddr);
        uart_puts("[drv] vq vaddr=0x");
        uart_hex64(vq_vaddr);
        uart_puts(" paddr=0x");
        uart_hex64(vq_paddr);
        uart_puts("\n");

        base = (uint8_t *)(unsigned long)vq_vaddr;

        /* 清零 virtqueue */
        {
            uint32_t i;
            for (i = 0U; i < 8192U; i++)
            {
                base[i] = 0U;
            }
        }

        /* 布局：desc@0, avail@desc_size, used@4K */
        g_desc = (vq_desc_t *)(void *)base;
        g_avail_idx = (uint16_t *)(void *)(base + QUEUE_SIZE * 16U + 2U);
        g_avail_ring = g_avail_idx + 1;
        g_used_idx = (uint16_t *)(void *)(base + 4096U + 2U);
        g_used_ring = (uint32_t *)(void *)(g_used_idx + 1);

        /* 分配 req/data/resp */
        g_req = (blk_req_t *)(unsigned long)dma_alloc(64ULL);
        g_data_buf = (uint8_t *)(unsigned long)dma_alloc(512ULL);
        g_resp_status = (uint8_t *)(unsigned long)dma_alloc(64ULL);

        if ((g_req == NULL) || (g_data_buf == NULL) || (g_resp_status == NULL))
        {
            uart_puts("[drv] req/data alloc FAIL\n");
            return -1;
        }

        /* Legacy QUEUE_PFN（物理地址 >> 12） */
        mmio_write32(VIRTIO_MMIO_QUEUE_ALIGN, 4096U);
        mmio_write32(VIRTIO_MMIO_QUEUE_PFN, (uint32_t)(vq_paddr >> 12U));
    }

    /* DRIVER_OK */
    mmio_write32(VIRTIO_MMIO_STATUS,
                 VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER
                 | VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    uart_puts("[drv] Init OK\n");
    return 0;
}

/**
 * @brief 读一个扇区（轮询模式）
 */
static int32_t virtio_blk_read_sector(uint64_t sector, uint8_t *buf)
{
    uint16_t avail_idx;
    uint16_t used_idx;
    uint32_t timeout;
    uint64_t req_phys;
    uint64_t data_phys;
    uint64_t resp_phys;

    if ((g_desc == NULL) || (buf == NULL))
    {
        return -1;
    }

    /* 构建请求 */
    g_req->type = VIRTIO_BLK_T_IN;
    g_req->reserved = 0U;
    g_req->sector = sector;
    *g_resp_status = 0xFFU;

    /* 查询 DMA 缓冲物理地址（设备需要物理地址） */
    req_phys = virt_to_phys((uint64_t)(unsigned long)g_req);
    data_phys = virt_to_phys((uint64_t)(unsigned long)g_data_buf);
    resp_phys = virt_to_phys((uint64_t)(unsigned long)g_resp_status);

    /* 描述符链：[0]req → [1]data(write) → [2]resp(write) */
    g_desc[0].addr = req_phys;
    g_desc[0].len = (uint32_t)sizeof(blk_req_t);
    g_desc[0].flags = VIRTQ_DESC_F_NEXT;
    g_desc[0].next = 1U;

    g_desc[1].addr = data_phys;
    g_desc[1].len = 512U;
    g_desc[1].flags = VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE;
    g_desc[1].next = 2U;

    g_desc[2].addr = resp_phys;
    g_desc[2].len = 1U;
    g_desc[2].flags = VIRTQ_DESC_F_WRITE;
    g_desc[2].next = 0U;

    /* 提交到可用环 */
    avail_idx = *g_avail_idx;
    g_avail_ring[avail_idx % QUEUE_SIZE] = 0U;
    *g_avail_idx = (uint16_t)(avail_idx + 1U);

    __asm__ volatile("dsb sy" ::: "memory");

    /* 通知设备 */
    mmio_write32(VIRTIO_MMIO_QUEUE_NOTIFY, 0U);

    /* 轮询 used ring */
    used_idx = *g_used_idx;
    timeout = 10000000U;
    while ((*g_used_idx == used_idx) && (timeout > 0U))
    {
        __asm__ volatile("dsb sy" ::: "memory");
        timeout--;
    }

    if (timeout == 0U)
    {
        uart_puts("[drv] Read TIMEOUT\n");
        return -1;
    }

    if (*g_resp_status != VIRTIO_BLK_S_OK)
    {
        uart_puts("[drv] Read status FAIL\n");
        return -1;
    }

    /* 拷贝数据 */
    {
        uint32_t i;
        for (i = 0U; i < 512U; i++)
        {
            buf[i] = g_data_buf[i];
        }
    }

    return 0;
}

/* ========================================================================
 * 入口函数
 * ======================================================================== */

/**
 * @brief 用户态 virtio-blk 驱动入口
 *
 * @details 作为独立 ELF 被 elf_load_and_run 加载执行。
 *          初始化设备后读扇区 0 验证。
 */
void _start(void)
{
    static uint8_t read_buf[512];

    uart_puts("\n[drv] === User-space virtio-blk driver ===\n");

    /* 初始化设备 */
    if (virtio_blk_init() != 0)
    {
        uart_puts("[drv] Init FAILED\n");
        for (;;)
        {
            __asm__ volatile("wfe" ::: "memory");
        }
    }

    /* 读扇区 0 验证 */
    uart_puts("[drv] Reading sector 0...\n");
    if (virtio_blk_read_sector(0ULL, read_buf) == 0)
    {
        uart_puts("[drv] Read OK [0]=0x");
        uart_hex64((uint64_t)read_buf[0U]);
        uart_puts(" [1]=0x");
        uart_hex64((uint64_t)read_buf[1U]);
        uart_puts(" [2]=0x");
        uart_hex64((uint64_t)read_buf[2U]);
        uart_puts("\n[drv] *** USER-SPACE DRIVER WORKING ***\n");
    }
    else
    {
        uart_puts("[drv] Read FAILED\n");
    }

    /* 驱动服务循环（保持运行） */
    for (;;)
    {
        __asm__ volatile("wfe" ::: "memory");
    }
}
