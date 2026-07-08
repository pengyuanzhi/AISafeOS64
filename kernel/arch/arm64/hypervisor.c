/**
 * @file    hypervisor.c
 * @brief   ARM EL2 Hypervisor 配置层
 * @author  AISafe64 Team
 * @date    2026-07-08
 * @version 1.0
 *
 * @details 本文件实现 ARM EL2 Hypervisor 的配置初始化。
 *
 *          架构设计：
 *          - boot.S 在 EL2 初始化 hypervisor 配置后降级到 EL1
 *          - 当前内核运行在 EL1（作为 Host/Guest 内核）
 *          - EL2 配置保留在系统寄存器中，为后续 Guest OS 管理做准备
 *
 *          EL2 关键寄存器配置：
 *          - HCR_EL2：Hypervisor 配置寄存器（trap 控制）
 *          - VTCR_EL2：虚拟化翻译控制寄存器（stage-2 页表格式）
 *          - CNTHCTL_EL2：定时器 trap 控制
 *          - VPIDR_EL2：虚拟处理器 ID
 *          - CPTR_EL2：协处理器 trap 控制
 *          - SCTLR_EL1：在 EL2 下通过 VHE 配置（如果支持）
 *
 *          Stage-2 页表（VTTBR_EL2）：
 *          - Guest 物理地址（IPA）→ Host 物理地址（PA）
 *          - 每个 Guest 有独立的 stage-2 页表
 *          - 由 hypervisor 管理（创建/销毁/切换）
 *
 * @note    当前为初始化壳，后续逐步完善 Guest OS 管理
 *
 * @revision history
 * v1.0 2026-07-08 初始版本（EL2 配置初始化）
 */

#include <stdint.h>
#include <stdbool.h>
#include "hal.h"

/* ========================================================================
 * EL2 系统寄存器操作（内联汇编）
 * ======================================================================== */

/**
 * @brief 读取 HCR_EL2
 *
 * @details Hypervisor 配置寄存器，控制 EL1/EL0 的 trap 行为。
 *          在 EL2 中可读写，在 EL1 中不可访问。
 *
 * @return HCR_EL2 当前值
 */
static inline uint64_t hyp_read_hcr(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, hcr_el2" : "=r"(val));
    return val;
}

/**
 * @brief 写入 HCR_EL2
 */
static inline void hyp_write_hcr(uint64_t val)
{
    __asm__ volatile("msr hcr_el2, %0" :: "r"(val));
    __asm__ volatile("isb");
}

/**
 * @brief 读取 VTCR_EL2
 *
 * @details 虚拟化翻译控制寄存器，控制 stage-2 页表格式。
 */
static inline uint64_t hyp_read_vtcr(void)
{
    uint64_t val;
    __asm__ volatile("mrs %0, vtcr_el2" : "=r"(val));
    return val;
}

/**
 * @brief 写入 VTCR_EL2
 */
static inline void hyp_write_vtcr(uint64_t val)
{
    __asm__ volatile("msr vtcr_el2, %0" :: "r"(val));
    __asm__ volatile("isb");
}

/* ========================================================================
 * HCR_EL2 位定义
 * ======================================================================== */

/** @brief 虚拟化使能（stage-2 翻译） */
#define HCR_VM_SHIFT        0U
#define HCR_VM              (1ULL << HCR_VM_SHIFT)

/** @brief Guest 读写 trap */
#define HCR_TRVR_SHIFT      1U

/** @brief Stage-2 不允许 MMIO 访问除非 stage-2 映射 */
#define HCR_PTW_SHIFT       27U

/** @brief Trap EL1 寄存器访问到 EL2 */
#define HCR_TGE_SHIFT       27U  /* Trap General Exceptions */

/** @brief Data Unified Cache trap */
#define HCR_DC_SHIFT        12U

/** @brief 默认 HCR_EL2 值（不 trap 任何 EL1 操作） */
#define HCR_DEFAULT         0ULL

/* ========================================================================
 * VTCR_EL2 配置常量
 * ======================================================================== */

/** @brief Stage-2 页表 T0SZ（48 位 IPA） */
#define VTCR_T0SZ_48BIT     (16ULL << 0U)

/** @brief Stage-2 页表 TG0（4KB 粒度） */
#define VTCR_TG0_4KB        (0ULL << 14U)

/** @brief Stage-2 SH0（Inner Shareable） */
#define VTCR_SH0_INNER      (3ULL << 22U)

