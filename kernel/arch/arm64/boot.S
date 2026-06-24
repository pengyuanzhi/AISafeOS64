/**
 * @file boot.S
 * @brief ARMv8-A 微内核启动汇编代码
 * @author AISafe64 Team
 * @date 2026-03-31
 * @version 2.0
 *
 * @details 微内核启动流程
 *          1. 从 EL3/EL2 降到 EL1
 *          2. 设置异常向量表
 *          3. 清零 BSS 段
 *          4. 设置内核栈（CPU 0）
 *          5. 跳转到 kernel_main C 入口
 *
 * @note 对应需求: KR-001（内核启动与初始化）
 */

.section .text.boot
.global kernel_entry

/**
 * @brief 内核入口点
 *
 * @details 由 bootloader（QEMU -kernel）调用
 *          x0: DTB 地址
 *          x1: 保留
 *          x2: 保留
 *          x3: 保留
 *
 * @note 必须是 4KB 对齐（异常向量表要求）
 */
.align 12
kernel_entry:
    /* ---- 第一步：禁用所有中断 ---- */
    msr     daifset, #0xF

    /* ---- 第二步：检查并降到 EL1 ---- */
    mrs     x20, currentel
    and     x20, x20, #0xC
    cmp     x20, #0xC                /* EL3? */
    b.eq    .Ldrop_from_el3
    cmp     x20, #0x8                /* EL2? */
    b.eq    .Ldrop_from_el2

    /* 已在 EL1，直接进入启动 */
    b       .Lel1_entry

/**
 * @brief 从 EL3 降到 EL1
 */
.Ldrop_from_el3:
    /* EL3 安全状态配置（暂时全部安全） */
    mov     x20, #0x3C5              /* EL1h, DAIF 全部掩码 */
    msr     spsr_el3, x20
    adr     x20, .Lel2_entry
    msr     elr_el3, x20
    eret

/**
 * @brief 从 EL2 降到 EL1
 */
.Lel2_entry:
.Ldrop_from_el2:
    /* 禁用 EL2 阶段翻译（HCR_EL2） */
    msr     hcr_el2, xzr

    /* 配置 SCTLR_EL1：禁用 MMU（后续 P0-3 启用） */
    mov     x20, #(1 << 12)          /* I 位：使能指令缓存 */
    orr     x20, x20, #(1 << 2)      /* C 位：使能数据缓存 */
    msr     sctlr_el1, x20

    /* 设置 EL1 返回状态 */
    mov     x20, #0x3C5              /* EL1h, DAIF 全部掩码 */
    msr     spsr_el2, x20

    /* 设置返回地址 */
    adr     x20, .Lel1_entry
    msr     elr_el2, x20

    eret

/**
 * @brief EL1 内核入口
 *
 * @details 执行内核初始化：
 *          - 保存 DTB 地址
 *          - 设置异常向量表
 *          - 清零 BSS 段
 *          - 设置 CPU 0 栈
 *          - 跳转到 C 入口
 */
.Lel1_entry:
    /* ---- 检查 CPU ID，从核跳转到等待循环 ---- */
    mrs     x20, mpidr_el1
    and     x20, x20, #0xFF
    cbnz    x20, .Lsecondary_wait

    /* ---- 保存 DTB 地址到全局变量 ---- */
    adr     x21, __dtb_ptr
    str     x0, [x21]

    /* ---- 设置异常向量表 ---- */
    adr     x20, exception_vector_table
    msr     vbar_el1, x20
    isb

    /* ---- 清零 BSS 段 ---- */
    ldr     x20, =__bss_start
    ldr     x21, =__bss_end
.Lclear_bss:
    cmp     x20, x21
    b.ge    .Lbss_done
    str     xzr, [x20], #8
    b       .Lclear_bss
.Lbss_done:

    /* ---- 设置 CPU 0 内核栈 ---- */
    ldr     x20, =__stacks_start
    add     x20, x20, #8192          /* CPU 0 栈顶 = stacks_start + 8KB */
    mov     sp, x20

    /* ---- 跳转到 C 入口 ---- */
    bl      kernel_main

    /* ---- 不应返回 ---- */
.Lhang:
    wfe
    b       .Lhang

/* ========================================================================
 * 从核等待循环（PSCI 唤醒前）
 * ======================================================================== */
.Lsecondary_wait:
    /* 从核在 PSCI CPU_ON 之前在此等待 */
    wfe
    b       .Lsecondary_wait

/* ========================================================================
 * 从核入口点
 * ======================================================================== */

/**
 * @brief 从核入口点
 *
 * @details QEMU virt 使用 PSCI CPU_ON 启动从核
 *          从核需要：禁用中断、设置异常向量表、设置自己的栈、跳转到 C 入口
 *
 * @note x0 由 PSCI 传入的 context_id（即 cpu_id）
 */
.section .text.boot
.global secondary_entry
secondary_entry:
    /* 禁用所有中断 */
    msr     daifset, #0xF

    /* 从 MPIDR_EL1 获取 CPU ID */
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF

    /* 设置异常向量表（与主核相同） */
    adr     x1, exception_vector_table
    msr     vbar_el1, x1
    isb

    /* 计算当前 CPU 的栈顶：__stacks_start + (cpu_id + 1) * 8192 */
    ldr     x1, =__stacks_start
    add     x1, x1, x0, lsl #13     /* x1 += cpu_id * 8192 */
    add     x1, x1, #8192           /* 栈顶 = base + (cpu_id+1)*8192 */
    mov     sp, x1

    /* 跳转到 C 入口，x0 = cpu_id */
    b       smp_secondary_entry

    /* 不应返回 */
1:  wfe
    b       1b

/* 全局符号（由链接器脚本定义） */
.global __bss_start
.global __bss_end
.global __stacks_start
.global __dtb_ptr

.data
.align 3
__dtb_ptr:
    .quad 0
