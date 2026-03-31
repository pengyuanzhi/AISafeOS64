/**
 * @file exception.S
 * @brief ARMv8-A 异常向量表
 * @author AISafe64 Team
 * @date 2026-03-31
 * @version 2.0
 *
 * @details ARMv8-A 异常向量表实现
 *          - 16 个异常向量入口
 *          - SVC 系统调用处理
 *          - IRQ 中断处理
 *          - 其他异常默认处理
 *
 * @note ARMv8-A 要求向量表 2KB 对齐
 * @note 每个向量占 128 字节（32 条指令）
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
exception_sync_spx:
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

    /* EC = 0x00 (0) = 未知原因 */
    /* 其他同步异常 → panic */
    mov     x0, x20                  /* ESR 值作为参数 */
    bl      exception_sync_handler
    b       .Lexception_panic

.Lsvc_handler:
    /* SVC 系统调用处理 */
    /* x8 = 系统调用号，x0-x7 = 参数 */
    bl      syscall_handler

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

    /* ================================================================
     * [5]  当前 EL，SPx，IRQ 异常
     * ================================================================ */
    .align 7
exception_irq_spx:
    /* 保存上下文 */
    stp     x0, x1, [sp, #-16]!
    stp     x2, x3, [sp, #-16]!

    /* 调用 IRQ 处理函数 */
    bl      irq_handler

    /* 恢复上下文 */
    ldp     x2, x3, [sp], #16
    ldp     x0, x1, [sp], #16
    eret

    /* ================================================================
     * [6]  当前 EL，SPx，FIQ 异常
     * ================================================================ */
    .align 7
exception_fiq_spx:
    b       .Lexception_panic

    /* ================================================================
     * [7]  当前 EL，SPx，SError 异常
     * ================================================================ */
    .align 7
exception_serror_spx:
    b       .Lexception_panic

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

/* ========== 异常默认处理 ========== */

.Lexception_panic:
    /* 内核异常 panic：永停 */
    wfe
    b       .Lexception_panic

/* ========== 异常处理 C 函数声明 ========== */
/* 这些函数在 entry.c 或后续模块中实现 */

/**
 * @brief IRQ 中断处理函数（C 语言入口）
 */
.weak irq_handler
irq_handler:
    ret

/**
 * @brief 同步异常处理函数
 */
.weak exception_sync_handler
exception_sync_handler:
    ret

/**
 * @brief 系统调用处理函数
 * @param x8 系统调用号
 * @param x0-x7 系统调用参数
 * @return 返回值在 x0
 */
.weak syscall_handler
syscall_handler:
    ret
