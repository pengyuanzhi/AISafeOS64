/**
 * @file    dynamic_link.h
 * @brief   动态链接接口
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 *
 * @details 提供动态链接接口：
 *          - 共享库加载
 *          - 符号解析
 *          - 符号绑定
 *          - 函数调用
 *
 * @note MISRA C:2012 合规
 * @note 对应优化点：动态链接
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_DYNAMIC_LINK_H
#define KERNEL_DYNAMIC_LINK_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/alignment.h>
#include <kernel/spinlock.h>
#include <stdint.h>

/* ========================================================================
 * 动态链接配置常量
 * ======================================================================== */

/** @brief 最大共享库数量 */
#define DYNLIB_MAX_LIBS    32U

/** @brief 最大符号表大小 */
#define DYNLIB_MAX_SYMS    8192U

/** @brief 最大库路径长度 */
#define DYNLIB_PATH_MAX    256U

/** @brief 符号版本 */
#define DYNLIB_VERSION_1   1U

/* ========================================================================
 * 符号信息
 * ======================================================================== */

/**
 * @brief 符号类型
 */
typedef enum
{
    DYNLIB_SYM_FUNC = 0U,      /**< @brief 函数 */
    DYNLIB_SYM_DATA,           /**< @brief 数据 */
    DYNLIB_SYM_OBJECT          /**< @brief 对象 */
} dynlib_sym_type_t;

/* ========================================================================
 * 符号信息
 * ======================================================================== */

/**
 * @brief 符号信息
 *
 * @details 存储动态链接的符号信息。
 */
typedef struct CACHE_ALIGN(64)
{
    dynlib_sym_type_t type;     /**< @brief 符号类型 */
    void             *address;  /**< @brief 符号地址 */
    const char      *name;      /**< @brief 符号名称 */
    void            *data;      /**< @brief 符号数据 */
    uint32_t          size;      /**< @brief 符号大小 */
    uint32_t          hash;      /**< @brief 符号哈希 */
    uint32_t          version;   /**< @brief 符号版本 */
} dynlib_sym_t;

/* ========================================================================
 * 共享库描述符
 * ======================================================================== */

/**
 * @brief 共享库描述符
 *
 * @details 存储动态加载的库的信息。
 */
typedef struct CACHE_ALIGN(64)
{
    const char      *path;      /**< @brief 库文件路径 */
    uint8_t          data[0];   /**< @data 字节数组 */
} dynlib_desc_t;

/* ========================================================================
 * 共享库管理器
 * ======================================================================== */

/**
 * @brief 共享库管理器
 *
 * @details 管理所有动态加载的库。
 */
typedef struct
{
    dynlib_desc_t   *libs[DYNLIB_MAX_LIBS];  /**< @brief 库数组 */
    dynlib_sym_t     syms[DYNLIB_MAX_SYMS];  /**< @brief 符号表 */
    uint32_t         lib_count;               /**< @count */
    uint32_t         sym_count;                /**< @brief 符号数量 */
    TicketLock_t     lock;                    /**< @brief 管理器锁 */
    uint32_t         total_size;              /**< @brief 总大小 */
} CACHE_ALIGN(64) dynlib_manager_t;

/* ========================================================================
 * 动态链接操作 API
 * ======================================================================== */

/**
 * @brief 初始化动态链接管理器
 *
 * @param manager 动态链接管理器实例
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t dynlib_init(dynlib_manager_t *manager);

/**
 * @brief 加载共享库
 *
 * @details 加载动态链接库。
 *
 * @param manager 动态链接管理器实例
 * @param path    库文件路径
 *
 * @return 库描述符指针，失败返回 NULL
 */
const dynlib_desc_t *dynlib_load(dynlib_manager_t *manager, const char *path);

/**
 * @brief 卸载共享库
 *
 * @details 卸载动态链接库。
 *
 * @param manager 动态链接管理器实例
 * @param desc    库描述符
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t dynlib_unload(dynlib_manager_t *manager, const dynlib_desc_t *desc);

/**
 * @brief 解析符号
 *
 * @details 解析符号地址。
 *
 * @param manager 动态链接管理器实例
 * @param name    符号名称
 * @param addr    输出参数，符号地址
 *
 * @return KERNEL_OK 成功
 * @return -ENOENT 符号未找到
 */
kernel_status_t dynlib_symbol(const dynlib_manager_t *manager,
                              const char *name,
                              void **addr);

/**
 * @brief 绑定符号
 *
 * @details 绑定符号地址到全局符号表。
 *
 * @param manager 动态链接管理器实例
 * @param name    符号名称
 * @param addr    符号地址
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t dynlib_bind(const dynlib_manager_t *manager,
                            const char *name,
                            void *addr);

/**
 * @brief 调用函数
 *
 * @details 通过函数指针调用动态链接的函数。
 *
 * @param manager 动态链接管理器实例
 * @param name    函数名称
 * @param args    函数参数
 * @param ret     输出参数，返回值
 *
 * @return KERNEL_OK 成功
 * @return -ENOENT 函数未找到
 */
kernel_status_t dynlib_call(const dynlib_manager_t *manager,
                            const char *name,
                            void *args,
                            void **ret);

/**
 * @brief 获取动态链接统计信息
 *
 * @param manager 动态链接管理器实例
 * @param lib_count   输出：加载的库数量
 * @param sym_count   输出：符号数量
 * @param total_size  输出：总大小
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t dynlib_get_stats(const dynlib_manager_t *manager,
                                 uint32_t *lib_count,
                                 uint32_t *sym_count,
                                 uint32_t *total_size);

#endif /* KERNEL_DYNAMIC_LINK_H */
