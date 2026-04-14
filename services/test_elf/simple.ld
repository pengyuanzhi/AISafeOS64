/**
 * @file    simple.ld
 * @brief   ELF 测试程序链接脚本
 * @author  AISafe64 Team
 * @date    2026-04-14
 *
 * @details 简单的用户态程序链接脚本，加载地址 0x400000（4MB），
 *          位于用户空间地址范围内。
 */

ENTRY(_start)

SECTIONS
{
    . = 0x400000;

    .text : {
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.ARM.attributes)
    }
}
