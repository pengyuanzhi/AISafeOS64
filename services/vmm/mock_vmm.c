/**
 * @file    mock_vmm.c
 * @brief   VMM Mock 实现（用于 NPT 测试）
 * @author  AISafe64 Team
 * @date    2026-05-22
 */

#include <stdint.h>
#include <string.h>
#include <kernel/types.h>
#include <kernel/errno.h>
#include "vmm.h"

/* ========================================================================
 * Mock 数据结构定义
 * ======================================================================== */

typedef struct vm_desc
{
    uint32_t vm_id;
    uint32_t vcpu_count;
    nested_page_table_t *npt;
    struct vcpu_desc *vcpus;
} vm_desc_t;

/* ======================================================================== * Mock 数据
 * ======================================================================== */

static vm_desc_t s_vms[VMM_MAX_VMS];

/* ========================================================================
 * Mock 实现
 * ======================================================================== */

vm_desc_t *vmm_get_vm(uint32_t vm_id)
{
    if (vm_id >= VMM_MAX_VMS)
    {
        return NULL;
    }

    return &s_vms[vm_id];
}

nested_page_table_t *vmm_get_npt(uint32_t vm_id)
{
    if (vm_id >= VMM_MAX_VMS)
    {
        return NULL;
    }

    return &s_vms[vm_id].npt;
}