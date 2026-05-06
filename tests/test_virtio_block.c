/**
 * @file    test_virtio_block.c
 * @brief   VirtIO-Block 块设备单元测试
 * @author  AISafe64 Team
 * @date    2026-05-03
 * @version 1.0
 *
 * @details 测试 VirtIO-Block 块设备的功能：
 *          - 设备初始化/销毁
 *          - 块设备读写
 *          - 块设备刷新
 *          - 块设备请求处理
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <unity.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <kernel/types.h>
#include "virtio_block.h"

/* ========================================================================
 * 测试常量
 * ======================================================================== */

#define TEST_VM_ID             (0U)
#define TEST_IMAGE_SIZE        (512ULL * 256ULL)  /* 256 扇区 */
#define TEST_MMIO_BASE         (0x09000000ULL)

/* ========================================================================
 * 测试夹具
 * ======================================================================== */

static uint8_t *s_test_image;
static uint8_t s_test_buffer[4096];  /* 8 扇区 */

void setUp(void)
{
    s_test_image = NULL;
    (void)memset(s_test_buffer, 0, sizeof(s_test_buffer));
}

void tearDown(void)
{
    if (s_test_image != NULL)
    {
        (void)virtio_blk_destroy(0);
        kfree(s_test_image);
        s_test_image = NULL;
    }
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

void test_virtio_blk_init_success(void)
{
    int32_t dev_id;

    s_test_image = (uint8_t *)kmalloc(TEST_IMAGE_SIZE);
    TEST_ASSERT_NOT_NULL(s_test_image);
    (void)memset(s_test_image, 0xAA, (size_t)TEST_IMAGE_SIZE);

    dev_id = virtio_blk_init(TEST_VM_ID, "test-blk",
                             s_test_image, TEST_IMAGE_SIZE,
                             TEST_MMIO_BASE);
    TEST_ASSERT_GREATER_THAN_INT32(0, dev_id);
}

void test_virtio_blk_init_invalid_vm_id(void)
{
    int32_t dev_id;

    s_test_image = (uint8_t *)kmalloc(TEST_IMAGE_SIZE);
    TEST_ASSERT_NOT_NULL(s_test_image);

    dev_id = virtio_blk_init(0xFFFFFFFFU, "test-blk",
                             s_test_image, TEST_IMAGE_SIZE,
                             TEST_MMIO_BASE);
    TEST_ASSERT_LESS_THAN_INT32(0, dev_id);
}

void test_virtio_blk_destroy_success(void)
{
    int32_t dev_id;
    kernel_status_t ret;

    s_test_image = (uint8_t *)kmalloc(TEST_IMAGE_SIZE);
    TEST_ASSERT_NOT_NULL(s_test_image);
    (void)memset(s_test_image, 0xAA, (size_t)TEST_IMAGE_SIZE);

    dev_id = virtio_blk_init(TEST_VM_ID, "test-blk",
                             s_test_image, TEST_IMAGE_SIZE,
                             TEST_MMIO_BASE);
    TEST_ASSERT_GREATER_THAN_INT32(0, dev_id);

    ret = virtio_blk_destroy((uint32_t)dev_id);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
}

void test_virtio_blk_read_success(void)
{
    int32_t dev_id;
    int64_t bytes;

    s_test_image = (uint8_t *)kmalloc(TEST_IMAGE_SIZE);
    TEST_ASSERT_NOT_NULL(s_test_image);
    (void)memset(s_test_image, 0xAA, (size_t)TEST_IMAGE_SIZE);

    dev_id = virtio_blk_init(TEST_VM_ID, "test-blk",
                             s_test_image, TEST_IMAGE_SIZE,
                             TEST_MMIO_BASE);
    TEST_ASSERT_GREATER_THAN_INT32(0, dev_id);

    bytes = virtio_blk_read((uint32_t)dev_id, 10ULL,
                             s_test_buffer, 8U);
    TEST_ASSERT_EQUAL_INT64(4096, bytes);
}

void test_virtio_blk_read_invalid_sector(void)
{
    int32_t dev_id;
    int64_t bytes;

    s_test_image = (uint8_t *)kmalloc(TEST_IMAGE_SIZE);
    TEST_ASSERT_NOT_NULL(s_test_image);
    (void)memset(s_test_image, 0xAA, (size_t)TEST_IMAGE_SIZE);

    dev_id = virtio_blk_init(TEST_VM_ID, "test-blk",
                             s_test_image, TEST_IMAGE_SIZE,
                             TEST_MMIO_BASE);
    TEST_ASSERT_GREATER_THAN_INT32(0, dev_id);

    bytes = virtio_blk_read((uint32_t)dev_id, 300ULL,
                             s_test_buffer, 1U);
    TEST_ASSERT_LESS_THAN_INT32(0, bytes);
}

void test_virtio_blk_write_success(void)
{
    int32_t dev_id;
    int64_t bytes;

    s_test_image = (uint8_t *)kmalloc(TEST_IMAGE_SIZE);
    TEST_ASSERT_NOT_NULL(s_test_image);
    (void)memset(s_test_image, 0xAA, (size_t)TEST_IMAGE_SIZE);

    dev_id = virtio_blk_init(TEST_VM_ID, "test-blk",
                             s_test_image, TEST_IMAGE_SIZE,
                             TEST_MMIO_BASE);
    TEST_ASSERT_GREATER_THAN_INT32(0, dev_id);

    (void)memset(s_test_buffer, 0x55, sizeof(s_test_buffer));
    bytes = virtio_blk_write((uint32_t)dev_id, 20ULL,
                              s_test_buffer, 8U);
    TEST_ASSERT_EQUAL_INT64(4096, bytes);
}

void test_virtio_blk_flush(void)
{
    int32_t dev_id;
    kernel_status_t ret;

    s_test_image = (uint8_t *)kmalloc(TEST_IMAGE_SIZE);
    TEST_ASSERT_NOT_NULL(s_test_image);
    (void)memset(s_test_image, 0xAA, (size_t)TEST_IMAGE_SIZE);

    dev_id = virtio_blk_init(TEST_VM_ID, "test-blk",
                             s_test_image, TEST_IMAGE_SIZE,
                             TEST_MMIO_BASE);
    TEST_ASSERT_GREATER_THAN_INT32(0, dev_id);

    ret = virtio_blk_flush((uint32_t)dev_id);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
}

void test_virtio_blk_handle_req_in(void)
{
    int32_t dev_id;
    kernel_status_t ret;
    virtio_blk_req_t req;

    s_test_image = (uint8_t *)kmalloc(TEST_IMAGE_SIZE);
    TEST_ASSERT_NOT_NULL(s_test_image);
    (void)memset(s_test_image, 0xAA, (size_t)TEST_IMAGE_SIZE);

    dev_id = virtio_blk_init(TEST_VM_ID, "test-blk",
                             s_test_image, TEST_IMAGE_SIZE,
                             TEST_MMIO_BASE);
    TEST_ASSERT_GREATER_THAN_INT32(0, dev_id);

    req.hdr.type = VIRTIO_BLK_T_IN;
    req.hdr.sector = 10ULL;
    req.data.addr = (void *)s_test_buffer;
    req.data.len = 4096U;
    req.status.status = VIRTIO_BLK_S_IOERR;

    ret = virtio_blk_handle_req((uint32_t)dev_id, &req);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_UINT8(VIRTIO_BLK_S_OK, req.status.status);
}

void test_virtio_blk_handle_req_out(void)
{
    int32_t dev_id;
    kernel_status_t ret;
    virtio_blk_req_t req;

    s_test_image = (uint8_t *)kmalloc(TEST_IMAGE_SIZE);
    TEST_ASSERT_NOT_NULL(s_test_image);
    (void)memset(s_test_image, 0xAA, (size_t)TEST_IMAGE_SIZE);

    dev_id = virtio_blk_init(TEST_VM_ID, "test-blk",
                             s_test_image, TEST_IMAGE_SIZE,
                             TEST_MMIO_BASE);
    TEST_ASSERT_GREATER_THAN_INT32(0, dev_id);

    (void)memset(s_test_buffer, 0x55, sizeof(s_test_buffer));
    req.hdr.type = VIRTIO_BLK_T_OUT;
    req.hdr.sector = 20ULL;
    req.data.addr = (void *)s_test_buffer;
    req.data.len = 4096U;
    req.status.status = VIRTIO_BLK_S_IOERR;

    ret = virtio_blk_handle_req((uint32_t)dev_id, &req);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL_UINT8(VIRTIO_BLK_S_OK, req.status.status);
}

void test_virtio_blk_inject_irq(void)
{
    int32_t dev_id;
    kernel_status_t ret;

    s_test_image = (uint8_t *)kmalloc(TEST_IMAGE_SIZE);
    TEST_ASSERT_NOT_NULL(s_test_image);
    (void)memset(s_test_image, 0xAA, (size_t)TEST_IMAGE_SIZE);

    dev_id = virtio_blk_init(TEST_VM_ID, "test-blk",
                             s_test_image, TEST_IMAGE_SIZE,
                             TEST_MMIO_BASE);
    TEST_ASSERT_GREATER_THAN_INT32(0, dev_id);

    ret = virtio_blk_inject_irq((uint32_t)dev_id, 0U);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
}

/* ========================================================================
 * 测试主函数
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_virtio_blk_init_success);
    RUN_TEST(test_virtio_blk_init_invalid_vm_id);
    RUN_TEST(test_virtio_blk_destroy_success);
    RUN_TEST(test_virtio_blk_read_success);
    RUN_TEST(test_virtio_blk_read_invalid_sector);
    RUN_TEST(test_virtio_blk_write_success);
    RUN_TEST(test_virtio_blk_flush);
    RUN_TEST(test_virtio_blk_handle_req_in);
    RUN_TEST(test_virtio_blk_handle_req_out);
    RUN_TEST(test_virtio_blk_inject_irq);

    return UNITY_END();
}
