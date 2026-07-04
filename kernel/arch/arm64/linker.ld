/**
 * @file linker.ld
 * @brief AISafeOS64 微内核 ARMv8-A 链接脚本（kernel/arch/arm64/ 副本，TTBR1 高地址空间）
 * @author AISafe64 Team
 * @date    2026-07-03
 * @version 3.0
 *
 * @details 标准 ARM64 双地址空间内存布局：
 *          - 内核链接虚拟基址（VMA）: 0xFFFF000040000000（TTBR1 高地址）
 *          - 内核物理加载地址（LMA）: 0x40000000（QEMU virt 加载点）
 *          - 线性映射偏移量: 0xFFFF000000000000（VA = PA + 偏移）
 *
 *          VMA 与 LMA 分离（AT 关键字）:
 *          - QEMU `-kernel` 按 program header 的 PhysAddr(p_paddr) 加载 ELF
 *            到物理内存 0x40000000；
 *          - QEMU 跳转到 e_entry（高地址 VMA）。MMU 关闭时高位地址线被
 *            物理地址空间屏蔽（48 位 PA），故 PC 实际落在 0x40000000，
 *            与加载位置一致，可正常取指。
 *          - 启动汇编（boot.S）在物理地址运行，建立 TTBR1 高地址映射后
 *            跳转到高地址继续执行（见 mmu_early_init 之后）。
 *
 *          段对齐要求（4KB 页映射）:
 *          - .text.boot: 4KB 对齐（含异常向量表）
 *          - .text:      4KB 对齐，映射为只读可执行（RX）
 *          - .rodata:    4KB 对齐，映射为只读不可执行（R--）
 *          - .data:      4KB 对齐，映射为读写不可执行（RW-）
 *          - .bss:       16B 对齐（启动清零）
 *          - .percpu:    64B 对齐（缓存行）
 *          - .stacks:    4KB 对齐（页对齐）
 *          - .heap:      4KB 对齐（页对齐）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(kernel_entry)

/**
 * @brief 内核虚拟基址（VMA，TTBR1 高地址线性映射）
 *
 * @details 线性映射偏移 = 0xFFFF000000000000，
 *          故 VA 0xFFFF000040000000 对应 PA 0x40000000。
 */
__kernel_virt_base = 0xFFFF000040000000;

/**
 * @brief 内核物理加载地址（LMA，QEMU virt 加载点）
 */
__kernel_phys_load = 0x40000000;

SECTIONS
{
    /**
     * .text.boot 段 - 启动代码和异常向量表
     * @details 必须在内核映像最前面，包含异常向量表（2KB 对齐）。
     *          MMU: 4KB 页对齐，可映射为只读可执行。
     *          LMA 固定在 __kernel_phys_load（0x40000000）。
     */
    . = __kernel_virt_base;
    .text.boot : AT(__kernel_phys_load) {
        KEEP(*(.text.boot))
        . = ALIGN(4096);
    }

    /**
     * .text 段 - 内核代码
     * @details MMU: 4KB 页对齐，映射为只读可执行（PXN=0）。
     *          LMA 紧跟 .text.boot 之后。
     */
    .text : AT(__kernel_phys_load + SIZEOF(.text.boot)) ALIGN(4096) {
        *(.text .text.*)
        . = ALIGN(4096);
    }
    __text_end = .;   /* text 段结束（.rodata 开始前） */

    /**
     * .rodata 段 - 只读数据
     * @details 包含常量字符串、跳转表、只读结构体。
     *          MMU: 4KB 页对齐，映射为只读不可执行。
     */
    .rodata : AT(__kernel_phys_load + SIZEOF(.text.boot) + SIZEOF(.text)) ALIGN(4096) {
        *(.rodata .rodata.*)
        . = ALIGN(4096);
    }

    /* .text / .rodata 只读区域结束标记 */
    __rodata_end = .;

    /**
     * .data 段 - 读写数据
     * @details MMU: 4KB 页对齐，映射为读写不可执行。
     */
    .data : AT(__kernel_phys_load + SIZEOF(.text.boot) + SIZEOF(.text) + SIZEOF(.rodata)) ALIGN(4096) {
        *(.data .data.*)
        . = ALIGN(4096);
    }

    /**
     * .bss 段 - 未初始化数据
     * @details 启动时由 boot.S 清零。
     *          MMU: 16B 对齐即可，与 .data 在同一可写区域。
     */
    .bss : AT(__kernel_phys_load + SIZEOF(.text.boot) + SIZEOF(.text) + SIZEOF(.rodata) + SIZEOF(.data)) ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }

    /**
     * .percpu 段 - 每 CPU 数据
     * @details 每个 CPU 拥有独立副本，大小为 __percpu_size。
     *          MMU: 64B 缓存行对齐，避免伪共享。
     */
    .percpu : AT(__kernel_phys_load + SIZEOF(.text.boot) + SIZEOF(.text) + SIZEOF(.rodata) + SIZEOF(.data) + SIZEOF(.bss)) ALIGN(64) {
        __percpu_start = .;
        *(.percpu .percpu.*)
        . = ALIGN(64);
        __percpu_end = .;
    }
    __percpu_size = __percpu_end - __percpu_start;

    /**
     * .stacks 段 - 每 CPU 内核栈
     * @details 每个 CPU 8KB 栈，最多 8 个 CPU = 64KB。
     *          MMU: 4KB 页对齐。
     */
    .stacks : AT(__kernel_phys_load + SIZEOF(.text.boot) + SIZEOF(.text) + SIZEOF(.rodata) + SIZEOF(.data) + SIZEOF(.bss) + SIZEOF(.percpu)) ALIGN(4096) {
        __stacks_start = .;
        . = . + (8192 * 8);   /* 8KB × 8 CPUs */
        __stacks_end = .;
    }

    /**
     * .heap 段 - 内核堆（对象池）
     * @details 2MB 预留，用于内核对象池分配。
     *          MMU: 4KB 页对齐，可按页管理。
     */
    .heap : AT(__kernel_phys_load + SIZEOF(.text.boot) + SIZEOF(.text) + SIZEOF(.rodata) + SIZEOF(.data) + SIZEOF(.bss) + SIZEOF(.percpu) + SIZEOF(.stacks)) ALIGN(4096) {
        __heap_start = .;
        . = . + 0x200000;     /* 2MB */
        __heap_end = .;
    }

    /**
     * 全局符号导出
     */
    __kernel_end = .;
    __kernel_size = __kernel_end - __kernel_virt_base;

    /**
     * @brief 丢弃段
     */
    /DISCARD/ :
    {
        *(.comment)
        *(.ARM.attributes)
        *(.note.*)
        *(.eh_frame*)
    }
}
