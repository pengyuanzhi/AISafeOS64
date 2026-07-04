/**
 * @file    virtio_blk_driver.ld
 * @brief   用户态 virtio-blk 驱动链接脚本
 *
 * @details 加载地址 0x400000（4MB），位于用户空间低地址（TTBR0）。
 *          独立 ELF，不编译进内核镜像。
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
