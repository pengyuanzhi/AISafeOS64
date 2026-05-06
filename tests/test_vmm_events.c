/**
 * @file    test_vmm_events.c
 * @brief   VM 事件管理单元测试
 * @author  AISafe64 Team
 * @date    2026-05-03
 * @version 1.0
 *
 * @details 测试 VM 事件管理的以下功能：
 *          - 事件管理器初始化/销毁
 *          - 事件创建/销毁
 *          - 事件添加/移除
 *          - 事件等待/通知
 *          - 事件回调
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
#include "vmm_events.h"
#include "vmm.h"
#include "vm.h"

/* ========================================================================
 * 测试常量
 * ======================================================================== */

#define TEST_CAPACITY (4U)
#define TEST_VM_ID    (0U)
#define TEST_VCPU_ID  (0U)

/* ========================================================================
 * 测试夹具
 * ======================================================================== */

void setUp(void)
{
    vmm_events_init(TEST_CAPACITY);
}

void tearDown(void)
{
    vmm_events_destroy();
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 测试事件管理器初始化
 */
void test_vmm_events_init(void)
{
    kernel_status_t ret;

    /* 销毁事件管理器 */
    vmm_events_destroy();

    /* 初始化事件管理器 */
    ret = vmm_events_init(TEST_CAPACITY);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
}

/**
 * @brief 测试事件管理器销毁
 */
void test_vmm_events_destroy(void)
{
    kernel_status_t ret;

    /* 销毁事件管理器 */
    ret = vmm_events_destroy();
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
}

/**
 * @brief 测试事件创建
 */
void test_vmm_events_create(void)
{
    kernel_status_t ret;
    vmm_event_desc_t *event;
    vmm_event_type_t type = VMM_EVENT_VM_CREATED;

    /* 创建事件 */
    ret = vmm_events_create(type, TEST_VM_ID, TEST_VCPU_ID, &event);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
    TEST_ASSERT_NOT_NULL(event);
    TEST_ASSERT_EQUAL_UINT32(type, event->type);
    TEST_ASSERT_EQUAL_UINT32(TEST_VM_ID, event->vm_id);
    TEST_ASSERT_EQUAL_UINT32(TEST_VCPU_ID, event->vcpu_id);

    /* 销毁事件 */
    ret = vmm_events_destroy_event(event);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
}

/**
 * @brief 测试事件创建（无效类型）
 */
void test_vmm_events_create_invalid_type(void)
{
    kernel_status_t ret;
    vmm_event_desc_t *event;

    /* 创建事件（无效类型） */
    ret = vmm_events_create(VMM_EVENT_MAX, TEST_VM_ID, TEST_VCPU_ID, &event);
    TEST_ASSERT_LESS_THAN_INT32(0, ret);
    TEST_ASSERT_NULL(event);
}

/**
 * @brief 测试事件创建（队列已满）
 */
void test_vmm_events_create_full(void)
{
    kernel_status_t ret;
    vmm_event_desc_t *events[5];
    vmm_event_type_t type = VMM_EVENT_VM_CREATED;

    /* 创建 5 个事件（超过容量 4） */
    for (int i = 0; i < 5; i++)
    {
        ret = vmm_events_create(type, TEST_VM_ID, TEST_VCPU_ID, &events[i]);
        if (i < TEST_CAPACITY)
        {
            TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
        }
        else
        {
            TEST_ASSERT_LESS_THAN_INT32(0, ret);
        }
    }
}

/**
 * @brief 测试事件添加
 */
void test_vmm_events_add(void)
{
    kernel_status_t ret;
    vmm_event_desc_t *event;

    /* 创建事件 */
    ret = vmm_events_create(VMM_EVENT_VM_CREATED, TEST_VM_ID, TEST_VCPU_ID, &event);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 添加事件 */
    ret = vmm_events_add(event);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
}

/**
 * @brief 测试事件添加（队列已满）
 */
void test_vmm_events_add_full(void)
{
    kernel_status_t ret;
    vmm_event_desc_t *events[TEST_CAPACITY];
    vmm_event_type_t type = VMM_EVENT_VM_CREATED;

    /* 创建 4 个事件 */
    for (int i = 0; i < TEST_CAPACITY; i++)
    {
        ret = vmm_events_create(type, TEST_VM_ID, TEST_VCPU_ID, &events[i]);
        TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
    }

    /* 尝试添加第 5 个事件（应该失败） */
    ret = vmm_events_add(events[0]);
    TEST_ASSERT_LESS_THAN_INT32(0, ret);
}

/**
 * @brief 测试事件移除
 */
void test_vmm_events_remove(void)
{
    kernel_status_t ret;
    vmm_event_desc_t *event;
    vmm_event_type_t type = VMM_EVENT_VM_CREATED;

    /* 创建并添加事件 */
    ret = vmm_events_create(type, TEST_VM_ID, TEST_VCPU_ID, &event);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    ret = vmm_events_add(event);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 移除事件 */
    ret = vmm_events_remove(&event);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 检查事件是否已清空 */
    TEST_ASSERT_EQUAL_UINT32(VMM_EVENT_MAX, event->type);
}

/**
 * @brief 测试事件移除（队列为空）
 */
void test_vmm_events_remove_empty(void)
{
    kernel_status_t ret;
    vmm_event_desc_t *event;

    /* 尝试从空队列移除事件 */
    ret = vmm_events_remove(&event);
    TEST_ASSERT_LESS_THAN_INT32(0, ret);
    TEST_ASSERT_NULL(event);
}

/**
 * @brief 测试事件通知
 */
void test_vmm_events_notify(void)
{
    kernel_status_t ret;
    vmm_event_desc_t *event;
    vmm_event_type_t type = VMM_EVENT_VM_CREATED;
    uint32_t event_count = 0;

    /* 创建并添加事件 */
    ret = vmm_events_create(type, TEST_VM_ID, TEST_VCPU_ID, &event);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    ret = vmm_events_add(event);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 通知事件 */
    ret = vmm_events_notify(event);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
}

/**
 * @brief 测试事件回调
 */
void test_vmm_events_callback(void)
{
    kernel_status_t ret;
    vmm_event_desc_t *event;
    vmm_event_type_t type = VMM_EVENT_VM_CREATED;

    /* 注册回调 */
    ret = vmm_events_register_callback(
        [](vmm_event_desc_t *e) { event_count++; }
    );
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 创建并添加事件 */
    ret = vmm_events_create(type, TEST_VM_ID, TEST_VCPU_ID, &event);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    ret = vmm_events_add(event);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 通知事件（应该触发回调） */
    ret = vmm_events_notify(event);
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 检查回调是否被调用 */
    TEST_ASSERT_GREATER_THAN_UINT32(0, event_count);
}

/**
 * @brief 测试清空所有事件
 */
void test_vmm_events_clear(void)
{
    kernel_status_t ret;
    vmm_event_desc_t *events[4];
    vmm_event_type_t type = VMM_EVENT_VM_CREATED;

    /* 创建 4 个事件 */
    for (int i = 0; i < 4; i++)
    {
        ret = vmm_events_create(type, TEST_VM_ID, TEST_VCPU_ID, &events[i]);
        TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

        ret = vmm_events_add(events[i]);
        TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
    }

    /* 清空所有事件 */
    ret = vmm_events_clear();
    TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

    /* 检查队列是否为空 */
    TEST_ASSERT_TRUE(vmm_events_is_empty());
}

/**
 * @brief 测试多个事件类型
 */
void test_vmm_events_multiple_types(void)
{
    kernel_status_t ret;
    vmm_event_desc_t *events[5];
    vmm_event_type_t types[] = {
        VMM_EVENT_VM_CREATED,
        VMM_EVENT_VM_DESTROYED,
        VMM_EVENT_VCPU_CREATED,
        VMM_EVENT_VCPU_DESTROYED,
        VMM_EVENT_EXIT
    };

    /* 创建并添加多个事件 */
    for (int i = 0; i < 5; i++)
    {
        ret = vmm_events_create(types[i], TEST_VM_ID, TEST_VCPU_ID, &events[i]);
        TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);

        ret = vmm_events_add(events[i]);
        TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
    }

    /* 移除并验证事件类型 */
    for (int i = 0; i < 5; i++)
    {
        vmm_event_desc_t *event;
        ret = vmm_events_remove(&event);
        TEST_ASSERT_EQUAL_INT32(KERNEL_OK, ret);
        TEST_ASSERT_EQUAL_UINT32(types[i], event->type);
    }
}

/* ========================================================================
 * 测试主函数
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_vmm_events_init);
    RUN_TEST(test_vmm_events_destroy);
    RUN_TEST(test_vmm_events_create);
    RUN_TEST(test_vmm_events_create_invalid_type);
    RUN_TEST(test_vmm_events_create_full);
    RUN_TEST(test_vmm_events_add);
    RUN_TEST(test_vmm_events_add_full);
    RUN_TEST(test_vmm_events_remove);
    RUN_TEST(test_vmm_events_remove_empty);
    RUN_TEST(test_vmm_events_notify);
    RUN_TEST(test_vmm_events_callback);
    RUN_TEST(test_vmm_events_clear);
    RUN_TEST(test_vmm_events_multiple_types);

    return UNITY_END();
}
