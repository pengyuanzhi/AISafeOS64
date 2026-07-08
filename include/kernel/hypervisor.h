/**
 * @file    hypervisor.h
 * @brief   ARM EL2 Hypervisor 接口
 * @author  AISafe64 Team
 * @date    2026-07-08
 * @version 1.0
 *
 * @details EL2 Hypervisor 配置层，为 Guest OS 虚拟化奠定基础。
 *
 * @revision history
 * v1.0 2026-07-08 初始版本（EL2 配置初始化）
 */

#ifndef KERNEL_HYPERVISOR_H
#define KERNEL_HYPERVISOR_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 初始化 EL2 Hypervisor 配置
 *
 * @details 在 boot.S 中从 EL2 降级到 EL1 之前调用。
 *          配置 HCR_EL2/VTCR_EL2/CNTHCTL_EL2/CPTR_EL2。
 *
 * @note 必须在 EL2 中调用
 */
void hypervisor_init(void);

/**
 * @brief 检查是否在 EL2 运行
 *
 * @return true 当前在 EL2
 */
bool hypervisor_is_el2(void);

/**
 * @brief 创建 Guest OS（占位）
 *
 * @return 0 成功，< 0 未实现
 */
int32_t hypervisor_create_guest(void);

#endif /* KERNEL_HYPERVISOR_H */
