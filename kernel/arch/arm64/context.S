/**
 * @file    context.S
 * @brief   ARM64 上下文切换汇编实现
 * @author  AISafe64 Team
 * @date    2026-03-31
 * @version 2.0
 *
 * @details 实现线程上下文保存和恢复：
 *          - context_switch: 保存 prev / 恢复 next
 *          - arch_setup_thread_context: 初始化新线程上下文
 *          - cpu_switch_to_first_task: 切换到第一个任务（无 prev 保存）
 *
 *          上下文保存区布局（context[] 数组，15 个 uint64_t）：
 *          [0]  x19    [1]  x20    [2]  x21    [3]  x22
 *          [4]  x23    [5]  x24    [6]  x25    [7]  x26
 *          [8]  x27    [9]  x28    [10] x29/FP [11] x30/LR
 *          [12] SP_el1 [13] SPSR_EL1 [14] ELR_EL1
 *
 * @note MISRA-C:2012 合规（汇编文件遵循 ARM64 ABI）
 * @note 对应需求: SC-002（上下文切换 < 5us）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

    .text
    .align 2

/* ========================================================================
 * context_switch(prev_ctx, next_ctx)
 *
 * x0 = prev->context (uint64_t *)
 * x1 = next->context (uint64_t *)
 * ======================================================================== */
    .global context_switch
    .type context_switch, @function
context_switch:
    /* ---- 保存 prev 上下文到 prev->context[] ---- */

    /* 保存 callee-saved 通用寄存器 x19 ~ x28 */
    stp     x19, x20, [x0, #0]
    stp     x21, x22, [x0, #16]
    stp     x23, x24, [x0, #32]
    stp     x25, x26, [x0, #48]
    stp     x27, x28, [x0, #64]

    /* 保存 FP(x29) 和 LR(x30) */
    stp     x29, x30, [x0, #80]

    /* 保存当前 SP */
    mov     x9, sp
    str     x9, [x0, #96]

    /* 保存 SPSR_EL1 和 ELR_EL1 */
    mrs     x9, spsr_el1
    str     x9, [x0, #104]
    mrs     x9, elr_el1
    str     x9, [x0, #112]

    /* ---- 恢复 next 上下文从 next->context[] ---- */
    /* x1 仍然指向 next->context */

    /* 恢复 callee-saved 通用寄存器 x19 ~ x28 */
    ldp     x19, x20, [x1, #0]
    ldp     x21, x22, [x1, #16]
    ldp     x23, x24, [x1, #32]
    ldp     x25, x26, [x1, #48]
    ldp     x27, x28, [x1, #64]

    /* 恢复 FP(x29) 和 LR(x30) */
    ldp     x29, x30, [x1, #80]

    /* 恢复 SP */
    ldr     x9, [x1, #96]
    mov     sp, x9

    /* 恢复 SPSR_EL1 和 ELR_EL1 */
    ldr     x9, [x1, #104]
    msr     spsr_el1, x9
    ldr     x9, [x1, #112]
    msr     elr_el1, x9

    /* 返回到 ELR_EL1 处继续执行 */
    ret


/* ========================================================================
 * arch_setup_thread_context(ctx, entry, arg, stack_top)
 *
 * x0 = ctx          (uint64_t *)
 * x1 = entry        (uint64_t) - 线程入口函数地址
 * x2 = arg          (uint64_t) - 入口函数参数
 * x3 = stack_top    (uint64_t) - 栈顶地址（已 16 字节对齐）
 *
 * 初始化上下文保存区，使线程首次被调度时：
 *   - x19 保存 entry（供 trampoline 使用）
 *   - x20 保存 arg（供 trampoline 使用）
 *   - LR/ELR 指向 thread_entry_trampoline
 *   - SP 指向 stack_top
 *   - SPSR_EL1 = 0x5（EL1h，IRQ 使能）
 * ======================================================================== */
    .global arch_setup_thread_context
    .type arch_setup_thread_context, @function
arch_setup_thread_context:
    /* context[0] = x19 = entry（真实入口地址） */
    str     x1, [x0, #0]

    /* context[1] = x20 = arg（入口参数） */
    str     x2, [x0, #8]

    /* 清零 x21 ~ x28 */
    str     xzr, [x0, #16]        /* context[2] = x21 = 0 */
    str     xzr, [x0, #24]        /* context[3] = x22 = 0 */
    str     xzr, [x0, #32]        /* context[4] = x23 = 0 */
    str     xzr, [x0, #40]        /* context[5] = x24 = 0 */
    str     xzr, [x0, #48]        /* context[6] = x25 = 0 */
    str     xzr, [x0, #56]        /* context[7] = x26 = 0 */
    str     xzr, [x0, #64]        /* context[8] = x27 = 0 */
    str     xzr, [x0, #72]        /* context[9] = x28 = 0 */

    /* context[10] = FP(x29) = 0 */
    str     xzr, [x0, #80]

    /* context[11] = LR(x30) = thread_entry_trampoline */
    ldr     x9, =thread_entry_trampoline
    str     x9, [x0, #88]

    /* context[12] = SP = stack_top */
    str     x3, [x0, #96]

    /* context[13] = SPSR_EL1 = 0x5（EL1h, IRQ 使能） */
    mov     x9, #0x5
    str     x9, [x0, #104]

    /* context[14] = ELR_EL1 = thread_entry_trampoline */
    ldr     x9, =thread_entry_trampoline
    str     x9, [x0, #112]

    ret


/* ========================================================================
 * thread_entry_trampoline
 *
 * 新线程首次被调度时的入口跳板。
 * context[0]（x19）保存了真实入口地址，context[1]（x20）保存了参数。
 * 将参数移到 x0，跳转到真实入口。
 * ======================================================================== */
    .global thread_entry_trampoline
    .type thread_entry_trampoline, @function
thread_entry_trampoline:
    mov     x0, x20            /* x0 = arg */
    br      x19                /* 跳转到 entry(arg) */


/* ========================================================================
 * cpu_switch_to_first_task(ctx)
 *
 * x0 = first_thread->context (uint64_t *)
 *
 * 首次切换到第一个任务，不需要保存 prev 上下文。
 * ======================================================================== */
    .global cpu_switch_to_first_task
    .type cpu_switch_to_first_task, @function
cpu_switch_to_first_task:
    /* 恢复 callee-saved 通用寄存器 */
    ldp     x19, x20, [x0, #0]
    ldp     x21, x22, [x0, #16]
    ldp     x23, x24, [x0, #32]
    ldp     x25, x26, [x0, #48]
    ldp     x27, x28, [x0, #64]
    ldp     x29, x30, [x0, #80]

    /* 恢复 SP */
    ldr     x9, [x0, #96]
    mov     sp, x9

    /* 恢复 SPSR_EL1 和 ELR_EL1 */
    ldr     x9, [x0, #104]
    msr     spsr_el1, x9
    ldr     x9, [x0, #112]
    msr     elr_el1, x9

    /* 确保所有系统寄存器写入完成 */
    isb

    /* 跳转到 ELR_EL1 处执行 */
    eret


/* ========================================================================
 * 标记栈不可执行（GNU note）
 * ======================================================================== */
    .section .note.GNU-stack, "", @progbits
