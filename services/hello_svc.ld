/**
 * @file    hello_svc.ld
 * @brief   hello_svc 链接脚本（用户态低地址）
 *
 * @details 链接到用户空间低地址 0x10000，不依赖 musl/crt0。
 */

ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    .text : {
        *(.text.startup)
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
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.ARM.extab*)
    }
}
