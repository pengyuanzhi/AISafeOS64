/**
 * @file    test_virtio_net.c
 * @brief   VirtIO Net 驱动测试程序
 * @author  AISafe64 Team
 * @date    2026-04-18
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* VirtIO MMIO 寄存器定义 */
#define VIRTIO_MMIO_MAGIC_VALUE    0x74726976U
#define VIRTIO_MMIO_VERSION        2U
#define VIRTIO_MMIO_DEVICE_ID      0x0008U
#define VIRTIO_MMIO_CONFIG         0x0100U

/* VirtIO 设备 ID */
#define VIRTIO_NET_DEVICE_ID       1U
#define VIRTIO_BLK_DEVICE_ID       2U

/**
 * @brief MMIO 读取 32 位值
 */
static uint32_t mmio_read32(volatile uint32_t *base, uint32_t offset)
{
    return base[offset / 4U];
}

/**
 * @brief 扫描 VirtIO Net 设备
 */
int main(void)
{
    uint32_t slot;

    printf("========================================\n");
    printf("VirtIO Net 设备扫描测试\n");
    printf("========================================\n");
    printf("\n");

    /* 扫描所有 32 个 virtio-mmio slot */
    for (slot = 0U; slot < 32U; slot++)
    {
        volatile uint32_t *base;
        uint32_t magic;
        uint32_t version;
        uint32_t device_id;

        base = (volatile uint32_t *)(void *)(0x0A000000ULL + ((uint64_t)slot * 0x200ULL));

        magic = mmio_read32(base, 0x0000U);
        version = mmio_read32(base, 0x0004U);
        device_id = mmio_read32(base, VIRTIO_MMIO_DEVICE_ID);

        if (magic == VIRTIO_MMIO_MAGIC_VALUE)
        {
            printf("[Slot %2u] Magic=0x%08X Version=%u DeviceID=0x%02X",
                   slot, magic, version, device_id);

            if (device_id == VIRTIO_NET_DEVICE_ID)
            {
                printf(" <--- VirtIO Net 设备\n");
            }
            else if (device_id == VIRTIO_BLK_DEVICE_ID)
            {
                printf(" <--- VirtIO Block 设备\n");
            }
            else
            {
                printf("\n");
            }
        }
    }

    printf("\n");
    printf("========================================\n");
    printf("测试完成\n");
    printf("========================================\n");

    return 0;
}
