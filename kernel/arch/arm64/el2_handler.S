/**
 * @file    el2_handler.S
 * @brief   EL2 Hypervisor 异常向量表 + HVC 处理
 * @author  AISafe64 Team
 * @date    2026-07-08
 * @version 1.0
 *
 * @details EL2 异常处理入口。当 Guest（EL1/EL0）触发异常或
 *          Host（EL1）执行 HVC 时，CPU 进入 EL2 执行此代码。
 *
 *          HVC 调用约定：
 *          x0 = 参数（如 VTTBR 值）
 *          x1 = 操作码（S2_HVC_SWITCH_VM / S2_HVC_RUN_GUEST / ...）
 *
 * @note    QNX 方案：EL2 hypervisor + EL1 Host + EL1/EL0 Guest
 *
 * @revision history
 * v1.0 2026-07-08 初始版本
 */

.section .text.boot
.align 11

/* ========================================================================
 * HVC 操作码
 * ======================================================================== */
#define HVC_SWITCH_VM    1   /* 切换 Guest stage-2 页表 */
#define HVC_RUN_GUEST    2   /* 进入 Guest（eret 到 EL1） */
#define HVC_RETURN_HOST  3   /* 返回 Host */

/* ========================================================================
 * EL2 异常向量表
 *
 * 16 个 slot，每个 128 字节（2KB 对齐）
 * 与 EL1 异常向量表格式相同
 * ======================================================================== */
.global el2_vector_table
el2_vector_table:
    /* [0] Current EL with SP0 */
    b .Lel2_sync_sp0          /* Sync */
    .align 7
    b .Lel2_irq_sp0            /* IRQ */
    .align 7
    b .Lel2_fiq_sp0            /* FIQ */
    .align 7
    b .Lel2_serror_sp0         /* SError */
    .align 7

    /* [4] Current EL with SPx */
    b .Lel2_sync_spx           /* Sync (HVC from EL1) */
    .align 7
    b .Lel2_irq_spx            /* IRQ */
    .align 7
    b .Lel2_fiq_spx            /* FIQ */
    .align 7
    b .Lel2_serror_spx         /* SError */
    .align 7

    /* [8] Lower EL (EL0) AArch64 */
    b .Lel2_sync_lower         /* Sync (Guest EL0 exception) */
    .align 7
    b .Lel2_irq_lower          /* IRQ (Guest EL0 IRQ) */
    .align 7
    b .Lel2_fiq_lower          /* FIQ */
    .align 7
    b .Lel2_serror_lower       /* SError */
    .align 7

    /* [12] Lower EL (EL1) AArch64 — Guest EL1 exception */
    b .Lel2_sync_lower         /* Sync (Guest EL1 HVC/exception) */
    .align 7
    b .Lel2_irq_lower          /* IRQ (Guest EL1 IRQ) */
    .align 7
    b .Lel2_fiq_lower          /* FIQ */
    .align 7
    b .Lel2_serror_lower       /* SError */
    .align 7

/* ========================================================================
 * EL2 Sync Handler (HVC from Host EL1)
 *
 * Host 通过 HVC 调用 EL2 服务。x1 = 操作码。
 * ======================================================================== */
.Lel2_sync_spx:
    /* 保存 x0-x3 到栈 */
    stp     x0, x1, [sp, #-16]!
    stp     x2, x3, [sp, #-16]!

    /* 读取 ESR_EL2 获取异常原因 */
    mrs     x2, esr_el2
    lsr     x2, x2, #26         /* EC = ESR[31:26] */
    cmp     x2, #0x16           /* EC=0x16 = HVC */
    b.ne    .Lel2_unknown

    /* 读取 x1（操作码）—— x1 在栈上 */
    ldr     x1, [sp, #8]        /* x1 在第二个 stp 的位置 */

    /* 根据 x1 操作码分发 */
    cmp     x1, #HVC_SWITCH_VM
    b.eq    .Lhvc_switch_vm

    cmp     x1, #HVC_RUN_GUEST
    b.eq    .Lhvc_run_guest

    /* 未知操作码：返回错误 */
    b       .Lhvc_done

.Lhvc_switch_vm:
    /* x0 = VTTBR_EL2 值（Guest PGD 物理地址 | VMID） */
    msr     vttbr_el2, x0
    isb
    /* 刷新 stage-2 TLB */
    tlbi    ipas2e1is
    dsb     ish
    isb
    /* 返回成功 */
    mov     x0, #0
    b       .Lhvc_done

.Lhvc_run_guest:
    /* 进入 Guest：
     * 1. 设置 SPSR_EL2 = Guest 的 PSTATE（EL1h, IRQ 使能）
     * 2. 设置 ELR_EL2 = Guest 入口地址（x0）
     * 3. eret 到 Guest EL1
     *
     * 注意：此处简化——完整实现需要保存 Host EL1 上下文
     */
    /* x0 = Guest 入口地址 */
    msr     elr_el2, x0

    /* SPSR_EL2 = EL1h + DAIF 清除（Guest 可执行） */
    mov     x0, #0x3C5          /* EL1h, DAIF 全部掩码 */
    msr     spsr_el2, x0

    /* 恢复 x0-x3（不需要了，Guest 用自己的寄存器） */
    ldp     x2, x3, [sp], #16
    ldp     x0, x1, [sp], #16

    /* eret 进入 Guest EL1 */
    eret

.Lhvc_done:
    /* 恢复寄存器（x0 保留返回值） */
    ldp     x2, x3, [sp], #16
    /* x0 是返回值，不恢复 */
    add     sp, sp, #8          /* 跳过 x1 */
    ldp     x0, x1, [sp], #16   /* 但需要恢复 x1... */
    /* 简化：直接恢复全部并 eret 返回 Host EL1 */
    /* Host 的 ELR_EL2 在 HVC 时自动保存 */
    eret

.Lel2_unknown:
    /* 未知 EL2 异常：返回错误 */
    mov     x0, #-1
    b       .Lhvc_done

/* ========================================================================
 * EL2 IRQ Handler（Guest 中断 trap 到 EL2）
 *
 * Guest 运行时硬件中断到达 → trap 到 EL2
 * 保存 Guest 上下文 → 返回 Host 处理中断
 * ======================================================================== */
.Lel2_irq_spx:
.Lel2_irq_lower:
    /* Guest 被 IRQ 打断
     * SPSR_EL2 和 ELR_EL2 已保存 Guest 的 PSTATE 和 PC
     * 直接 eret 返回 Host EL1（Host 处理中断后重新 vcpu_run）
     *
     * 设置 x0 = 1（表示 IRQ trap） */
    mov     x0, #1
    eret

/* ========================================================================
 * EL2 Sync Handler for Lower EL（Guest 异常）
 *
 * Guest EL1/EL0 触发同步异常 → trap 到 EL2
 * ======================================================================== */
.Lel2_sync_lower:
    /* Guest 触发同步异常（如 Guest 内的缺页、非法指令等）
     * SPSR_EL2 和 ELR_EL2 已保存 Guest 上下文
     * 设置 x0 = 2（表示 Guest sync exception） */
    mov     x0, #2
    eret

/* ========================================================================
 * 其他异常处理（SP0 / SError / FIQ）
 * ======================================================================== */
.Lel2_sync_sp0:
.Lel2_irq_sp0:
.Lel2_fiq_sp0:
.Lel2_serror_sp0:
.Lel2_fiq_spx:
.Lel2_serror_spx:
.Lel2_fiq_lower:
.Lel2_serror_lower:
    /* 未处理：死循环 */
    wfe
    b       .
