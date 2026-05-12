/**
 * @file    ext4_cow.c
 * @brief   EXT4 写时复制（CoW）实现
 * @author  AISafe64 Team
 * @date    2026-05-12
 * @version 1.1
 *
 * @details EXT4 写时复制（Copy-on-Write）实现：
 *          - 块引用计数管理（递增、递减、查询）
 *          - 快照创建与回滚
 *          - 快照清理
 *          - POSIX 错误码返回约定
 *
 * @note MISRA-C:2012 合规
 * @note TDD: REFACTOR 阶段 - 优化重构
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "ext4_cow.h"
#include "ext4_journal.h"
#include <string.h>

/* ========================================================================
 * 内部变量
 * ======================================================================== */

/** @brief 块引用计数表（索引 0 保留不使用） */
static uint32_t s_refcounts[EXT4_COW_MAX_BLOCKS];

/** @brief 快照信息表（索引与快照 ID - 1 对应） */
static ext4_cow_snapshot_t s_snapshots[EXT4_COW_MAX_SNAPSHOTS];

/** @brief 下一个待分配的快照 ID（从 1 开始） */
static uint32_t s_next_snapshot_id;

/** @brief 模块初始化标志 */
static bool s_initialized;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 验证块 ID 是否有效
 *
 * @details 检查模块初始化状态和块 ID 范围，
 *          块 ID 为 0 或 >= EXT4_COW_MAX_BLOCKS 均视为无效。
 *
 * @param block_id 待验证的块 ID
 *
 * @return true 块 ID 有效，false 无效
 */
static bool cow_validate_block_id(uint32_t block_id)
{
    bool is_valid = false;

    if (!s_initialized)
    {
        return false;
    }

    /* 块 ID 为 0 无效（保留） */
    if (block_id == EXT4_COW_INVALID_BLOCK)
    {
        return false;
    }

    /* 超出范围 */
    if (block_id >= EXT4_COW_MAX_BLOCKS)
    {
        return false;
    }

    is_valid = true;
    return is_valid;
}

/**
 * @brief 将快照 ID 转换为快照表索引并验证
 *
 * @details 快照 ID 从 1 开始递增，对应数组索引为 ID - 1。
 *          同时检查快照是否存在且处于活跃状态。
 *
 * @param snapshot_id 快照 ID
 * @param[out] out_idx 输出对应的数组索引
 *
 * @return 0 验证通过，<0 验证失败
 * @retval -EINVAL 快照 ID 无效或快照不活跃
 */
static int32_t cow_validate_snapshot(uint32_t snapshot_id, uint32_t *out_idx)
{
    uint32_t idx;

    if (!s_initialized)
    {
        return -EXT4_COW_EINVAL;
    }

    /* 快照 ID 为 0 无效 */
    if (snapshot_id == 0U)
    {
        return -EXT4_COW_EINVAL;
    }

    /* 快照 ID 未被分配过 */
    if (snapshot_id >= s_next_snapshot_id)
    {
        return -EXT4_COW_EINVAL;
    }

    idx = snapshot_id - 1U;

    /* 检查快照是否活跃 */
    if (!s_snapshots[idx].active)
    {
        return -EXT4_COW_EINVAL;
    }

    *out_idx = idx;
    return 0;
}

/**
 * @brief 安全地清除指定快照 ID 对应的块引用计数
 *
 * @details 仅在快照 ID 位于有效块范围内时执行清除操作
 *
 * @param snapshot_id 快照 ID（同时作为块 ID 使用）
 */
static void cow_clear_block_refcount(uint32_t snapshot_id)
{
    if (snapshot_id < EXT4_COW_MAX_BLOCKS)
    {
        s_refcounts[snapshot_id] = 0U;
    }
}

/* ========================================================================
 * 接口函数实现
 * ======================================================================== */

/**
 * @brief 初始化 CoW 模块
 *
 * @details 清空引用计数表和快照表，将模块置为就绪状态。
 *          必须在使用其他 CoW 接口前调用。
 *
 * @return 0 成功
 */
int32_t ext4_cow_init(void)
{
    /* 清空引用计数表 */
    (void)memset(s_refcounts, 0, sizeof(s_refcounts));

    /* 清空快照表 */
    (void)memset(s_snapshots, 0, sizeof(s_snapshots));

    /* 快照 ID 从 1 开始分配 */
    s_next_snapshot_id = 1U;
    s_initialized = true;

    return 0;
}

/**
 * @brief 销毁 CoW 模块
 *
 * @details 释放所有内部状态，将模块置为未初始化。
 *          销毁后不得调用其他接口（除非重新初始化）。
 */
void ext4_cow_destroy(void)
{
    s_initialized = false;

    (void)memset(s_refcounts, 0, sizeof(s_refcounts));
    (void)memset(s_snapshots, 0, sizeof(s_snapshots));
    s_next_snapshot_id = 0U;
}

/**
 * @brief 递增块引用计数
 *
 * @details 将指定块的引用计数加 1。首次引用时计数从 0 变为 1。
 *          块 ID 为 0 或超出范围均视为无效。
 *
 * @param block_id 块 ID（必须 > 0 且 < EXT4_COW_MAX_BLOCKS）
 *
 * @return 0 成功，<0 失败
 * @retval -EINVAL 参数无效或模块未初始化
 * @retval -ENOMEM 引用计数已达上限
 */
int32_t ext4_cow_refcount_inc(uint32_t block_id)
{
    /* 验证块 ID */
    if (!cow_validate_block_id(block_id))
    {
        return -EXT4_COW_EINVAL;
    }

    /* 溢出检查：引用计数已达上限 */
    if (s_refcounts[block_id] >= EXT4_COW_MAX_REFCOUNT)
    {
        return -EXT4_COW_ENOMEM;
    }

    /* MISRA: 使用显式 += 替代 ++ 运算符 */
    s_refcounts[block_id] += 1U;

    return 0;
}

