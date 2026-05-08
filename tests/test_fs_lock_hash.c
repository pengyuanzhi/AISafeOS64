/**
 * @file    test_fs_lock_hash.c
 * @brief   文件锁哈希索引测试
 * @author  AISafe64 Team
 * @date    2026-05-08
 * @version 1.0
 *
 * @details 测试文件锁哈希索引功能：
 *          - 锁管理器初始化
 *          - 文件加锁/解锁
 *          - 递归锁
 *          - 线程间锁冲突
 *          - 锁查找
 *          - 锁清空
 *
 * @note MISRA-C:2012 合规（简化版）
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* 导入接口 */
#include "services/fs/fs_lock_hash.h"

/**
 * @brief 性能测试
 */
int32_t main(void)
{
    fs_mount_lock_mgr_t mgr;
    fs_lock_hash_entry_t *lock;
    int32_t ret;
    uint32_t i;

    printf("=== 文件锁哈希索引测试 ===\n\n");

    /* 测试1：锁管理器初始化 */
    printf("--- 测试1：锁管理器初始化 ---\n");
    ret = fs_mount_lock_mgr_init(&mgr);
    if (ret == 0 && mgr.initialized)
    {
        printf("✓ 锁管理器初始化成功\n");
        printf("  lock_count = %u\n", mgr.lock_count);
    }
    else
    {
        printf("✗ 锁管理器初始化失败\n");
        return 1;
    }

    /* 测试2：加锁 */
    printf("\n--- 测试2：加锁 ---\n");
    ret = fs_mount_lock_lock(&mgr, 1U, 100U, 1U, 1000U);
    if (ret == 0)
    {
        printf("✓ 加锁成功 (mount_id=1, ino=100, owner_tid=1000)\n");
        printf("  lock_count = %u\n", mgr.lock_count);
    }
    else
    {
        printf("✗ 加锁失败 (ret=%d)\n", ret);
        return 1;
    }

    /* 测试3：递归锁 */
    printf("\n--- 测试3：递归锁 ---\n");
    ret = fs_mount_lock_lock(&mgr, 1U, 100U, 1U, 1000U);
    if (ret == 0)
    {
        lock = fs_mount_lock_find(&mgr, 1U, 100U);
        if ((lock != NULL) && (lock->lock_count == 2U))
        {
            printf("✓ 递归锁成功 (lock_count=%u)\n", lock->lock_count);
        }
        else
        {
            printf("✗ 递归锁失败\n");
            return 1;
        }
    }
    else
    {
        printf("✗ 递归锁失败 (ret=%d)\n", ret);
        return 1;
    }

    /* 测试4：线程间锁冲突 */
    printf("\n--- 测试4：线程间锁冲突 ---\n");
    ret = fs_mount_lock_lock(&mgr, 1U, 100U, 1U, 2000U);
    if (ret == -2)
    {
        printf("✓ 线程间锁冲突检测正确\n");
        printf("  owner_tid=1000 持有锁，owner_tid=2000 尝试加锁被拒绝\n");
    }
    else
    {
        printf("✗ 线程间锁冲突检测失败 (ret=%d)\n", ret);
        return 1;
    }

    /* 测试5：解锁 */
    printf("\n--- 测试5：解锁 ---\n");
    ret = fs_mount_lock_unlock(&mgr, 1U, 100U, 1000U);
    if (ret == 0)
    {
        lock = fs_mount_lock_find(&mgr, 1U, 100U);
        if ((lock != NULL) && (lock->lock_count == 1U))
        {
            printf("✓ 解锁成功 (lock_count=%u)\n", lock->lock_count);
        }
        else
        {
            printf("✗ 解锁失败\n");
            return 1;
        }
    }
    else
    {
        printf("✗ 解锁失败 (ret=%d)\n", ret);
        return 1;
    }

    /* 测试6：完全解锁 */
    printf("\n--- 测试6：完全解锁 ---\n");
    ret = fs_mount_lock_unlock(&mgr, 1U, 100U, 1000U);
    if (ret == 0)
    {
        lock = fs_mount_lock_find(&mgr, 1U, 100U);
        if (lock == NULL)
        {
            printf("✓ 完全解锁成功\n");
            printf("  lock_count = %u\n", mgr.lock_count);
        }
        else
        {
            printf("✗ 完全解锁失败\n");
            return 1;
        }
    }
    else
    {
        printf("✗ 完全解锁失败 (ret=%d)\n", ret);
        return 1;
    }

    /* 测试7：多锁加锁 */
    printf("\n--- 测试7：多锁加锁 ---\n");
    for (i = 0U; i < 10U; i++)
    {
        ret = fs_mount_lock_lock(&mgr, 1U, 100U + i, 1U, 1000U);
        if (ret != 0)
        {
            printf("✗ 多锁加锁失败 (i=%u, ret=%d)\n", i, ret);
            return 1;
        }
    }
    printf("✓ 多锁加锁成功 (10 个锁)\n");
    printf("  lock_count = %u\n", mgr.lock_count);

    /* 测试8：锁查找 */
    printf("\n--- 测试8：锁查找 ---\n");
    lock = fs_mount_lock_find(&mgr, 1U, 105U);
    if ((lock != NULL) && lock->locked && 
        lock->mount_id == 1U && lock->ino == 105U)
    {
        printf("✓ 锁查找成功 (mount_id=1, ino=105)\n");
        printf("  owner_tid = %u\n", lock->owner_tid);
        printf("  lock_type = %u\n", lock->lock_type);
        printf("  lock_count = %u\n", lock->lock_count);
    }
    else
    {
        printf("✗ 锁查找失败\n");
        return 1;
    }

    /* 测试9：锁清空 */
    printf("\n--- 测试9：锁清空 ---\n");
    fs_mount_lock_clear(&mgr);
    if (mgr.lock_count == 0U)
    {
        printf("✓ 锁清空成功\n");
    }
    else
    {
        printf("✗ 锁清空失败 (lock_count=%u)\n", mgr.lock_count);
        return 1;
    }

    /* 测试10：NULL 参数处理 */
    printf("\n--- 测试10：NULL 参数处理 ---\n");
    ret = fs_mount_lock_mgr_init(NULL);
    if (ret != 0)
    {
        printf("✓ NULL 锁管理器处理正确\n");
    }
    else
    {
        printf("✗ NULL 锁管理器处理错误\n");
        return 1;
    }

    /* 测试11：边界测试 - 最大锁数 */
    printf("\n--- 测试11：边界测试 - 最大锁数 ---\n");
    for (i = 0U; i < 128U; i++)
    {
        ret = fs_mount_lock_lock(&mgr, 1U, 200U + i, 1U, 1000U);
        if (ret != 0)
        {
            printf("✗ 最大锁数测试失败 (i=%u, ret=%d)\n", i, ret);
            return 1;
        }
    }
    printf("✓ 最大锁数测试成功 (128 个锁)\n");

    /* 测试12：边界测试 - 超过最大锁数 */
    printf("\n--- 测试12：边界测试 - 超过最大锁数 ---\n");
    ret = fs_mount_lock_lock(&mgr, 1U, 999U, 1U, 1000U);
    if (ret == -3)
    {
        printf("✓ 超过最大锁数检测正确\n");
    }
    else
    {
        printf("✗ 超过最大锁数检测失败 (ret=%d)\n", ret);
        return 1;
    }

    /* 测试13：错误解锁 */
    printf("\n--- 测试13：错误解锁 ---\n");
    ret = fs_mount_lock_unlock(&mgr, 1U, 99999U, 1000U);
    if (ret == -3)
    {
        printf("✓ 未找到锁的解锁处理正确\n");
    }
    else
    {
        printf("✗ 未找到锁的解锁处理失败 (ret=%d)\n", ret);
        return 1;
    }

    /* 测试14：多个挂载点 */
    printf("\n--- 测试14：多个挂载点 ---\n");
    fs_mount_lock_clear(&mgr);
    ret = fs_mount_lock_lock(&mgr, 1U, 100U, 1U, 1000U);
    if (ret != 0)
    {
        printf("✗ 挂载点 1 加锁失败\n");
        return 1;
    }
    ret = fs_mount_lock_lock(&mgr, 2U, 100U, 1U, 2000U);
    if (ret == 0)
    {
        printf("✓ 多个挂载点加锁成功\n");
        printf("  lock_count = %u\n", mgr.lock_count);
    }
    else
    {
        printf("✗ 挂载点 2 加锁失败 (ret=%d)\n", ret);
        return 1;
    }

    printf("\n=== 测试完成：全部通过 ===\n");
    return 0;
}
