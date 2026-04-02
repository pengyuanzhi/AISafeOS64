/**
 * @file    secure_boot.h
 * @brief   安全启动链验证框架接口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件定义了安全启动链验证框架接口：
 *          - 引导阶段签名验证
 *          - 镜像完整性验证
 *          - 安全启动状态管理
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: SE-008, SE-009
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_SECURE_BOOT_H
#define KERNEL_SECURE_BOOT_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * 安全启动阶段
 * ======================================================================== */

/**
 * @brief 安全启动阶段
 */
typedef enum
{
    BOOT_PHASE_ROM = 0U,           /**< @brief Boot ROM 阶段（不可变） */
    BOOT_PHASE_BOOTLOADER,         /**< @brief Bootloader 阶段 */
    BOOT_PHASE_KERNEL,             /**< @brief 内核加载阶段 */
    BOOT_PHASE_SERVICES,           /**< @brief 用户态服务启动阶段 */
    BOOT_PHASE_COMPLETE            /**< @brief 启动完成 */
} boot_phase_t;

/* ========================================================================
 * 签名验证状态
 * ======================================================================== */

/**
 * @brief 签名验证状态
 */
typedef enum
{
    SIG_VERIFY_OK = 0U,         /**< @brief 验证通过 */
    SIG_VERIFY_FAILED,          /**< @brief 验证失败（签名不匹配） */
    SIG_VERIFY_NO_KEY,          /**< @brief 无签名密钥 */
    SIG_VERIFY_ERROR             /**< @brief 验证错误 */
} sig_verify_result_t;

/* ========================================================================
 * 安全启动配置
 * ======================================================================== */

/**
 * @brief 安全启动配置
 */
typedef struct
{
    boot_phase_t current_phase;       /**< @brief 当前启动阶段 */
    bool         signature_check;     /**< @brief 是否启用签名检查 */
    bool         secure_boot_enabled; /**< @brief 安全启动是否已启用 */
} secure_boot_config_t;

/* ========================================================================
 * 安全启动 API
 * ======================================================================== */

/**
 * @brief 初始化安全启动子系统
 *
 * @return KERNEL_OK 成功
 *
 * @note 对应需求: SE-009
 */
kernel_status_t secure_boot_init(void);

/**
 * @brief 验证引导加载器签名
 *
 * @details 使用 ROM 中的公钥验证 bootloader 的签名。
 *
 * @return SIG_VERIFY_OK 验证通过
 * @return SIG_VERIFY_FAILED 签名不匹配
 * @return SIG_VERIFY_NO_KEY 无签名密钥
 *
 * @note 对应需求: SE-008
 */
sig_verify_result_t secure_boot_verify_loader(void);

/**
 * @brief 验证内核镜像签名
 *
 * @param kernel_addr 内核加载地址
 * @param kernel_size 内核镜像大小
 *
 * @return SIG_VERIFY_OK 验证通过
 * @return SIG_VERIFY_FAILED 签名不匹配
 *
 * @note 对应需求: SE-008
 */
sig_verify_result_t secure_boot_verify_kernel(uintptr_t kernel_addr,
                                               uint64_t kernel_size);

/**
 * @brief 获取当前启动阶段
 *
 * @return 当前启动阶段
 */
boot_phase_t secure_boot_get_phase(void);

/**
 * @brief 获取安全启动配置
 *
 * @param config 输出配置
 */
void secure_boot_get_config(secure_boot_config_t *config);

#endif /* KERNEL_SECURE_BOOT_H */
