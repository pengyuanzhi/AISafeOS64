/**
 * @file exception.S
 * @brief ARMv8-A 异常向量表
 * @author AISafe64 Team
 * @date 2026-04-04
 * @version 4.0
 *
 * @details ARMv8-A 异常向量表实现
 *          - 16 个异常向量入口
 *          - 每个向量仅含跳转指令（确保不超出 128 字节 slot）
 *          - 实际处理逻辑在独立的处理函数中
 *
 * @note ARMv8-A 要求向量表 2KB 对齐
 * @note 每个向量 slot 最多 128 字节（32 条指令）
 * @note 向量表内只放跳转指令，处理逻辑放在外部
 * @note 对应需求: KR-003（异常向量表管理）
 */

.section .text.boot
.align 11                          /* 2^11 = 2048 字节对齐 */
.global exception_vector_table

exception_vector_table:
    /* ================================================================
     * [0]  当前 EL，SP0，同步异常
     * ================================================================ */
    .align 7
    b       .Lexception_panic

    /* [1]  当前 EL，SP0，IRQ */
    .align 7
    b       .Lexception_panic

    /* [2]  当前 EL，SP0，FIQ */
    .align 7
    b       .Lexception_panic

    /* [3]  当前 EL，SP0，SError */
    .align 7
    b       .Lexception_panic

    /* ================================================================
     * [4]  当前 EL，SPx，同步异常 ← SVC 系统调用入口
     * ================================================================ */
    .align 7
    b       .Lsync_handler_spx

    /* [5]  当前 EL，SPx，IRQ */
    .align 7
    b       .Lirq_handler_spx

    /* [6]  当前 EL，SPx，FIQ */
    .align 7
    b       .Lexception_panic

    /* [7]  当前 EL，SPx，SError */
    .align 7
    b       .Lserror_handler_spx

    /* ================================================================
     * [8-11]  低 EL（AArch64）异常
     * ================================================================ */
    .align 7
    b       .Lexception_panic         /* [8]  同步 */
    .align 7
    b       .Lexception_panic         /* [9]  IRQ */
    .align 7
    b       .Lexception_panic         /* [10] FIQ */
    .align 7
    b       .Lexception_panic         /* [11] SError */

    /* ================================================================
     * [12-15] 低 EL（AArch32）异常
     * ================================================================ */
    .align 7
    b       .Lexception_panic         /* [12] 同步 */
    .align 7
    b       .Lexception_panic         /* [13] IRQ */
    .align 7
    b       .Lexception_panic         /* [14] FIQ */
    .align 7
    b       .Lexception_panic         /* [15] SError */

/* ========================================================================
 * 异常处理函数（位于向量表外部，无大小限制）
 * ======================================================================== */

/**
 * @brief EL1h 同步异常处理
 *
 * @details 保存所有通用寄存器，读取 ESR 判断异常类型：
 *          EC=0x15 (SVC64) → syscall_handler
 *          其他 → exception_sync_handler(esr, far, &elr, spsr)
 */