/** @brief Stage-2 ORGN0（Outer Write-Back） */
#define VTCR_ORGN0_WB       (1ULL << 26U)

/** @brief Stage-2 IRGN0（Inner Write-Back） */
#define VTCR_IRGN0_WB       (1ULL << 20U)

/** @brief Stage-2 PS（物理地址大小，40 位 = 1TB） */
#define VTCR_PS_40BIT       (2ULL << 16U)

/** @brief 默认 VTCR_EL2 值 */
#define VTCR_DEFAULT        (VTCR_T0SZ_48BIT | VTCR_TG0_4KB \
                             | VTCR_SH0_INNER | VTCR_ORGN0_WB \
                             | VTCR_IRGN0_WB | VTCR_PS_40BIT)

/* ========================================================================
 * 公共 API
 * ======================================================================== */

/**
 * @brief 初始化 EL2 Hypervisor 配置
 *
 * @details 在 boot.S 中从 EL2 降级到 EL1 之前调用。
 *          配置 EL2 系统寄存器为安全默认值：
 *          - HCR_EL2 = 0（禁用所有 trap，stage-2 翻译关闭）
 *          - VTCR_EL2 = 默认格式（为后续 stage-2 准备）
 *          - CNTHCTL_EL2 = 允许 EL1 访问物理定时器
 *          - CPTR_EL2 = 允许 NEON/FP（不 trap）
 *
 *          这确保当前内核在 EL1 运行时不受 EL2 干扰。
 *          后续创建 Guest OS 时再启用 stage-2 和 trap。
 *
 * @note 必须在 EL2 中调用
 */
void hypervisor_init(void)
{
    /* HCR_EL2：禁用所有 trap 和 stage-2 翻译
     * VM=0：不启用 stage-2 翻译
     * TGE=0：不 trap 一般异常
     * 当前内核作为 Host 运行在 EL1，不需要任何 trap */
    hyp_write_hcr(HCR_DEFAULT);

    /* VTCR_EL2：配置 stage-2 页表格式（为 Guest 准备）
     * 即使 VM=0 也要正确配置，后续启用 stage-2 时立即可用 */
    hyp_write_vtcr(VTCR_DEFAULT);

    /* CNTHCTL_EL2：允许 EL1 访问物理定时器
     * EL1PHYS_TIMER (bit 0) = 0：不 trap EL1 物理定时器访问
     * EL1PHYS_CNT (bit 1) = 0：不 trap EL1 物理计数器访问 */
    __asm__ volatile("msr cnthctl_el2, %0" :: "r"(0ULL));

    /* CPTR_EL2：允许 FP/NEON（不 trap）
     * TCPAC (bit 31) = 0：不 trap CPACR
     * TTA (bit 20) = 0：不 trap 内存标签
     * TFP (bit 10) = 0：不 trap FP/NEON */
    __asm__ volatile("msr cptr_el2, %0" :: "r"(0ULL));

    /* VPIDR_EL2：设置虚拟 MIDR（Guest 看到的处理器 ID）
     * 设为 0，Guest 使用真实 MIDR */
    __asm__ volatile("msr vpidr_el2, %0" :: "r"(0ULL));

    /* VMPIDR_EL2：设置虚拟 MPIDR
     * 设为与真实 MPIDR 相同 */
    {
        uint64_t mpidr;
        __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
        __asm__ volatile("msr vmpidr_el2, %0" :: "r"(mpidr));
    }

    /*确保配置生效 */
    __asm__ volatile("isb");
}

/**
 * @brief 检查是否支持 EL2（VHE）
 *
 * @return true 当前在 EL2 运行（可以配置 hypervisor）
 * @return false 当前在 EL1（hypervisor 配置不可用）
 */
bool hypervisor_is_el2(void)
{
    uint64_t currentel;
    __asm__ volatile("mrs %0, currentel" : "=r"(currentel));
    return ((currentel & 0xCULL) == 0x8ULL);
}

/**
 * @brief 创建 Guest OS（占位）
 *
 * @details 后续实现：
 *          1. 分配 stage-2 页表
 *          2. 配置 Guest 内存映射
 *          3. 加载 Guest 内核镜像
 *          4. 创建 vCPU 上下文
 *          5. eret 到 Guest EL1 入口
 *
 * @note 当前为占位实现
 */
int32_t hypervisor_create_guest(void)
{
    /* TODO: stage-2 页表创建 + vCPU 上下文 */
    return -1;
}
