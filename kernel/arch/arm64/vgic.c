/**
 * @file    vgic.c
 * @brief   ARM VGIC（虚拟中断控制器）管理
 * @author  AISafe64 Team
 * @date    2026-07-08
 * @version 1.0
 *
 * @details VGIC 实现 Guest 虚拟中断管理：
 *          - 物理中断 trap 到 EL2 后，映射到 Guest 虚拟中断
 *          - 通过 GICv3 虚拟化接口（ICH_* 寄存器）注入虚拟中断
 *          - Guest EOI 虚拟中断后清理状态
 *
 *          GICv3 虚拟化关键寄存器：
 *          - ICH_VTR_EL2：虚拟中断能力（最多 LR 数量）
 *          - ICH_LR<n>_EL2：List Register（虚拟中断待处理队列）
 *          - ICH_HCR_EL2：虚拟中断控制
 *          - ICC_IAR1_EL1：Guest 读取中断号（虚拟 IAR）
 *          - ICC_EOIR1_EL1：Guest EOI（虚拟 EOI）
 *
 *          虚拟中断流程：
 *          1. Host 物理中断到达 → EL2 trap
 *          2. Host 判断中断属于哪个 Guest
 *          3. 填充 ICH_LR<n>_EL2 注入虚拟中断
 *          4. Guest eret 后读取 ICC_IAR1_EL1 获取虚拟中断号
 *          5. Guest 处理完毕写 ICC_EOIR1_EL1
 *          6. EL2 trap（EOI trap）→ Host 清理
 *
 * @note    QNX 方案：EL2 hypervisor + EL1 Host
 *
 * @revision history
 * v1.0 2026-07-08 初始版本
 */

#include <kernel/vgic.h>
#include <kernel/config.h>
#include <kernel/errno.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "hal.h"

/* ========================================================================
 * GICv3 虚拟化寄存器操作
 * ======================================================================== */

/**
 * @brief 读取 ICH_VTR_EL2（虚拟中断能力）
 *
 * @details 返回 List Register 数量等信息。
 *          [4:0] = ListRegs（最大 LR 数量 - 1）
 *
 * @return ICH_VTR_EL2 值
 */
static inline uint64_t vgic_read_vtr(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, ich_vtr_el2" : "=r"(val));
    return val;
}

/**
 * @brief 写入 ICH_LR<n>_EL2（List Register）
 *
 * @details 向 Guest 注入一个虚拟中断。
 *          n = LR 编号（0~最大LR数-1）
 *
 * @param lr LR 编号
 * @param val LR 值
 */
static inline void vgic_write_lr(uint32_t lr, uint64_t val)
{
    /* ICH_LR0_EL2 ~ ICH_LR15_EL2 */
    switch (lr)
    {
        case 0U:
            __asm__ volatile("msr ich_lr0_el2, %0" :: "r"(val));
            break;
        case 1U:
            __asm__ volatile("msr ich_lr1_el2, %0" :: "r"(val));
            break;
        case 2U:
            __asm__ volatile("msr ich_lr2_el2, %0" :: "r"(val));
            break;
        case 3U:
            __asm__ volatile("msr ich_lr3_el2, %0" :: "r"(val));
            break;
        case 4U:
            __asm__ volatile("msr ich_lr4_el2, %0" :: "r"(val));
            break;
        case 5U:
            __asm__ volatile("msr ich_lr5_el2, %0" :: "r"(val));
            break;
        case 6U:
            __asm__ volatile("msr ich_lr6_el2, %0" :: "r"(val));
            break;
        case 7U:
            __asm__ volatile("msr ich_lr7_el2, %0" :: "r"(val));
            break;
        default:
            break;
    }
}

/**
 * @brief 写入 ICH_HCR_EL2（虚拟中断控制）
 *
 * @details 控制虚拟中断行为：
 *          bit[0] = En（启用虚拟中断）
 *          bit[1] = UIE（未处理中断使能）
 *          bit[2] = LRENPIE（LR 空中断使能）
 *          bit[3] = NPIE（无待处理中断使能）
 *          bit[4] = VGrp0EIE（Group0 EOI trap）
 *          bit[5] = VGrp0DIE
 *          bit[6] = VGrp1EIE（Group1 EOI trap）
 *          bit[7] = VGrp1DIE
 */
static inline void vgic_write_hcr(uint64_t val)
{
    __asm__ volatile("msr ich_hcr_el2, %0" :: "r"(val));
    __asm__ volatile("isb");
}