.Lsync_handler_spx:
    /* 保存所有通用寄存器 */
    stp     x0, x1, [sp, #-16]!
    stp     x2, x3, [sp, #-16]!
    stp     x4, x5, [sp, #-16]!
    stp     x6, x7, [sp, #-16]!
    stp     x8, x9, [sp, #-16]!
    stp     x10, x11, [sp, #-16]!
    stp     x12, x13, [sp, #-16]!
    stp     x14, x15, [sp, #-16]!
    stp     x16, x17, [sp, #-16]!
    stp     x18, x19, [sp, #-16]!
    stp     x20, x21, [sp, #-16]!
    stp     x22, x23, [sp, #-16]!
    stp     x24, x25, [sp, #-16]!
    stp     x26, x27, [sp, #-16]!
    stp     x28, x29, [sp, #-16]!
    str     x30, [sp, #-8]!

    /* 保存 ELR_EL1 和 SPSR_EL1 */
    mrs     x20, elr_el1
    mrs     x21, spsr_el1
    stp     x20, x21, [sp, #-16]!

    /* 读取 ESR_EL1 判断异常类型 */
    mrs     x20, esr_el1
    lsr     x21, x20, #26            /* EC = ESR[31:26] */

    /* EC = 0x15 (21) = SVC 指令 */
    cmp     x21, #0x15
    b.eq    .Lsvc_handler

    /* 其他同步异常 → exception_sync_handler(esr, far, &elr, spsr) */
    mrs     x1, far_el1              /* x1 = FAR */
    mov     x0, x20                  /* x0 = ESR */
    mov     x2, sp                   /* x2 = 指向栈上 ELR 的指针 */
    ldr     x3, [sp, #8]             /* x3 = SPSR */
    bl      exception_sync_handler

    /* 恢复并返回 */
    b       .Lsync_restore

.Lsvc_handler:
    /* SVC 系统调用处理 */
    /* x8 = 系统调用号，x0-x7 = 参数 */
    /*
     * 在栈上分配 syscall_frame_t (9 个 uint64_t = 72 字节)
     * 从已保存的寄存器区域复制 x0-x8 到 frame
     *
     * syscall_frame_t layout:
     *   offset 0:  x0 (arg0 / return value)
     *   offset 8:  x1
     *   offset 16: x2
     *   offset 24: x3
     *   offset 32: x4
     *   offset 40: x5
     *   offset 48: x6
     *   offset 56: x7
     *   offset 64: x8 (syscall number)
     */
    sub     sp, sp, #72        /* 分配 72 字节给 syscall_frame_t */

    /* 从已保存区域加载寄存器并存储到 frame */
    /* 已保存的寄存器位于 sp+72 (原始 sp) 偏移处 */
    ldp     x9, x10, [sp, #72+0xF8]  /* x0,x1 at orig_sp+0xF8 */
    stp     x9, x10, [sp, #0]        /* frame->x0, frame->x1 */

    ldp     x9, x10, [sp, #72+0xE8]  /* x2,x3 at orig_sp+0xE8 */
    stp     x9, x10, [sp, #16]       /* frame->x2, frame->x3 */

    ldp     x9, x10, [sp, #72+0xD8]  /* x4,x5 at orig_sp+0xD8 */
    stp     x9, x10, [sp, #32]       /* frame->x4, frame->x5 */

    ldp     x9, x10, [sp, #72+0xC8]  /* x6,x7 at orig_sp+0xC8 */
    stp     x9, x10, [sp, #48]       /* frame->x6, frame->x7 */

    ldr     x9, [sp, #72+0xB8]       /* x8 at orig_sp+0xB8 */
    str     x9, [sp, #64]            /* frame->x8 */

    /* 将 frame 指针传给 syscall_handler(frame) */
    mov     x0, sp
    bl      syscall_handler

    /* 从 frame->x0 取返回值，写回已保存区域 */
    ldr     x9, [sp, #0]             /* frame->x0 = return value */
    str     x9, [sp, #72+0xF8]       /* 写回 saved x0 位置 */

    /* 释放 frame */
    add     sp, sp, #72

.Lsync_restore:
    /* 恢复 ELR_EL1 和 SPSR_EL1 */
    ldp     x20, x21, [sp], #16
    msr     elr_el1, x20
    msr     spsr_el1, x21

    /* 恢复所有通用寄存器 */
    ldr     x30, [sp], #8
    ldp     x28, x29, [sp], #16
    ldp     x26, x27, [sp], #16
    ldp     x24, x25, [sp], #16
    ldp     x22, x23, [sp], #16
    ldp     x20, x21, [sp], #16
    ldp     x18, x19, [sp], #16
    ldp     x16, x17, [sp], #16
    ldp     x14, x15, [sp], #16
    ldp     x12, x13, [sp], #16
    ldp     x10, x11, [sp], #16
    ldp     x8, x9, [sp], #16
    ldp     x6, x7, [sp], #16
    ldp     x4, x5, [sp], #16
    ldp     x2, x3, [sp], #16
    ldp     x0, x1, [sp], #16

    eret

/**
 * @brief EL1h IRQ 异常处理
 *
 * @details 保存 x0-x3，调用 irq_handler（C 函数），
 *          恢复 x0-x3，eret 返回。
 */
.Lirq_handler_spx:
    /* 保存通用寄存器 */
    stp     x0, x1, [sp, #-16]!
    stp     x2, x3, [sp, #-16]!

    /* 调用 IRQ 处理函数（C 语言入口） */
    bl      irq_handler

    /* 恢复通用寄存器 */
    ldp     x2, x3, [sp], #16
    ldp     x0, x1, [sp], #16
    eret

/**
 * @brief EL1h SError 异常处理
 */
.Lserror_handler_spx:
    /* 保存通用寄存器 */
    stp     x0, x1, [sp, #-16]!
    stp     x2, x3, [sp, #-16]!
    stp     x4, x5, [sp, #-16]!
    stp     x6, x7, [sp, #-16]!

    /* 读取异常信息寄存器 */
    mrs     x0, esr_el1              /* x0 = ESR */
    mrs     x1, far_el1              /* x1 = FAR */
    mrs     x2, elr_el1              /* x2 = ELR */

    /* 调用 SError 处理函数 */
    bl      el1_serror_handler

    /* SError 处理函数不返回（挂起），若返回则 panic */
    ldp     x6, x7, [sp], #16
    ldp     x4, x5, [sp], #16
    ldp     x2, x3, [sp], #16
    ldp     x0, x1, [sp], #16
    b       .Lexception_panic

/* ========== 异常默认处理 ========== */

.Lexception_panic:
    /* 内核异常 panic：永停 */
    wfe
    b       .Lexception_panic