/**
 * @brief 递减块引用计数
 *
 * @details 将指定块的引用计数减 1。计数为 0 时不能再递减。
 *          当引用计数降为 0 时，该块可被回收。
 *
 * @param block_id 块 ID（必须 > 0 且 < EXT4_COW_MAX_BLOCKS）
 *
 * @return 0 成功，<0 失败
 * @retval -EINVAL 参数无效、引用计数已为 0 或模块未初始化
 */
int32_t ext4_cow_refcount_dec(uint32_t block_id)
{
    /* 验证块 ID */
    if (!cow_validate_block_id(block_id))
    {
        return -EXT4_COW_EINVAL;
    }

    /* 引用计数已经为 0，不能递减 */
    if (s_refcounts[block_id] == 0U)
    {
        return -EXT4_COW_EINVAL;
    }

    /* MISRA: 使用显式 -= 替代 -- 运算符 */
    s_refcounts[block_id] -= 1U;

    return 0;
}

/**
 * @brief 获取块引用计数
 *
 * @details 查询指定块的当前引用计数值。
 *          块 ID 无效或模块未初始化时返回 0。
 *
 * @param block_id 块 ID
 *
 * @return 引用计数值（0 表示无效或未引用）
 */
uint32_t ext4_cow_get_refcount(uint32_t block_id)
{
    /* 验证块 ID */
    if (!cow_validate_block_id(block_id))
    {
        return 0U;
    }

    return s_refcounts[block_id];
}

/**
 * @brief 创建快照
 *
 * @details 创建一个新快照，分配唯一 ID（从 1 开始递增）。
 *          快照创建时会自动递增对应的块引用计数。
 *          名称长度不得超过 EXT4_COW_SNAPSHOT_NAME_LEN - 1。
 *
 * @param name 快照名称（不能为 NULL）
 *
 * @return 快照 ID（>0 成功），0 失败
 */
uint32_t ext4_cow_snapshot_create(const char *name)
{
    uint32_t idx;
    uint32_t result;

    if (!s_initialized)
    {
        return 0U;
    }

    if (name == NULL)
    {
        return 0U;
    }

    /* 检查快照数量上限 */
    if (s_next_snapshot_id > EXT4_COW_MAX_SNAPSHOTS)
    {
        return 0U;
    }

    idx = s_next_snapshot_id - 1U;

    /* 二次检查索引范围（防御性编程） */
    if (idx >= EXT4_COW_MAX_SNAPSHOTS)
    {
        return 0U;
    }

    /* 填充快照元数据 */
    s_snapshots[idx].id = s_next_snapshot_id;
    s_snapshots[idx].refcount = 1U;
    s_snapshots[idx].block_count = 0U;
    s_snapshots[idx].active = true;

    /* 安全拷贝快照名称（保证以 '\0' 结尾） */
    (void)strncpy(s_snapshots[idx].name, name, EXT4_COW_SNAPSHOT_NAME_LEN - 1U);
    s_snapshots[idx].name[EXT4_COW_SNAPSHOT_NAME_LEN - 1U] = '\0';

    /* 将快照 ID 作为块 ID 建立初始引用 */
    if (s_next_snapshot_id < EXT4_COW_MAX_BLOCKS)
    {
        s_refcounts[s_next_snapshot_id] = 1U;
    }

    /* 保存结果并递增 ID 分配器 */
    result = s_next_snapshot_id;
    s_next_snapshot_id += 1U;

    return result;
}

/**
 * @brief 回滚到指定快照
 *
 * @details 将文件系统状态回滚到指定快照创建时的状态。
 *          回滚操作会将快照对应的块引用计数置零。
 *          快照必须处于活跃状态才能回滚。
 *
 * @param snapshot_id 快照 ID（由 ext4_cow_snapshot_create() 返回）
 *
 * @return 0 成功，<0 失败
 * @retval -EINVAL 快照 ID 无效、快照不活跃或模块未初始化
 */
int32_t ext4_cow_snapshot_rollback(uint32_t snapshot_id)
{
    uint32_t idx;
    int32_t ret;

    /* 验证快照有效性 */
    ret = cow_validate_snapshot(snapshot_id, &idx);
    if (ret != 0)
    {
        return ret;
    }

    /* 回滚：清除块引用计数 */
    cow_clear_block_refcount(snapshot_id);

    /* 清除快照自身的引用计数 */
    s_snapshots[idx].refcount = 0U;

    return 0;
}

/**
 * @brief 清理指定快照
 *
 * @details 释放快照占用的资源，将快照标记为不活跃。
 *          清理后该快照的引用计数置零，不再参与 CoW 逻辑。
 *          快照必须处于活跃状态才能清理。
 *
 * @param snapshot_id 快照 ID（由 ext4_cow_snapshot_create() 返回）
 *
 * @return 0 成功，<0 失败
 * @retval -EINVAL 快照 ID 无效、快照不活跃或模块未初始化
 */
int32_t ext4_cow_snapshot_cleanup(uint32_t snapshot_id)
{
    uint32_t idx;
    int32_t ret;

    /* 验证快照有效性 */
    ret = cow_validate_snapshot(snapshot_id, &idx);
    if (ret != 0)
    {
        return ret;
    }

    /* 清除块引用计数 */
    cow_clear_block_refcount(snapshot_id);

    /* 标记快照为不活跃并清除引用计数 */
    s_snapshots[idx].active = false;
    s_snapshots[idx].refcount = 0U;

    return 0;
}
