/**
 * @file    vmm_stats.h
 * @brief   VMM 统计信息接口
 * @author  AISafe64 Team
 * @date    2026-05-03
 * @version 1.0
 *
 * @details 本文件定义了 VMM（虚拟机管理器）的统计信息结构：
 *          - VMM 统计信息结构
 *          - 统计信息更新函数
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef SERVICES_VMM_STATS_VMM_STATS_H
#define SERVICES_VMM_STATS_VMM_STATS_H

#include <stdint.h>
#include <stdbool.h>
#include <kernel/types.h>

/* ========================================================================
 * 统计信息结构
 * ======================================================================== */

/**
 * @brief VMM 统计信息
 *
 * @details VMM 运行时的统计信息，用于性能监控和分析
 */
typedef struct
{
    /** @brief VM 统计 */
    uint32_t       vm_created;       /**< @brief 已创建 VM 数 */
    uint32_t       vm_running;       /**< @brief 运行中 VM 数 */
    uint32_t       vm_total;         /**< @brief 总 VM 数 */

    /** @brief vCPU 统计 */
    uint32_t       vcpu_created;     /**< @brief 已创建 vCPU 数 */
    uint32_t       vcpu_running;     /**< @brief 运行中 vCPU 数 */
    uint32_t       vcpu_total;       /**< @brief 总 vCPU 数 */

    /** @brief 设备统计 */
    uint32_t       vdev_total;       /**< @brief 虚拟设备数 */
    uint32_t       vdev_block;       /**< @brief 块设备数 */
    uint32_t       vdev_net;         /**< @brief 网卡设备数 */

    /** @brief VM 退出统计 */
    uint64_t       exit_wfi;         /**< @brief WFI/WFE 退出数 */
    uint64_t       exit_hypercall;   /**< @brief Hypercall 退出数 */
    uint64_t       exit_mmio;        /**< @brief MMIO 退出数 */
    uint64_t       exit_sysreg;      /**< @brief 系统寄存器退出数 */
    uint64_t       exit_inst_abort;  /**< @brief 指令中止退出数 */
    uint64_t       exit_total;       /**< @brief 总退出数 */

    /** @brief 性能统计 */
    uint64_t       run_time_total;   /**< @brief 总运行时间 (ticks) */
} vmm_stats_t;

/* ========================================================================
 * 统计信息更新函数
 * ======================================================================== */

/**
 * @brief 更新 VM 统计信息
 *
 * @param vm_id       VM ID
 * @param created     true=创建, false=销毁
 */
void vmm_stats_update_vm(uint32_t vm_id, bool created);

/**
 * @brief 更新 vCPU 统计信息
 *
 * @param vm_id       VM ID
 * @param vcpu_id     vCPU ID
 * @param created     true=创建, false=销毁
 * @param running     true=运行中, false=停止
 */
void vmm_stats_update_vcpu(uint32_t vm_id, uint32_t vcpu_id,
                           bool created, bool running);

/**
 * @brief 更新设备统计信息
 *
 * @param vm_id       VM ID
 * @param dev_type    设备类型
 * @param registered  true=注册, false=注销
 */
void vmm_stats_update_device(uint32_t vm_id, uint32_t dev_type,
                             bool registered);

/**
 * @brief 更新 VM 退出统计信息
 *
 * @param exit_type   退出类型
 */
void vmm_stats_update_exit(uint64_t exit_type);

/**
 * @brief 增加运行时间
 *
 * @param vm_id       VM ID
 * @param vcpu_id     vCPU ID
 * @param ticks       增加的 ticks
 */
void vmm_stats_increment_run_time(uint32_t vm_id, uint32_t vcpu_id,
                                  uint64_t ticks);

/**
 * @brief 重置统计信息
 *
 * @param stats       统计信息指针
 */
void vmm_stats_reset(vmm_stats_t *stats);

#endif /* SERVICES_VMM_STATS_VMM_STATS_H */
