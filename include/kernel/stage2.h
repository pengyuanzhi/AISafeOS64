/**
 * @file    stage2.h
 * @brief   ARM Stage-2 页表接口（Guest 物理地址映射）
 * @author  AISafe64 Team
 * @date    2026-07-08
 * @version 1.0
 *
 * @details Stage-2 页表将 Guest IPA（Intermediate Physical Address）
 *          映射到 Host PA。每个 Guest 有独立的 Stage-2 页表。
 *
 * @revision history
 * v1.0 2026-07-08 初始版本
 */

#ifndef KERNEL_STAGE2_H
#define KERNEL_STAGE2_H

#include <stdint.h>

/** @brief Stage-2 权限标志 */
#define S2_PERM_RO    0x1U   /**< @brief 只读 */
#define S2_PERM_RW    0x3U   /**< @brief 读写 */

/**
 * @brief 创建 Guest 地址空间
 *
 * @param out_vm_id 输出 VM ID
 * @return 0 成功，< 0 失败
 */
int32_t s2_create_vm(uint32_t *out_vm_id);

/**
 * @brief 映射 Guest 物理地址到 Host 物理地址
 *
 * @param vm_id VM ID
 * @param ipa Guest 物理地址（Intermediate Physical Address）
 * @param pa Host 物理地址
 * @param size 映射大小
 * @param perm 权限（S2_PERM_*）
 * @return 0 成功
 */
int32_t s2_map(uint32_t vm_id, uint64_t ipa, uint64_t pa,
               uint64_t size, uint32_t perm);

/**
 * @brief 切换到指定 Guest 的 Stage-2 页表
 *
 * @details 通过 HVC 调用 EL2 设置 VTTBR_EL2
 *
 * @param vm_id VM ID
 * @return 0 成功
 */
int32_t s2_switch_vm(uint32_t vm_id);

/**
 * @brief 销毁 Guest 地址空间
 *
 * @param vm_id VM ID
 * @return 0 成功
 */
int32_t s2_destroy_vm(uint32_t vm_id);

#endif /* KERNEL_STAGE2_H */
