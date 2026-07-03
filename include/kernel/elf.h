/**
 * @file    elf.h
 * @brief   ELF 加载器接口
 * @version 1.0
 * @date    2026-04-15
 *
 * ELF 加载器提供从 VirtIO 块设备读取 ELF 文件并解析的功能。
 * 支持 64 位 AArch64 ELF 可执行文件（ET_EXEC）和共享目标文件（ET_DYN）。
 *
 * @note 供内核启动流程使用
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_ELF_H
#define KERNEL_ELF_H

#include <kernel/types.h>
#include <kernel/errno.h>
#include <stdbool.h>

/* ========================================================================
 * ELF 类型定义
 * ======================================================================== */

#define ELF_ET_EXEC            2U    /**< @brief 可执行文件 */
#define ELF_ET_DYN             3U    /**< @brief 共享目标文件 */

#define ELF_EM_AARCH64         183U   /**< @brief ARM64 架构 */

#define ELF_PT_NULL            0U    /**< @brief 未使用 */
#define ELF_PT_LOAD            1U    /**< @brief 可加载段 */
#define ELF_PT_DYNAMIC         2U    /**< @brief 动态链接信息 */

#define ELF_PF_R               (1U << 0)  /**< @brief 可读 */
#define ELF_PF_W               (1U << 1)  /**< @brief 可写 */
#define ELF_PF_X               (1U << 2)  /**< @brief 可执行 */

/* ========================================================================
 * ELF 头结构（仅需要的字段）
 * ======================================================================== */

typedef struct
{
    uint8_t  e_ident[16];      /**< @brief 魔术和文件类 */
    uint16_t e_type;           /**< @brief 文件类型 */
    uint16_t e_machine;        /**< @brief 机器类型 */
    uint32_t e_version;        /**< @brief 版本 */
    uint64_t e_entry;         /**< @brief 入口点 */
    uint64_t e_phoff;         /**< @brief 段表偏移 */
    uint64_t e_shoff;         /**< @brief 节表偏移 */
    uint32_t e_flags;         /**< @brief 处理器标志 */
    uint16_t e_ehsize;        /**< @brief ELF 文件头大小 */
    uint16_t e_phentsize;     /**< @brief 段表条目大小 */
    uint16_t e_phnum;         /**< @brief 段表条目数 */
    uint16_t e_shentsize;     /**< @brief 节表条目大小 */
    uint16_t e_shnum;         /**< @brief 节表条目数 */
} elf_header_t;

/**
 * @brief ELF 段表条目（64 位）
 */
typedef struct
{
    uint32_t p_type;          /**< @brief 段类型 */
    uint32_t p_flags;         /**< @brief 段权限 */
    uint64_t p_offset;        /**< @brief 文件偏移 */
    uint64_t p_vaddr;         /**< @brief 虚拟地址 */
    uint64_t p_paddr;         /**< @brief 物理地址 */
    uint64_t p_filesz;        /**< @brief 文件大小 */
    uint64_t p_memsz;         /**< @brief 内存大小 */
    uint64_t p_align;         /**< @brief 对齐 */
} elf_program_header_t;

/**
 * @brief ELF 段描述符
 */
typedef struct
{
    uint64_t vaddr;            /**< @brief 虚拟地址 */
    uint64_t length;           /**< @brief 段内存长度（memsz） */
    uint64_t filesz;           /**< @brief 段文件长度（filesz，区分代码与 BSS） */
    uint64_t offset;           /**< @brief 文件偏移 */
    uint8_t  prot;            /**< @brief 保护权限（PF_R|PF_W|PF_X） */
    bool     active;           /**< @brief 活跃标记 */
} elf_segment_t;

/* ========================================================================
 * ELF 加载器状态
 * ======================================================================== */

typedef enum
{
    ELF_OK = 0U,              /**< @brief 成功 */
    ELF_ERR_NO_DEV = 1U,      /**< @brief 无设备 */
    ELF_ERR_READ = 2U,         /**< @brief 读取失败 */
    ELF_ERR_MAGIC = 3U,        /**< @brief 魔术错误 */
    ELF_ERR_CLASS = 4U,        /**< @brief 类别错误 */
    ELF_ERR_ENDIAN = 5U,       /**< @brief 字节序错误 */
    ELF_ERR_MACHINE = 6U,      /**< @brief 机器类型错误 */
    ELF_ERR_TYPE = 7U,         /**< @brief 文件类型错误 */
    ELF_ERR_SEGMENT = 8U       /**< @brief 段错误 */
} elf_error_t;

/* ========================================================================
 * ELF 加载器接口
 * ======================================================================== */

/**
 * @brief 初始化 ELF 加载器
 *
 * 从 VirtIO 块设备读取 ELF 文件，解析 ELF 头和段表。
 *
 * @param elf_data ELF 数据缓冲区
 * @param elf_size ELF 数据大小（字节）
 * @param header 输出：ELF 头
 * @param segments 输出：ELF 段数组
 * @param max_segments 最大段数
 * @param segment_count 输出：实际段数
 * @return ELF_OK 成功，其他为错误码
 *
 * @note 仅解析 ELF 头和段表，不执行实际加载
 * @note 需要在内核启动流程中调用
 */
elf_error_t elf_loader_init(const uint8_t *elf_data, uint32_t elf_size,
                           elf_header_t *header,
                           elf_segment_t *segments, uint32_t max_segments,
                           uint32_t *segment_count);

/**
 * @brief 获取 ELF 加载器状态
 *
 * @return ELF_OK 已初始化，其他为错误码
 */
elf_error_t elf_loader_get_status(void);

/**
 * @brief 获取 ELF 入口点
 *
 * @param entry 输出：入口点地址
 * @return ELF_OK 成功，其他为错误码
 */
elf_error_t elf_loader_get_entry(uint64_t *entry);

/**
 * @brief 从块设备加载 ELF 到用户空间并创建用户线程
 *
 * @details 完整的 ELF 加载流程：
 *          1. 从块设备读取整个 ELF 文件到内核缓冲
 *          2. 解析 ELF 头和 PT_LOAD 段
 *          3. 创建用户地址空间（user_pgd）
 *          4. 逐段映射到用户空间（TTBR0），拷贝段数据，清零 BSS
 *          5. 分配用户栈
 *          6. 创建用户线程（入口为 ELF entry，TTBR0 低地址）
 *
 * @param elf_data    已读入内存的完整 ELF 数据缓冲
 * @param elf_size    ELF 数据大小
 * @param thread_name 线程名称
 * @return KERNEL_OK 成功，其他为错误码
 *
 * @note 调用前需确保块设备已读取 ELF 到 elf_data
 * @note 线程在 scheduler_start() 后执行
 */
kernel_status_t elf_load_and_run(const uint8_t *elf_data, uint32_t elf_size,
                                  const char *thread_name);

#endif /* KERNEL_ELF_H */
