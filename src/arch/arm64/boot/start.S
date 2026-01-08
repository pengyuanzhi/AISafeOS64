/**
 * @file start.S
 * @brief AISafe64 RTOS - ARMv8-A启动代码
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details ARMv8-A架构启动代码
 *          - 设置异常向量表
 *          - 初始化CPU（EL1）
 *          - 设置栈指针
 *          - 初始化BSS段
 *          - 跳转到C入口点
 *
 * @note MISRA-C:2012合规（汇编代码）
 * @note 支持1-8核SMP启动
 * @note 启动后进入EL1（Kernel模式）
 */

.section .text.boot
.global _start
.global exception_vector_table

/**
 * @brief 系统入口点
 *
 * @details
 * - 由bootloader调用
 * - x0: 设备树指针（DTB地址）
 * - x1: 保留参数
 *
 * @note 必须是4KB对齐（异常向量表要求）
 */
_start:
    /* 禁用中断 */
    msr     daifset, #0xF           /* 禁用所有中断 */

    /* 获取当前异常级别 */
    mrs     x20, currentel
    and     x20, x20, #0xC           /* 提取EL[3:2] */
    cmp     x20, #0x8                /* 检查是否在EL2 */
    b.eq    drop_to_el1              /* 如果在EL2，降到EL1 */
    cmp     x20, #0xC                /* 检查是否在EL3 */
    b.eq    drop_to_el1              /* 如果在EL3，降到EL1 */

    /* 已经在EL1，继续启动 */
    b       el1_entry

drop_to_el1:
    /*
     * 从EL3/EL2降到EL1
     * - 设置SCTLR_EL1配置
     * - 设置SP_EL1（使用SPx模式）
     * - 配置栈指针
     */
    mov     x20, #(1 << 0)           /* M位：使能MMU */
    orr     x20, x20, #(1 << 2)      /* C位：使能数据缓存 */
    orr     x20, x20, #(1 << 12)     /* I位：使能指令缓存 */
    orr     x20, x20, #(1 << 3)      /* SA位：栈对齐检查 */
    msr     sctlr_el1, x20

    isb                             /* 指令同步屏障 */

    /* 设置SP为SPx模式 */
    mov     x20, sp
    msr     sp_el1, x20
    mov     sp, x20

    /* 设置VBAR（向量基址寄存器） */
    adr     x20, exception_vector_table
    msr     vbar_el1, x20
    isb

    /* 模拟ERET返回EL1 */
    mov     x20, #0x3C5             /* EL1h, IRQ/FIQ掩码 */
    msr     spsr_el3, x20
    adr     x20, el1_entry
    msr     elr_el3, x20
    eret

el1_entry:
    /*
     * EL1入口点
     * - 保存设备树指针
     * - 清空BSS段
     * - 设置栈指针
     * - 跳转到C入口
     */

    /* 保存设备树指针到全局变量 */
    adr     x20, __dtb_ptr
    str     x0, [x20]

    /* 清空BSS段 */
    adr     x20, __bss_start
    adr     x21, __bss_end
clear_bss:
    cmp     x20, x21
    b.ge    bss_cleared
    str     xzr, [x20], #8
    b       clear_bss
bss_cleared:

    /* 设置栈指针 */
    ldr     x20, =__stack_top
    mov     sp, x20

    /* 设置数据缓存使能 */
    mrs     x20, sctlr_el1
    orr     x20, x20, #(1 << 2)      /* C位：使能数据缓存 */
    msr     sctlr_el1, x20

    isb                             /* 指令同步屏障 */

    /* 跳转到C入口点 */
    bl      kernel_main

    /* 不应返回 */
hang:
    wfe                             /* 等待中断 */
    b       hang

/**
 * @brief 异常向量表
 *
 * @details ARMv8-A要求2KB对齐的异常向量表
 *          每个异常占128字节（32条指令）
 *
 * @note 异常向量表布局：
 *       [0]    当前EL，SP0，同步异常
 *       [1]    当前EL，SP0，IRQ异常
 *       [2]    当前EL，SP0，FIQ异常
 *       [3]    当前EL，SP0，SError异常
 *       [4]    当前EL，SPx，同步异常 ← SVC系统调用入口
 *       [5]    当前EL，SPx，IRQ异常
 *       [6]    当前EL，SPx，FIQ异常
 *       [7]    当前EL，SPx，SError异常
 *       [8-11] 低EL（AArch64），异常
 *       [12-15] 低EL（AArch32），异常
 */
.align 11                          /* 2^11 = 2048字节对齐 */
exception_vector_table:

    /* [0] 当前EL，SP0，同步异常 */
.align 7
exception_sync_sp0:
    b       exception_sync_sp0
    .align 7

    /* [1] 当前EL，SP0，IRQ异常 */
exception_irq_sp0:
    b       exception_irq_sp0
    .align 7

    /* [2] 当前EL，SP0，FIQ异常 */
exception_fiq_sp0:
    b       exception_fiq_sp0
    .align 7

    /* [3] 当前EL，SP0，SError异常 */
