/**
 * @file    vmm.h
 * @brief   VMM 公共接口（简化 Mock 版本，用于 NPT 测试）
 * @author  AISafe64 Team
 * @date    2026-05-22
 * @version 1.0
 */

#ifndef SERVICES_VMM_VMM_H
#define SERVICES_VMM_VMM_H

#include <stdint.h>
#include <kernel/types.h>
#include "./npt/npt.h"  /* 包含嵌套页表定义 */

/* ========================================================================
 * VMM 常量
 * ======================================================================== */

#define VMM_MAX_VMS               (32U)          /**< 最大 VM 数量 */

/* ========================================================================
 * 类型定义（前置声明）
 * ======================================================================== */

typedef struct vm_desc vm_desc_t;

/* ========================================================================
 * 内部 API 声明（用于 NPT）
 * ======================================================================== */

/**
 * @brief 获取 VM 描述符
 *
 * @param vm_id VM ID
 *
 * @return VM 描述符指针，失败返回 NULL
 */
vm_desc_t *vmm_get_vm(uint32_t vm_id);

/**
 * @brief 获取 VM 的嵌套页表（内部 API）
 *
 * @param vm_id VM ID
 *
 * @return 嵌套页表指针，失败返回 NULL
 */
nested_page_table_t *vmm_get_npt(uint32_t vm_id);

#endif /* SERVICES_VMM_VMM_H */