/* ========================================================================
 * ICH_LR 位定义
 * ======================================================================== */

/** @brief Virtual IRQ ID bits [31:0] */
#define ICH_LR_VINTID_MASK     0xFFFFFFFFULL

/** @brief Hardware IRQ bit (bit 41) — 1=物理中断转发 */
#define ICH_LR_HW_SHIFT        41U
#define ICH_LR_HW              (1ULL << ICH_LR_HW_SHIFT)

/** @brief Pending state (bit 62) */
#define ICH_LR_PENDING         (1ULL << 62U)

/** @brief Valid (bit 63) — 1=此 LR 有效 */
#define ICH_LR_VALID           (1ULL << 63U)

/** @brief Physical IRQ ID (bits [51:32]) — 与 HW=1 配合 */
#define ICH_LR_PINTID_SHIFT    32U
#define ICH_LR_PINTID_MASK     0xFFFFULL

/* ========================================================================
 * 数据结构
 * ======================================================================== */

/**
 * @brief Guest 虚拟中断状态
 */
typedef struct
{
    bool     in_use;               /**< @brief 是否活跃 */
    uint32_t pending_irq;          /**< @brief 待注入的虚拟中断号（0=无） */
    uint32_t max_lrs;              /**< @brief 最大 List Register 数量 */
} vgic_vm_t;

#ifndef CONFIG_MAX_GUESTS
#define CONFIG_MAX_GUESTS 4U
#endif

/** @brief 每 Guest 的 VGIC 状态 */
static vgic_vm_t s_vgic_vms[CONFIG_MAX_GUESTS];

/** @brief 最大 LR 数量（从硬件读取） */
static uint32_t s_max_lrs;

/* ========================================================================
 * 公共 API
 * ======================================================================== */

void vgic_init(void)
{
    uint32_t i;

    /* 读取硬件支持的 LR 数量 */
    uint64_t vtr = vgic_read_vtr();
    s_max_lrs = (uint32_t)(vtr & 0xFFULL) + 1U;
    if (s_max_lrs > 8U)
    {
        s_max_lrs = 8U;  /* 限制最多 8 个 LR */
    }

    for (i = 0U; i < CONFIG_MAX_GUESTS; i++)
    {
        s_vgic_vms[i].in_use = false;
        s_vgic_vms[i].pending_irq = 0U;
        s_vgic_vms[i].max_lrs = s_max_lrs;
    }
}

int32_t vgic_create_vm(uint32_t vm_id)
{
    if (vm_id >= CONFIG_MAX_GUESTS)
    {
        return -(int32_t)EINVAL;
    }

    s_vgic_vms[vm_id].in_use = true;
    s_vgic_vms[vm_id].pending_irq = 0U;

    return 0;
}

int32_t vgic_inject_irq(uint32_t vm_id, uint32_t virq)
{
    uint32_t lr;
    uint64_t lr_val;

    if ((vm_id >= CONFIG_MAX_GUESTS) || !s_vgic_vms[vm_id].in_use)
    {
        return -(int32_t)EINVAL;
    }

    /* 设置待注入中断 */
    s_vgic_vms[vm_id].pending_irq = virq;

    /* 构造 LR 值：vIRQ ID + Pending + Valid */
    lr_val = ((uint64_t)virq & ICH_LR_VINTID_MASK)
           | ICH_LR_PENDING
           | ICH_LR_VALID;

    /* 写入 LR0（简化：总是用 LR0） */
    lr = 0U;
    vgic_write_lr(lr, lr_val);

    return 0;
}

int32_t vgic_enable(uint32_t vm_id)
{
    (void)vm_id;

    /* 启用虚拟中断：
     * ICH_HCR_EL2 bit[0] = En（启用）
     * bit[6] = VGrp1EIE（Group1 EOI trap 到 EL2） */
    vgic_write_hcr(0x1ULL | (1ULL << 6U));

    return 0;
}

int32_t vgic_disable(uint32_t vm_id)
{
    (void)vm_id;

    /* 禁用虚拟中断 */
    vgic_write_hcr(0ULL);

    return 0;
}

int32_t vgic_destroy_vm(uint32_t vm_id)
{
    if (vm_id >= CONFIG_MAX_GUESTS)
    {
        return -(int32_t)EINVAL;
    }

    s_vgic_vms[vm_id].in_use = false;
    s_vgic_vms[vm_id].pending_irq = 0U;

    return 0;
}

uint32_t vgic_get_max_lrs(void)
{
    return s_max_lrs;
}
