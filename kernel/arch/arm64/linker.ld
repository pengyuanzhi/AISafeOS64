/**
 * @file linker.ld
 * @brief AISafeOS64 微内核 ARMv8-A 链接脚本（kernel/arch/arm64/ 副本）
 * @author AISafe64 Team
 * @date 2026-04-04
 * @version 2.1
 *
 * @details 微内核内存布局
 *          - 内核加载地址: 0x40080000 (QEMU virt)
 *          - 异常向量表: 2KB 对齐
 *          - 内核代码段: 只读可执行
 *          - 内核数据段: 读写
 *          - 每 CPU 栈: 8KB × NR_CPUS
 *          - 内核堆: 2MB（对象池预留）
 *
 * @note MMU 页对齐支持：
 *       - .text.boot: 4KB 对齐（含异常向量表）
 *       - .rodata:    4KB 对齐（可映射为只读）
 *       - .data:      4KB 对齐（可映射为读写）
 *       - .bss:       16B 对齐（启动清零）
 *       - .percpu:    64B 对齐（缓存行）
 *       - .stacks:    4KB 对齐（页对齐）
 *       - .heap:      4KB 对齐（页对齐，供 MMU 页分配器使用）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(kernel_entry)

/**
 * @brief 内存配置（QEMU virt 平台）
 *
 * RAM: 0x40000000 起始，1GB
 * 内核加载: 0x40080000（跳过 512KB 留给 bootloader/DTB）
 */
MEMORY
{
    RAM (rwx) : ORIGIN = 0x40000000, LENGTH = 1024M
}

/**
 * @brief 内核虚拟地址常量
 *
 * @note 当前使用物理地址直接映射
 *       后续启用 MMU 后切换到虚拟地址
 */
__kernel_base = 0x40080000;

SECTIONS
{
    /**
     * .text.boot 段 - 启动和异常向量表
     * @details 必须在内核映像最前面
     *          包含异常向量表（2KB 对齐要求）
     *          MMU: 4KB 页对齐，可映射为只读可执行
     */
    . = __kernel_base;
    .text.boot : {
        *(.text.boot)
        . = ALIGN(4096);
    } > RAM

    /**
     * .text 段 - 内核代码
     * @details MMU: 4KB 页对齐，映射为只读可执行（PXN=0）
     */
    .text : ALIGN(4096) {
        *(.text .text.*)
        . = ALIGN(4096);
    } > RAM
    __text_end = .;   /* text 段结束（.rodata 开始前） */

    /**
     * .rodata 段 - 只读数据
     * @details 包含常量字符串、跳转表、只读结构体
     *          MMU: 4KB 页对齐，映射为只读不可执行
     */
    .rodata : ALIGN(4096) {
        *(.rodata .rodata.*)
    } > RAM

    /* .text / .rodata 只读区域结束标记 */
    __rodata_end = .;

    /**
     * .data 段 - 读写数据
     * @details MMU: 4KB 页对齐，映射为读写不可执行
     */
    .data : ALIGN(4096) {
        *(.data .data.*)
    } > RAM

    /**
     * .bss 段 - 未初始化数据
     * @details 启动时由 boot.S 清零
     *          MMU: 16B 对齐即可，与 .data 在同一可写区域
     */
    .bss : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } > RAM

    /**
     * .percpu 段 - 每 CPU 数据
     * @details 每个 CPU 拥有独立副本
     *          大小为 __percpu_size，运行时按 CPU ID 偏移访问
     *          MMU: 64B 缓存行对齐，避免伪共享
     */
    .percpu : ALIGN(64) {
        __percpu_start = .;
        *(.percpu .percpu.*)
        . = ALIGN(64);
        __percpu_end = .;
    } > RAM
    __percpu_size = __percpu_end - __percpu_start;

    /**
     * .stacks 段 - 每 CPU 内核栈
     * @details 每个 CPU 8KB 栈，16 字节对齐
     *          最多 8 个 CPU = 64KB
     *          MMU: 4KB 页对齐
     */
    .stacks : ALIGN(4096) {
        __stacks_start = .;
        . = . + (8192 * 8);   /* 8KB × 8 CPUs */
        __stacks_end = .;
    } > RAM

    /**
     * .heap 段 - 内核堆（对象池）
     * @details 2MB 预留，用于内核对象池分配
     *          MMU: 4KB 页对齐，可按页管理
     */
    .heap : ALIGN(4096) {
        __heap_start = .;
        . = . + 0x200000;     /* 2MB */
        __heap_end = .;
    } > RAM

    /**
     * 全局符号导出
     */
    __kernel_end = .;
    __kernel_size = __kernel_end - __kernel_base;
}

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
