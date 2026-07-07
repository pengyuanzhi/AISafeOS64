/**
 * @file    init_elf.S
 * @brief   嵌入 hello_start ELF（用户态冒烟测试）
 *
 * @details 将 build/services/hello_svc.elf 以二进制形式嵌入
 *          内核 .rodata 段的 .init_elf 子段。内核启动时通过
 *          elf_load_and_run 加载到用户空间执行。
 *
 *          后续替换为从 virtio-blk 磁盘加载 init.elf。
 */

.section .rodata
.global _init_elf_start
.global _init_elf_end

_init_elf_start:
.incbin "../../../build/hello_svc.elf"
_init_elf_end:
