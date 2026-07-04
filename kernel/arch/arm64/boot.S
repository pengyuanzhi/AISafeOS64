/**
 * @file boot.S
 * @brief ARMv8-A 微内核启动汇编代码（TTBR1 高地址迁移版）
 * @author AISafe64 Team
 * @date 2026-07-03
 * @version 4.0
 *
 * @details 微内核启动流程（双地址空间版）：
 *          1. 从 EL3/EL2 降到 EL1
 *          2. 设置异常向量表（物理地址）
 *          3. 计算 VA-PA 偏移量（KERNEL_VA_OFFSET = 0xFFFF000000000000）
 *          4. 清零 BSS 段（使用物理地址）
 *          5. 设置内核栈（CPU 0，使用物理地址）
 *          6. 【关键】在汇编中建立粗粒度页表（1GB 块）+ TTBR0 恒等映射，
 *             开启 MMU，重定位到高地址 VA
 *             —— 必须在汇编完成，因为任何 C 代码都引用高地址 VMA 符号，
 *                MMU 关闭时立即触发数据异常。
 *          7. 跳转到 kernel_main C 入口（此时在高地址 VA 运行）
 *
 *          注意：链接脚本将内核链接到高地址虚拟基址
 *          0xFFFF000040000000，物理加载到 0x40000000。
 *          MMU 关闭时高位地址线被屏蔽，故 PC 实际在物理地址运行。
 *          所有符号引用（ldr =symbol）得到的是高地址 VMA，必须减去
 *          VA-PA 偏移量才能得到物理地址用于启动期访问。
 *
 *          mmu_early_init() 在高地址 VA 运行后，重建精细页表（4KB 权限）。
 *
 * @note 对应需求: KR-001（内核启动与初始化）
 */

.section .text.boot
.global kernel_entry

/* 注：页表描述符常量已预计算为字面值（见下文注释），不再用 #define。
 * boot.S 使用极简粗粒度页表（1GB 块）启动 MMU，精细权限由 mmu.c 重建。
 */

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

    /* ---- 计算 VA-PA 偏移量（KERNEL_VA_OFFSET）----
     * adr 取当前 PC 的物理地址，ldr = 取链接器分配的虚拟地址（高地址）。
     * 二者之差即为线性映射偏移量 0xFFFF000000000000。
     * 该偏移量保存在 x24 寄存器，供后续所有符号访问使用。
     */
    adr     x22, kernel_entry        /* x22 = 物理地址 */
    ldr     x23, =kernel_entry       /* x23 = 虚拟地址（高地址） */
    sub     x24, x23, x22            /* x24 = KERNEL_VA_OFFSET */

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

    /* 配置 SCTLR_EL1：禁用 MMU（后续 boot.S 启用） */
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
 *          - 清零 BSS 段（物理地址）
 *          - 设置 CPU 0 栈（物理地址）
 *          - 建立 MMU 页表并开启 MMU（汇编）
 *          - 重定位到高地址 VA 后跳转到 C 入口
 *
 * @note x24 保持 VA-PA 偏移量，跨 eret 不变（被调用者保存）
 */
.Lel1_entry:
    /* ---- 检查 CPU ID，从核跳转到等待循环 ---- */
    mrs     x20, mpidr_el1
    and     x20, x20, #0xFF
    cbnz    x20, .Lsecondary_wait

    /* ---- 保存 DTB 地址到全局变量（物理地址访问）----
     * ldr = 取高地址 VMA，减去偏移得物理地址。
     */
    ldr     x21, =__dtb_ptr
    sub     x21, x21, x24
    str     x0, [x21]

    /* ---- 设置异常向量表（物理地址访问）----
     * 异常向量表必须用物理地址设置（MMU 关闭期间触发异常需物理地址）。
     * 开启 MMU 跳转到高地址后会重新设置 VBAR 为高地址。
     */
    ldr     x20, =exception_vector_table
    sub     x20, x20, x24
    msr     vbar_el1, x20
    isb

    /* ---- 清零 BSS 段（物理地址）---- */
    ldr     x20, =__bss_start
    sub     x20, x20, x24
    ldr     x21, =__bss_end
    sub     x21, x21, x24
.Lclear_bss:
    cmp     x20, x21
    b.ge    .Lbss_done
    str     xzr, [x20], #8
    b       .Lclear_bss