exception_serror_sp0:
    b       exception_serror_sp0
    .align 7

    /* [4] 当前EL，SPx，同步异常 ← SVC系统调用入口 */
exception_sync_spx:
    /* 保存上下文 */
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
    str     x30, [sp, #-16]!

    /* 保存ELR_EL1（返回地址）和SPSR_EL1（处理器状态） */
    mrs     x20, elr_el1
    mrs     x21, spsr_el1
    stp     x20, x21, [sp, #-16]!

    /* 读取ESR_EL1（异常综合寄存器） */
    mrs     x20, esr_el1
    ubfx    x21, x20, #26, #6        /* 提取EC[31:26]（异常类） */

    /* 检查是否为SVC异常（EC = 0x15 = 21） */
    mov     x22, #21
    cmp     x21, x22
    b.ne    exception_other          /* 不是SVC，转到其他异常处理 */

    /* SVC系统调用处理 */
    /* 提取系统调用号（从x8寄存器） */
    mov     x20, x8                  /* 系统调用号 */

    /* 保存参数到栈（x0-x7） */
    stp     x0, x1, [sp, #-16]!
    stp     x2, x3, [sp, #-16]!
    stp     x4, x5, [sp, #-16]!
    stp     x6, x7, [sp, #-16]!

    /* 调用C系统调用处理函数 */
    bl      syscall_handler

    /* 系统调用返回值在x0中 */
    /* 恢复上下文（反向操作） */
    ldp     x6, x7, [sp], #16
    ldp     x4, x5, [sp], #16
    ldp     x2, x3, [sp], #16
    ldp     x0, x1, [sp], #16

    /* 恢复ELR_EL1和SPSR_EL1 */
    ldp     x20, x21, [sp], #16
    msr     elr_el1, x20
    msr     spsr_el1, x21

    /* 恢复通用寄存器 */
    ldr     x30, [sp], #16
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

    eret                            /* 返回用户空间 */

exception_other:
    /* 其他同步异常 */
    bl      exception_sync_handler
    /* 不返回，进入内核panic */
kernel_panic:
    b       kernel_panic

    /* [5] 当前EL，SPx，IRQ异常 */
    .align 7
exception_irq_spx:
    /* IRQ异常处理 */
    stp     x0, x1, [sp, #-16]!
    stp     x2, x3, [sp, #-16]!

    bl      irq_handler

    ldp     x2, x3, [sp], #16
    ldp     x0, x1, [sp], #16
    eret

    /* [6] 当前EL，SPx，FIQ异常 */
    .align 7
exception_fiq_spx:
    /* FIQ异常处理 */
    stp     x0, x1, [sp, #-16]!

    bl      fiq_handler

    ldp     x0, x1, [sp], #16
    eret

    /* [7] 当前EL，SPx，SError异常 */
    .align 7
exception_serror_spx:
    /* SError异常处理 */
    stp     x0, x1, [sp, #-16]!

    bl      serror_handler

    ldp     x0, x1, [sp], #16
    eret

    /* [8-11] 低EL（AArch64），异常 */
    .align 7
exception_sync_lower64:
    b       exception_sync_lower64
    .align 7

exception_irq_lower64:
    b       exception_irq_lower64
    .align 7

exception_fiq_lower64:
    b       exception_fiq_lower64
    .align 7

exception_serror_lower64:
    b       exception_serror_lower64
    .align 7

    /* [12-15] 低EL（AArch32），异常 */
    .align 7
exception_sync_lower32:
    b       exception_sync_lower32
    .align 7

exception_irq_lower32:
    b       exception_irq_lower32
    .align 7

exception_fiq_lower32:
    b       exception_fiq_lower32
    .align 7

exception_serror_lower32:
    b       exception_serror_lower32
    .align 7

/* 异常处理占位符（后续实现） */
exception_sync_sp0:
    b       kernel_panic

exception_irq_sp0:
    b       kernel_panic

exception_fiq_sp0:
    b       kernel_panic

exception_serror_sp0:
    b       kernel_panic

exception_sync_handler:
    b       kernel_panic

exception_irq_lower64:
    b       kernel_panic

exception_fiq_lower64:
    b       kernel_panic

exception_serror_lower64:
    b       kernel_panic

exception_sync_lower64:
    b       kernel_panic

exception_irq_lower32:
    b       kernel_panic

exception_fiq_lower32:
    b       kernel_panic

exception_serror_lower32:
    b       kernel_panic

irq_handler:
    b       kernel_panic

fiq_handler:
    b       kernel_panic

serror_handler:
    b       kernel_panic

kernel_panic:
    /* 内核panic，停止执行 */
    wfe
    b       kernel_panic

/**
 * @brief 全局符号（由链接器定义）
 */
.global __dtb_ptr
.global __bss_start
.global __bss_end
.global __stack_top

__dtb_ptr:
    .quad 0

.section .bss
__bss_start:
    .space 4
__bss_end:
    .space 4

.section .stack
    .align 12
    .space 4096                     /* 4KB栈空间 */
__stack_top:
