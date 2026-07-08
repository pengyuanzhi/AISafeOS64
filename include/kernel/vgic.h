/**
 * @file    vgic.h
 * @brief   ARM VGIC（虚拟中断控制器）接口
 * @author  AISafe64 Team
 * @date    2026-07-08
 * @version 1.0
 *
 * @details VGIC 管理 Guest 虚拟中断。
 *
 * @revision history
 * v1.0 2026-07-08 初始版本
 */

#ifndef KERNEL_VGIC_H
#define KERNEL_VGIC_H

#include <stdint.h>

/**
 * @brief 初始化 VGIC 子系统
 */
void vgic_init(void);

/**
 * @brief 为 Guest 创建 VGIC 状态
 *
 * @param vm_id VM ID
 * @return 0 成功
 */
int32_t vgic_create_vm(uint32_t vm_id);

/**
 * @brief 向 Guest 注入虚拟中断
 *
 * @param vm_id VM ID
 * @param virq 虚拟中断号
 * @return 0 成功
 */
int32_t vgic_inject_irq(uint32_t vm_id, uint32_t virq);

/**
 * @brief 启用 Guest 虚拟中断
 *
 * @param vm_id VM ID
 * @return 0 成功
 */
int32_t vgic_enable(uint32_t vm_id);

/**
 * @brief 禁用 Guest 虚拟中断
 *
 * @param vm_id VM ID
 * @return 0 成功
 */
int32_t vgic_disable(uint32_t vm_id);

/**
 * @brief 销毁 Guest VGIC 状态
 *
 * @param vm_id VM ID
 * @return 0 成功
 */
int32_t vgic_destroy_vm(uint32_t vm_id);

/**
 * @brief 获取硬件支持的 List Register 数量
 *
 * @return LR 数量
 */
uint32_t vgic_get_max_lrs(void);

#endif /* KERNEL_VGIC_H */