.Lbss_done:

    /* ---- 设置 CPU 0 内核栈（物理地址）----
     * 启动期栈使用物理地址。开启 MMU 跳转到高地址后，
     * SP 会加上偏移量切换为高地址。
     */
    ldr     x20, =__stacks_start
    sub     x20, x20, x24
    add     x20, x20, #8192          /* CPU 0 栈顶 = stacks_start + 8KB */
    mov     sp, x20

    /* ---- 保存 VA-PA 偏移量到全局变量 g_va_pa_offset ----
     * 供后续 C 代码使用。此时用物理地址写入。
     */
    ldr     x20, =g_va_pa_offset
    sub     x20, x20, x24
    str     x24, [x20]

    /* ====================================================================
     * 【关键阶段】在汇编中建立页表并开启 MMU
     *
     * 原因：编译器将所有全局符号解析为高地址 VMA（adrp → 0xFFFF...）。
     *       MMU 关闭时，任何 C 函数的第一条访存指令即触发数据异常。
     *       因此 MMU 启用 + 高地址重定位必须完全在汇编中完成。
     *
     * 建立 4 级页表的极简版本（粗粒度，仅满足启动取指/访存）：
     *   TTBR1（高地址 VA = PA + 0xFFFF000000000000）:
     *     PGD[256] → PUD1（高地址 VA 在 PGD 的后半区，索引 256+）
     *       PUD1[0] = 1GB Device block  @ PA 0x00000000 (MMIO)
     *       PUD1[1] = 1GB Normal RW block @ PA 0x40000000 (RAM)
     *   TTBR0（启动期恒等映射 VA = PA）:
     *     PGD[0] → PUD0
     *       PUD0[0] = 1GB Device block  @ PA 0x00000000 (MMIO)
     *       PUD0[1] = 1GB Normal RW block @ PA 0x40000000 (RAM)
     *
     * 高地址线性映射的 PGD 索引计算：
     *   VA = 0xFFFF000000000000，PGD 索引 = VA[47:39] = 0x1FF (511)
     *   实际上 0xFFFF000000000000 >> 39 = 0x1FFFF800，低 9 位 = 0x000
     *   —— 由于 T1SZ=16（VA[63:48]=0xFFFF），TTBR1 查找从 bit47 开始。
     *   PGD 索引 = (VA >> 39) & 0x1FF。
     *   0xFFFF000000000000 的 PGD index = (0xFFFF000000000000 >> 39) & 0x1FF
     *                                   = 0。故 PGD[0]。
     * ==================================================================== */

    /* ---- 计算各页表物理地址 ----
     * x25 = boot_pgd_ttbr1 物理地址
     * x26 = boot_pud_ttbr1 物理地址
     * x27 = boot_pgd_ttbr0 物理地址
     * x28 = boot_pud_ttbr0 物理地址
     */
    ldr     x25, =boot_pgd_ttbr1
    sub     x25, x25, x24
    ldr     x26, =boot_pud_ttbr1
    sub     x26, x26, x24
    ldr     x27, =boot_pgd_ttbr0
    sub     x27, x27, x24
    ldr     x28, =boot_pud_ttbr0
    sub     x28, x28, x24

    /* ---- 清零 4 个页表（每个 4096 字节 = 512 × 8B）----
     * 4 个表共 16384 字节连续存放（.bss 中按顺序定义），从 x25 开始清零。
     * 验证：boot_pgd_ttbr1, boot_pud_ttbr1, boot_pgd_ttbr0, boot_pud_ttbr0
     *       在 .bss 中连续定义，故 x25 起的 16KB 覆盖全部。
     */
    mov     x16, x25                 /* 起始地址 */
    mov     x17, #(4096 * 4 / 8)    /* 总项数 = 2048 */
    mov     x18, xzr
.Lclear_tables:
    str     x18, [x16], #8
    subs    x17, x17, #1
    b.ne    .Lclear_tables

    /* ---- 构建 TTBR1 PUD 表（高地址线性映射）----
     * PUD1[0] = 1GB Device block @ PA 0 (MMIO: UART/GIC/virtio)
     * PUD1[1] = 1GB Normal RW block @ PA 0x40000000 (RAM)
     *
     * 描述符常量预计算（避免汇编器复杂表达式求值问题）：
     *   DEV_BLK = VALID|AF|SH|DEV_ATTR|AP_RW|PXN|UXN @PA0
     *           = 0x1|0x400|0x300|0x4|0|(1<<53)|(1<<54) = 0x0060000000000705
     *   RAM_BLK = VALID|AF|SH|NORMAL_ATTR|AP_RW|UXN @PA0x40000000
     *           （启动期需可执行，故不设 PXN，仅 UXN 禁止 EL0 执行）
     *           = 0x40000000|0x1|0x400|0x300|0|(1<<54)
     *           = 0x0040000040000705
     */
    ldr     x0, =0x0060000000000705    /* Device 1GB block @ PA 0 (PXN|UXN) */
    str     x0, [x26]                   /* boot_pud_ttbr1[0] */
    ldr     x0, =0x0040000040000705    /* Normal RW 1GB block @ PA 0x40000000 */
    str     x0, [x26, #8]              /* boot_pud_ttbr1[1]（启动期可执行） */

    /* ---- 构建 TTBR0 PUD 表（恒等映射，VA=PA）----
     * PUD0[0] = 1GB Device block @ PA 0 (MMIO)
     * PUD0[1] = 1GB Normal RW block @ PA 0x40000000 (RAM)
     */
    ldr     x0, =0x0060000000000705    /* Device 1GB block @ PA 0 */
    str     x0, [x28]                   /* boot_pud_ttbr0[0] */
    ldr     x0, =0x0040000040000705    /* Normal RW 1GB block @ PA 0x40000000 */
    str     x0, [x28, #8]              /* boot_pud_ttbr0[1]（启动期可执行） */

    /* ---- 构建 PGD 表 ----
     * TTBR1 PGD[0] → PUD1 表（高地址 VA 的 PGD 索引为 0）
     * TTBR0 PGD[0] → PUD0 表（低地址 VA 的 PGD 索引为 0）
     * PGD 表描述符 = VALID | TABLE_BIT | PUD_paddr = PUD_paddr | 0x3
     */
    ldr     x0, =boot_pud_ttbr1
    sub     x0, x0, x24               /* PUD1 物理地址 */
    orr     x0, x0, #0x3              /* VALID | TABLE_BIT */
    str     x0, [x25]                 /* boot_pgd_ttbr1[0] */

    ldr     x0, =boot_pud_ttbr0
    sub     x0, x0, x24               /* PUD0 物理地址 */
    orr     x0, x0, #0x3              /* VALID | TABLE_BIT */
    str     x0, [x27]                 /* boot_pgd_ttbr0[0] */

    /* ---- 设置 MAIR_EL1 ----
     * Attr0 = Normal WB (0xFF) [bits 7:0], Attr1 = Device nGnRnE (0x00) [15:8]
     * 值 = 0x00FF
     */
    ldr     x0, =0x00FF
    msr     mair_el1, x0
    isb

    /* ---- 设置 TCR_EL1 ----
     * T0SZ=16(0x10), T1SZ=16(0x100000), TG0_4KB(0), TG1_4KB(0x80000000),
     * IRGN0(0x100)/ORGN0(0x400), IRGN1(0x1000000)/ORGN1(0x4000000),
     * SH0(0x3000)/SH1(0x30000000), IPS=2(0x200000000)
     * 合计 = 0x2B5103510
     */
    ldr     x0, =0x2B5103510
    msr     tcr_el1, x0
    isb

    /* ---- 设置 TTBR0_EL1（恒等映射 PGD）和 TTBR1_EL1（高地址 PGD）---- */
    msr     ttbr0_el1, x27          /* boot_pgd_ttbr0 物理地址 */
    isb
    msr     ttbr1_el1, x25          /* boot_pgd_ttbr1 物理地址 */
    isb

    /* ---- 刷新 TLB ---- */
    tlbi   vmalle1is
    tlbi   vmalle1
    dsb    ish
    isb

    /* ---- 启用 MMU (SCTLR_EL1.M = 1) ----
     * 此时 TTBR0 恒等映射生效，PC 仍在物理地址可继续执行。
     */
    mrs     x0, sctlr_el1
    orr     x0, x0, #1
    msr     sctlr_el1, x0
    isb

    /* ---- 重定位到高地址 VA ----
     * MMU 已开，恒等映射仍生效。计算 kernel_main 的高地址 VA 并跳转，
     * 同时将 SP、VBAR 切换为高地址。
     */
    /* SP += KERNEL_VA_OFFSET（切换为高地址栈）*/
    add     sp, sp, x24

    /* VBAR_EL1 = exception_vector_table 高地址 VA */
    ldr     x0, =exception_vector_table
    msr     vbar_el1, x0
    isb

    /* 跳转到高地址 VA 的 kernel_main（bl 自动用高地址 LR）*/
    ldr     x0, =kernel_main
    blr     x0

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
 * @details QEMU virt 使用 PSCI CPU_ON 启动从核。
 *          从核在物理地址运行（MMU 关闭），需要：
 *          - 计算 VA-PA 偏移量
 *          - 禁用中断、设置异常向量表、设置自己的栈
 *          - 跳转到 C 入口（mmu_init_secondary 重建页表后跳高地址）
 *
 * @note x0 由 PSCI 传入的 context_id（即 cpu_id）
 */
.section .text.boot
.global secondary_entry
secondary_entry:
    /* 禁用所有中断 */
    msr     daifset, #0xF

    /* 计算 VA-PA 偏移量（与主核一致：VA - PA = KERNEL_VA_OFFSET） */
    adr     x22, secondary_entry
    ldr     x23, =secondary_entry
    sub     x24, x23, x22            /* x24 = VA - PA = KERNEL_VA_OFFSET */

    /* 从 MPIDR_EL1 获取 CPU ID */
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF

    /* 设置异常向量表（物理地址，MMU 关闭期间） */
    ldr     x1, =exception_vector_table
    sub     x1, x1, x24
    msr     vbar_el1, x1
    isb

    /* 计算当前 CPU 的栈顶（物理地址）：__stacks_start + (cpu_id + 1) * 8192 */
    ldr     x1, =__stacks_start
    sub     x1, x1, x24
    add     x1, x1, x0, lsl #13     /* x1 += cpu_id * 8192 */
    add     x1, x1, #8192           /* 栈顶 = base + (cpu_id+1)*8192 */
    mov     sp, x1

    /* 保存偏移量（与主核共用全局变量） */
    ldr     x1, =g_va_pa_offset
    sub     x1, x1, x24
    str     x24, [x1]

    /* 设置 MAIR_EL1（与主核一致）：0x00FF */
    ldr     x1, =0x00FF
    msr     mair_el1, x1
    isb

    /* 设置 TCR_EL1（与主核一致）：0x2B5103510 */
    ldr     x1, =0x2B5103510
    msr     tcr_el1, x1
    isb

    /* 复用主核已建立的 TTBR1 页表 + TTBR0 恒等映射表 */
    ldr     x1, =boot_pgd_ttbr1
    sub     x1, x1, x24
    ldr     x2, =boot_pgd_ttbr0
    sub     x2, x2, x24
    msr     ttbr1_el1, x1
    msr     ttbr0_el1, x2
    isb

    /* 刷新 TLB */
    tlbi    vmalle1is
    tlbi    vmalle1
    dsb     ish
    isb

    /* 启用 MMU */
    mrs     x1, sctlr_el1
    orr     x1, x1, #1
    msr     sctlr_el1, x1
    isb

    /* 重定位到高地址：SP += offset，VBAR 设高地址 */
    add     sp, sp, x24
    ldr     x1, =exception_vector_table
    msr     vbar_el1, x1
    isb

    /* 跳转到高地址 VA 的从核 C 入口 */
    ldr     x1, =smp_secondary_entry
    br      x1

    /* 不应返回 */
1:  wfe
    b       1b

/* 全局符号（由链接器脚本定义） */
.global __bss_start
.global __bss_end
.global __stacks_start
.global __dtb_ptr

/* VA-PA 偏移量全局变量（由 boot.S 主核写入，供 mmu.c 读取） */
.global g_va_pa_offset

/* ========================================================================
 * 启动期页表（.bss 段，4KB 对齐）
 *
 * 仅用于 boot.S 开启 MMU。mmu_early_init() 在高地址运行后会重建精细页表
 * 并重新加载 TTBR0/TTBR1（s_pgd_ttbr0_empty / s_pgd_ttbr1）。
 * ======================================================================== */
    .bss
    .align 12
    .global boot_pgd_ttbr1
    .global boot_pud_ttbr1
    .global boot_pgd_ttbr0
    .global boot_pud_ttbr0
boot_pgd_ttbr1:
    .skip 4096
boot_pud_ttbr1:
    .skip 4096
boot_pgd_ttbr0:
    .skip 4096
boot_pud_ttbr0:
    .skip 4096

/* 数据段全局变量 */
    .data
    .align 3
__dtb_ptr:
    .quad 0
g_va_pa_offset:
    .quad 0
