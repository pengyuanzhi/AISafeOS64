/**
 * @file    ext4_cow.h
 * @brief   EXT4 写时复制（CoW）接口
 * @author  AISafe64 Team
 * @date    2026-05-12
 * @version 1.1
 *
 * @details EXT4 写时复制（Copy-on-Write）接口：
 *          - 块引用计数管理（递增、递减、查询）
 *          - 快照创建与回滚
 *          - 快照清理
 *          - POSIX 错误码返回约定
 *
 * @note MISRA-C:2012 合规
 * @note TDD: REFACTOR 阶段 - 优化重构
 *
 * @warning 本模块非线程安全，调用方需在锁保护下使用
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef EXT4_COW_H
#define EXT4_COW_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 最大引用计数 */
#define EXT4_COW_MAX_REFCOUNT     0xFFFFFFFFU

/** @brief 最大快照数量 */
#define EXT4_COW_MAX_SNAPSHOTS    64U

/** @brief 最大块数量 */
#define EXT4_COW_MAX_BLOCKS       4096U

/** @brief 快照名称最大长度（含终止符） */
#define EXT4_COW_SNAPSHOT_NAME_LEN  32U

/** @brief 无效块 ID 标记 */
#define EXT4_COW_INVALID_BLOCK    0U

/* ========================================================================
 * POSIX 错误码定义（内部使用）
 * ======================================================================== */

/** @brief 无效的参数 */
#define EXT4_COW_EINVAL           22

/** @brief 内存不足（引用计数溢出时使用） */
#define EXT4_COW_ENOMEM           12

/* ========================================================================
 * 数据结构
 * ======================================================================== */

/**
 * @brief 快照信息结构体
 *
 * @details 存储单个快照的完整元数据，
 *          包括标识、引用关系和生命周期状态
 */
typedef struct
{
    uint32_t id;                               /**< @brief 快照唯一 ID（从 1 开始递增） */
    char     name[EXT4_COW_SNAPSHOT_NAME_LEN]; /**< @brief 快照名称（以 '\0' 结尾） */
    uint32_t refcount;                         /**< @brief 当前引用计数 */
    uint32_t block_count;                      /**< @brief 关联的数据块数量 */
    bool     active;                           /**< @brief 快照是否活跃（true=活跃，false=已清理） */
} ext4_cow_snapshot_t;

/* ========================================================================
 * 接口函数声明
 * ======================================================================== */

/**
 * @brief 初始化 CoW 模块
 *
 * @details 清空引用计数表和快照表，将模块置为就绪状态。
 *          必须在使用其他 CoW 接口前调用。
 *
 * @return 0 成功
 *
 * @note 重复调用将重置所有内部状态
 * @see ext4_cow_destroy()
 *
 * @since 1.0.0
 */
int32_t ext4_cow_init(void);

/**
 * @brief 销毁 CoW 模块
 *
 * @details 释放所有内部状态，将模块置为未初始化。
 *          销毁后不得调用其他接口（除非重新初始化）。
 *
 * @note 安全地支持重复调用
 * @see ext4_cow_init()
 *
 * @since 1.0.0
 */
void ext4_cow_destroy(void);

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
 *
 * @pre ext4_cow_init() 已调用
 *
 * @since 1.0.0
 */
int32_t ext4_cow_refcount_inc(uint32_t block_id);

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
 *
 * @pre ext4_cow_init() 已调用
 *
 * @since 1.0.0
 */
int32_t ext4_cow_refcount_dec(uint32_t block_id);

/**
 * @brief 获取块引用计数
 *
 * @details 查询指定块的当前引用计数值。
 *          块 ID 无效或模块未初始化时返回 0。
 *
 * @param block_id 块 ID
 *
 * @return 引用计数值（0 表示无效或未引用）
 *
 * @since 1.0.0
 */
uint32_t ext4_cow_get_refcount(uint32_t block_id);

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
 *
 * @pre ext4_cow_init() 已调用
 * @pre name 不能为 NULL
 *
 * @since 1.0.0
 */
uint32_t ext4_cow_snapshot_create(const char *name);

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
 *
 * @pre ext4_cow_init() 已调用
 * @pre snapshot_id 对应的快照必须处于活跃状态
 *
 * @since 1.0.0
 */
int32_t ext4_cow_snapshot_rollback(uint32_t snapshot_id);

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
 *
 * @pre ext4_cow_init() 已调用
 * @pre snapshot_id 对应的快照必须处于活跃状态
 *
 * @since 1.0.0
 */
int32_t ext4_cow_snapshot_cleanup(uint32_t snapshot_id);

#endif /* EXT4_COW_H */
