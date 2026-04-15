/**
 * @file    elf_loader.c
 * @brief   ELF 加载器实现
 * @version 1.0
 * @date    2026-04-15
 *
 * 实现 ELF 加载器的核心功能：
 * - 从 VirtIO 块设备读取 ELF 文件
 * - 解析 ELF 头和段表
 * - 验证 ELF 格式
 *
 * @note 供内核启动流程使用
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/elf.h>
#include <kernel/driver.h>
#include <arch/arm64/hal.h>
#include <kernel/types.h>
#include <string.h>
#include <stdbool.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

#define ELF_BUF_SIZE            4096U
#define ELF_MAX_SEGMENTS        16U
#define ELF_MAGIC_0            0x7F
#define ELF_MAGIC_1            'E'
#define ELF_MAGIC_2            'L'
#define ELF_MAGIC_3            'F'
#define ELF_CLASS_64BIT         2U
#define ELF_ENDIAN_LE           1U

/* ========================================================================
 * 全局变量
 * ======================================================================== */

static uint8_t s_elf_buf[ELF_BUF_SIZE] __attribute__((aligned(8))) = {0};
static elf_header_t s_elf_header;
static elf_segment_t s_elf_segments[ELF_MAX_SEGMENTS];
static uint32_t s_elf_segment_count;
static elf_error_t s_elf_status = ELF_ERR_NO_DEV;
static bool s_elf_initialized = false;

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 检查 ELF 魔术
 */
static bool elf_check_magic(const uint8_t *ident)
{
    return (ident[0U] == ELF_MAGIC_0) &&
           (ident[1U] == ELF_MAGIC_1) &&
           (ident[2U] == ELF_MAGIC_2) &&
           (ident[3U] == ELF_MAGIC_3);
}

/* ========================================================================
 * ELF 加载器接口实现
 * ======================================================================== */

elf_error_t elf_loader_init(const uint8_t *elf_data, uint32_t elf_size,
                           elf_header_t *header,
                           elf_segment_t *segments, uint32_t max_segments,
                           uint32_t *segment_count)
{
    uint32_t i;
    const elf_program_header_t *phdr;

    if ((elf_data == NULL) || (header == NULL) ||
        (segments == NULL) || (segment_count == NULL))
    {
        return ELF_ERR_READ;
    }

    if (elf_size < sizeof(elf_header_t))
    {
        return ELF_ERR_READ;
    }

    /* 读取 ELF 头 */
    (void)memcpy(header, elf_data, sizeof(elf_header_t));

    /* 验证 ELF 魔术 */
    if (!elf_check_magic(header->e_ident))
    {
        return ELF_ERR_MAGIC;
    }

    /* 验证 ELF 类别（64-bit） */
    if (header->e_ident[4U] != ELF_CLASS_64BIT)
    {
        return ELF_ERR_CLASS;
    }

    /* 验证字节序（小端） */
    if (header->e_ident[5U] != ELF_ENDIAN_LE)
    {
        return ELF_ERR_ENDIAN;
    }

    /* 验证架构（AArch64） */
    if (header->e_machine != ELF_EM_AARCH64)
    {
        return ELF_ERR_MACHINE;
    }

    /* 验证文件类型（EXEC 或 DYN） */
    if ((header->e_type != ELF_ET_EXEC) &&
        (header->e_type != ELF_ET_DYN))
    {
        return ELF_ERR_TYPE;
    }

    /* 读取段表 */
    *segment_count = 0U;

    if ((header->e_phoff > 0U) && (header->e_phnum > 0U) &&
        (header->e_phentsize >= sizeof(elf_program_header_t)))
    {
        phdr = (const elf_program_header_t *)(elf_data + header->e_phoff);

        for (i = 0U; (i < header->e_phnum) && (i < max_segments); i++)
        {
            if (phdr[i].p_type == ELF_PT_LOAD)
            {
                segments[*segment_count].vaddr = phdr[i].p_vaddr;
                segments[*segment_count].length = phdr[i].p_memsz;
                segments[*segment_count].offset = phdr[i].p_offset;

                /* 转换保护权限 */
                segments[*segment_count].prot = 0U;
                if ((phdr[i].p_flags & ELF_PF_R) != 0U)
                {
                    segments[*segment_count].prot |= ELF_PF_R;
                }
                if ((phdr[i].p_flags & ELF_PF_W) != 0U)
                {
                    segments[*segment_count].prot |= ELF_PF_W;
                }
                if ((phdr[i].p_flags & ELF_PF_X) != 0U)
                {
                    segments[*segment_count].prot |= ELF_PF_X;
                }

                segments[*segment_count].active = true;
                (*segment_count)++;
            }
        }
    }

    return ELF_OK;
}

elf_error_t elf_loader_get_status(void)
{
    return s_elf_status;
}

elf_error_t elf_loader_get_entry(uint64_t *entry)
{
    if (entry == NULL)
    {
        return ELF_ERR_READ;
    }

    if (!s_elf_initialized)
    {
        return ELF_ERR_NO_DEV;
    }

    *entry = s_elf_header.e_entry;

    return ELF_OK;
}
