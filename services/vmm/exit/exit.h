/**
 * @file    exit.h
 * @brief   VM 退出处理接口
 * @author  AISafe64 Team
 * @date    2026-05-03
 * @version 1.0
 *
 * @details 本文件定义了 VM 退出处理的数据结构和接口：
 *          - VM 退出原因定义
 *          - 退出处理函数
 *          - 公共 API 接口
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef AISAFE64_EXIT_H
#define AISAFE64_EXIT_H

#include <stdint.h>
#include <kernel/types.h>
#include <kernel/config.h>

/* ========================================================================
 * VM 退出原因定义
 * ======================================================================== */

/** @brief WFI/WFE 退出原因 (EC=0x01) */
#define EXIT_REASON_WFI_WFE    (0x01ULL)

/** @brief HVC 退出原因 (EC=0x08) */
#define EXIT_REASON_HVC        (0x08ULL)

/** @brief 系统寄存器退出原因 (EC=0x06) */
#define EXIT_REASON_SYSREG     (0x06ULL)

/** @brief 指令中止退出原因 (EC=0x0E) */
#define EXIT_REASON_INST_ABORT (0x0EULL)

/** @brief 数据中止退出原因 (EC=0x0A) */
#define EXIT_REASON_DATA_ABORT (0x0AULL)

/* ========================================================================
 * MMIO 访问宽度定义
 * ======================================================================== */

#define MMIO_ACCESS_MAX_SIZE   (8U)

/* ========================================================================
 * 内部 API 接口
 * ======================================================================== */

/**
 * @brief 处理 VM 退出事件（内部 API）
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t exit_handler(uint32_t vm_id, uint32_t vcpu_id);

/* ========================================================================
 * 公共 API 接口
 * ======================================================================== */

/**
 * @brief 处理 VM 退出事件
 *
 * @details 根据 ESR_EL2 提取异常类 (EC)，分发到对应的退出处理函数
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_handle_exit(uint32_t vm_id, uint32_t vcpu_id);

#endif /* AISAFE64_EXIT_H */
