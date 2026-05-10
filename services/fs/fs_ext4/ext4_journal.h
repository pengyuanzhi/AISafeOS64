/**
 * @file    ext4_journal.h
 * @brief   EXT4 日志文件系统接口
 * @author  AISafe64 Team
 * @date    2026-05-10
 * @version 1.0
 *
 * @details EXT4 日志文件系统接口：
 *          - Journal 超级块管理
 *          - 元数据预写日志
 *          - Journal 提交和验证
 *          - 崩溃恢复机制
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef EXT4_JOURNAL_H
#define EXT4_JOURNAL_H

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief Journal 大小（块数） */
#define EXT4_JOURNAL_SIZE         16U

/** @brief Journal 块大小（4KB） */
#define EXT4_JOURNAL_BLOCK_SIZE   4096U

/** @brief Journal 数据大小（16KB） */
#define EXT4_JOURNAL_DATA_SIZE    (EXT4_JOURNAL_SIZE * EXT4_JOURNAL_BLOCK_SIZE)

/** @brief Journal 超级块偏移（块） */
#define EXT4_JOURNAL_SUPERBLOCK_OFFSET  0U

/** @brief Journal 序列号最大值 */
#define EXT4_JOURNAL_SEQUENCE_MAX     (1U << 28)

/* ========================================================================
 * 枚举类型
 * ======================================================================== */

/**
 * @brief Journal 类型
 */
typedef enum
{
    EXT4_JOURNAL_ORDERED = 0U,   /**< @brief Ordered 模式：只保证元数据顺序 */
    EXT4_JOURNAL_WRITEBACK       /**< @brief Writeback 模式：不保证顺序 */
} ext4_journal_type_t;

/**
 * @brief Journal 状态
 */
typedef enum
{
    EXT4_JOURNAL_INVALID = 0U,   /**< @brief 无效 */
    EXT4_JOURNAL_CLEAN = 1U,     /**< @brief 清洁状态 */
    EXT4_JOURNAL_DIRTY = 2U,     /**< @brief 脏状态 */
    EXT4_JOURNAL_RECOVER = 3U    /**< @brief 需要恢复 */
} ext4_journal_state_t;

/**
 * @brief 元数据记录类型
 */
typedef enum
{
    EXT4_JMETADATA_INODE = 0U,    /**< @brief Inode 修改 */
    EXT4_JMETADATA_BLOCK,         /**< @brief 块分配/释放 */
    EXT4_JMETADATA_DIR,           /**< @brief 目录修改 */
    EXT4_JMETADATA_SYNC           /**< @brief 同步点 */
} ext4_jmetadata_type_t;

/* ========================================================================
 * 数据结构
 * ======================================================================== */

/**
 * @brief Journal 超级块
 */
typedef struct
{
    uint32_t        journal_size;      /**< @brief Journal 大小（块数） */
    uint32_t        journal_start;     /**< @brief Journal 起始块 */
    uint32_t        journal_sequence;   /**< @brief Journal 序列号 */
    uint32_t        journal_head;       /**< @brief Journal 头位置（循环缓冲区） */
    uint32_t        journal_tail;       /**< @brief Journal 尾位置（循环缓冲区） */
    uint32_t        journal_state;      /**< @brief Journal 状态 */
    ext4_journal_type_t journal_type;  /**< @brief Journal 类型 */
    uint32_t        journal_inode;      /**< @brief Journal inode 编号 */
    uint32_t        journal_uuid[4];    /**< @brief Journal UUID */
} ext4_journal_superblock_t;

/**
 * @brief Journal 元数据记录
 */
typedef struct
{
    ext4_jmetadata_type_t type;     /**< @brief 记录类型 */
    uint32_t        sequence;       /**< @brief 序列号 */
    uint32_t        block;          /**< @brief 块编号 */
    uint32_t        inode;          /**< @brief inode 编号 */
    uint32_t        size;           /**< @brief 大小 */
    uint32_t        flags;          /**< @brief 标志 */
    uint32_t        timestamp;      /**< @brief 时间戳 */
    uint8_t         data[256];      /**< @brief 数据 */
} ext4_journal_metadata_t;

/* ========================================================================
 * 接口函数声明
 * ======================================================================== */

/**
 * @brief 初始化 Journal
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_journal_init(void);

/**
 * @brief 销毁 Journal
 */
void ext4_journal_destroy(void);

/**
 * @brief 写入元数据到 Journal
 *
 * @param metadata 元数据记录
 * @return 0 成功，<0 失败
 */
int32_t ext4_journal_write_metadata(const ext4_journal_metadata_t *metadata);

/**
 * @brief 同步 Journal 到磁盘
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_journal_sync(void);

/**
 * @brief 提交 Journal
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_journal_commit(void);

/**
 * @brief 验证 Journal 一致性
 *
 * @return true 一致，false 不一致
 */
bool ext4_journal_validate(void);

/**
 * @brief 从磁盘读取 Journal
 *
 * @param dev_id 设备 ID
 * @return 0 成功，<0 失败
 */
int32_t ext4_journal_load_from_disk(uint32_t dev_id);

/**
 * @brief 保存 Journal 到磁盘
 *
 * @param dev_id 设备 ID
 * @return 0 成功，<0 失败
 */
int32_t ext4_journal_save_to_disk(uint32_t dev_id);

/**
 * @brief 获取 Journal 状态
 *
 * @return Journal 状态
 */
ext4_journal_state_t ext4_journal_get_state(void);

/**
 * @brief 设置 Journal 状态
 *
 * @param state 新状态
 */
void ext4_journal_set_state(ext4_journal_state_t state);

#endif /* EXT4_JOURNAL_H */
