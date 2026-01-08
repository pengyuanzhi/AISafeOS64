/*
 * app.lds - Application Linker Script
 *
 * Linker script for user applications.
 * Defines memory layout and section placement.
 *
 * @note AISafe64 Application Loader
 * @version 1.0
 * @date 2025-01-08
 */

OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(app_main)

/* User space base address */
USER_SPACE_BASE = 0x40000000;

/* Memory regions */
MEMORY
{
    /* Application code and data (loaded from ELF) */
    APP_MEM (rwx) : ORIGIN = USER_SPACE_BASE, LENGTH = 256M

    /* Stack (allocated by kernel at runtime) */
    /* STACK (rw) : ORIGIN = 0x50000000, LENGTH = 1M */
}

/* Section definitions */
SECTIONS
{
    /* Text section (code) */
    .text : {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata*)
    } > APP_MEM

    /* Read-only data */
    .rodata : {
        *(.rodata)
        *(.rodata*)
    } > APP_MEM

    /* Initialized data */
    .data : {
        *(.data)
        *(.data*)
        *(.sdata)
        *(.sdata*)
    } > APP_MEM

    /* BSS (uninitialized data) */
    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss*)
        *(.sbss)
        *(.sbss*)
        *(COMMON)
        __bss_end = .;
    } > APP_MEM

    /* Stack information (for reference only) */
    .stack : {
        __stack_bottom = .;
        . = ALIGN(16);
        /* Stack is allocated by kernel, not here */
        __stack_top = .;
    }

    /* Debug sections */
    /DISCARD/ : {
        *(.comment)
        *(.note)
        *(.note.*)
    }

    /* Symbol export */
    __app_start = USER_SPACE_BASE;
    __app_end = .;
}

/* Stack size (informational, actual size from config) */
__stack_size = 0x4000; /* 16KB default */
