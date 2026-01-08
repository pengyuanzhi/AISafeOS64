/**
 * @file linker.ld
 * @brief AISafe64 RTOS - ARMv8-A链接脚本
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details ARMv8-A内存布局定义
 *          - 内核虚拟地址：0xFFFF000000000000
 *          - 物理内存映射：恒等映射
 *          - 栈空间：8KB
 *          - 对齐要求：页对齐（4KB）
 */

OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)

/**
 * @brief 内存配置
 *
 * @note QEMU virt平台默认配置
 *       - RAM: 1GB @ 0x40000000
 *       - 内核加载地址：0x40080000
 */
MEMORY
{
    RAM (rwx)   : ORIGIN = 0x40000000, LENGTH = 1G
    KERNEL (rx) : ORIGIN = 0x40080000, LENGTH = 2M
}

/**
 * @brief 段定义
 */
SECTIONS
{
    /**
     * .text.boot段 - 启动代码
     * @details 必须放在最前面，包含异常向量表
     */
    .text.boot : {
        *(.text.boot)
        . = ALIGN(4096);
    } > KERNEL

    /**
     * .text段 - 代码段
     * @details 只读，可执行
     */
    .text : {
        *(.text*)
        *(.rodata*)
    } > KERNEL

    /**
     * .rodata段 - 只读数据
     */
    .rodata : {
        *(.rodata*)
    } > KERNEL

    /**
     * .data段 - 读写数据
     */
    .data : {
        *(.data*)
    } > KERNEL

    /**
     * .bss段 - 未初始化数据
     * @details 清零
     */
    .bss : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } > KERNEL

    /**
     * .heap段 - 内核堆
     * @details 1MB堆空间，用于kmalloc/kfree
     */
    .heap (NOLOAD) : {
        __kernel_heap_start = .;
        . = . + 1M;                  /* 1MB堆空间 */
        __kernel_heap_end = .;
    } > KERNEL

    /**
     * .stack段 - 栈空间
     * @details 8KB栈，向下增长
     */
    .stack (NOLOAD) : {
        __stack_bottom = .;
        . = . + 8192;                /* 8KB栈空间 */
        __stack_top = .;
    } > KERNEL

    /**
     * 全局符号
     */
    __dtb_ptr = __bss_end;           /* 设备树指针 */
}

/**
 * @brief 调试符号
 */
DISCARD
{
    /* 丢弃不需要的段 */
    *(.comment)
    *(.ARM.attributes)
}
