/**
 * @file kernel.ld
 * @brief AISafeOS64 微内核 ARMv8-A 链接脚本
 * @author AISafe64 Team
 * @date 2026-03-31
 * @version 2.0
 *
 * @details 微内核内存布局
 *          - 内核加载地址: 0x40080000 (QEMU virt)
 *          - 异常向量表: 2KB 对齐
 *          - 内核代码段: 只读可执行
 *          - 内核数据段: 读写
 *          - 每 CPU 栈: 8KB × NR_CPUS
 *          - 内核堆: 1MB（对象池预留）
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
 *       后续 P0-3 阶段启用 MMU 后切换到虚拟地址
 */
__kernel_base = 0x40080000;

SECTIONS
{
    /**
     * .text.boot 段 - 启动和异常向量表
     * @details 必须在内核映像最前面
     *          包含异常向量表（2KB 对齐要求）
     */
    . = __kernel_base;
    .text.boot : {
        *(.text.boot)
        . = ALIGN(4096);
    } > RAM

    /**
     * .text 段 - 内核代码
     */
    .text : {
        *(.text .text.*)
    } > RAM

    /**
     * .rodata 段 - 只读数据
     * @details 包含常量字符串、跳转表、只读结构体
     */
    .rodata : ALIGN(4096) {
        *(.rodata .rodata.*)
    } > RAM

    /**
     * .data 段 - 读写数据
     */
    .data : ALIGN(4096) {
        *(.data .data.*)
    } > RAM

    /**
     * .bss 段 - 未初始化数据
     * @details 启动时由 boot.S 清零
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
     */
    .stacks : ALIGN(4096) {
        __stacks_start = .;
        . = . + (8192 * 8);   /* 8KB × 8 CPUs */
        __stacks_end = .;
    } > RAM

    /**
     * .heap 段 - 内核堆（对象池）
     * @details 2MB 预留，用于内核对象池分配
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
