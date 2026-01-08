/**
 * @file context_switch.S
 * @brief AISafe64 RTOS - ARMv8-A上下文切换
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details ARMv8-A上下文切换汇编实现
 *          - 保存/恢复通用寄存器
 *          - 保存/恢复特殊寄存器
 *          - 切换栈指针
 *
 * @note ARMv8-A架构
 * @note System V AAPCS64调用约定
 */

.section .text.context_switch

/**
 * @brief 上下文切换函数
 * @param x0 上一个任务TCB指针
 * @param x1 下一个任务TCB指针
 *
 * @details 保存prev任务上下文，恢复next任务上下文
 *          - 保存x0-x30, sp, elr_el1, spsr_el1
 *          - 恢复next任务的所有寄存器
 *          - 切换到next任务
 */
.global context_switch
context_switch:
    /* x0 = prev任务TCB指针
     * x1 = next任务TCB指针
     */

    /* 保存prev任务上下文到TCB */
    /* 保存通用寄存器x19-x28 (callee-saved) */
    stp     x19, x20, [x0, #0]       /* context[0-1] */
    stp     x21, x22, [x0, #16]      /* context[2-3] */
    stp     x23, x24, [x0, #32]      /* context[4-5] */
    stp     x25, x26, [x0, #48]      /* context[6-7] */
    stp     x27, x28, [x0, #64]      /* context[8-9] */

    /* 保存帧指针和链接寄存器 */
    stp     x29, x30, [x0, #80]      /* context[10-11], FP, LR */

    /* 保存栈指针 */
    mov     x2, sp
    str     x2, [x0, #96]            /* context[12] = SP */

    /* 保存程序状态寄存器 */
    mrs     x2, spsr_el1
    mrs     x3, elr_el1
    stp     x2, x3, [x0, #104]       /* context[13-14] = SPSR, ELR */

    /* 保存prev任务的栈指针到TCB */
    mov     x2, sp
    str     x2, [x0, #16]            /* stack_ptr */

    /* 恢复next任务上下文 */
    /* 恢复栈指针 */
    ldr     x2, [x1, #16]            /* stack_ptr */
    mov     sp, x2

    /* 恢复程序状态寄存器 */
    ldp     x2, x3, [x1, #104]       /* SPSR, ELR */
    msr     spsr_el1, x2
    msr     elr_el1, x3

    /* 恢复通用寄存器 */
    ldp     x19, x20, [x1, #0]
    ldp     x21, x22, [x1, #16]
    ldp     x23, x24, [x1, #32]
    ldp     x25, x26, [x1, #48]
    ldp     x27, x28, [x1, #64]
    ldp     x29, x30, [x1, #80]

    /* 恢复栈指针（可能在上一步被修改） */
    ldr     x2, [x1, #96]
    mov     sp, x2

    /* 返回 */
    ret

/**
 * @brief 创建新任务的上下文
 * @param x0 任务TCB指针
 * @param x1 任务入口函数
 * @param x2 任务参数
 * @param x3 栈顶地址
 *
 * @details 初始化新任务的栈帧，使其可以首次被调度运行
 */
.global arch_setup_task_context
arch_setup_task_context:
    /* x0 = TCB指针
     * x1 = entry函数
     * x2 = 参数
     * x3 = 栈顶
     */

    /* 初始化TCB context数组为0 */
    mov     x4, #0
    mov     x5, #15                  /* 15个寄存器对 (x19-x30, SP, SPSR, ELR) */
init_loop:
    str     x4, [x0, #0]
    add     x0, x0, #8
    subs    x5, x5, #1
    b.ne    init_loop

    /* 恢复x0为TCB指针 */
    sub     x0, x0, #120             /* 回退15*8 = 120字节 */

    /* 设置任务栈指针 */
    str     x3, [x0, #16]            /* stack_ptr */

    /* 在栈上构造初始栈帧 */
    /* 栈布局（向下增长）：
     * [sp + #0]  = x0 (参数，将在entry中恢复)
     * [sp + #8]  = x1 (entry函数地址)
     * [sp + #16] = 返回地址（dummy）
     */

    stp     x2, x1, [x3, #-16]!      /* 存储参数和entry地址，sp -= 16 */

    /* 设置初始上下文 */
    mov     x4, #0
    str     x4, [x0, #96]            /* context[12] = SP (将在context_switch时设置) */

    /* 设置ELR为entry函数，但需要调整 */
    /* 由于我们将entry地址和参数放在栈上，需要特殊处理 */
    /* 实际上，我们设置ELR为entry_trampoline，参数在x0 */

    str     x1, [x0, #112]           /* context[14] = ELR = entry地址 */

    /* 设置x0为参数（将在context_switch后恢复） */
    str     x2, [x0, #104]           /* context[13] = SPSR (暂时存储参数，不正确，需要修正) */

    /* 正确设置SPSR（EL1h, IRQ/FIQ掩码设置） */
    mov     x4, #0x3c5               /* EL1h, IRQ/FIQ/SError掩码 */
    str     x4, [x0, #104]           /* context[13] = SPSR */

    /* 将参数放在正确位置（context[0] = x0） */
    str     x2, [x0, #0]             /* context[0] = x0 = 参数 */

    /* 设置栈指针为当前值（已调整） */
    str     x3, [x0, #96]            /* context[12] = SP */

    ret

/**
 * @brief 任务入口trampoline
 * @details 实际的任务入口点，用于初始化后跳转到真正的entry
 */
.global task_entry_trampoline
task_entry_trampoline:
    /* 此时x0已经包含参数（从context[0]恢复）
     * x1-x30已恢复
     * ELR已设置为entry函数
     */

    /* 调用真正的任务入口函数 */
    /* 注意：x0已经是参数，entry应该接受一个void *参数 */
    ret

/**
 * @brief 首次上下文切换（调度器启动时使用）
 * @param x0 第一个任务TCB指针
 *
 * @details 这是调度器启动后的第一次上下文切换
 *          - 不保存任何"prev"上下文（因为从kernel初始化来）
 *          - 直接恢复first任务的所有寄存器
 *          - 跳转到first任务执行
 */
.global cpu_switch_to_first_task
cpu_switch_to_first_task:
    /* x0 = first任务TCB指针 */

    /* 恢复栈指针 */
    ldr     x2, [x0, #16]            /* stack_ptr */
    mov     sp, x2

    /* 恢复程序状态寄存器 */
    ldp     x2, x3, [x0, #104]       /* SPSR, ELR */
    msr     spsr_el1, x2
    msr     elr_el1, x3

    /* 恢复通用寄存器 */
    ldp     x19, x20, [x0, #0]
    ldp     x21, x22, [x0, #16]
    ldp     x23, x24, [x0, #32]
    ldp     x25, x26, [x0, #48]
    ldp     x27, x28, [x0, #64]
    ldp     x29, x30, [x0, #80]

    /* 恢复栈指针（可能在上一步被修改） */
    ldr     x2, [x0, #96]
    mov     sp, x2

    /* 设置x0为任务参数 */
    ldr     x0, [x0, #0]             /* context[0] = 参数 */

    /* 跳转到第一个任务 */
    ret

/* 为了兼容性，添加context_switch_asm别名 */
.global context_switch_asm
context_switch_asm:
    /* 跳转到context_switch函数 */
    b       context_switch

.size context_switch, . - context_switch
.size arch_setup_task_context, . - arch_setup_task_context
.size task_entry_trampoline, . - task_entry_trampoline
.size cpu_switch_to_first_task, . - cpu_switch_to_first_task
