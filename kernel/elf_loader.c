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
#include <kernel/page_table.h>
#include <kernel/phys_mem.h>
#include <kernel/mmu.h>
#include <string.h>
#include <stdbool.h>
#include "../../sched/scheduler.h"
#include "../../sched/thread.h"

/* arch_setup_elf_thread_context 定义在 context.S */
extern void arch_setup_elf_thread_context(uint64_t *ctx, uint64_t entry,
                                           uint64_t arg, uint64_t kernel_sp,
                                           uint64_t user_sp);

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
                segments[*segment_count].filesz = phdr[i].p_filesz;
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

/* ========================================================================
 * ELF 加载并创建用户线程
 * ======================================================================== */

/** @brief 用户栈大小（8KB，与 CONFIG_STACK_SIZE_DEFAULT 一致） */
#define ELF_USER_STACK_SIZE  8192U

/** @brief 用户栈顶地址（TTBR0 高端，避开 ELF 加载区 0x400000 和 MMIO 区 0x10000000） */
#define ELF_USER_STACK_TOP   ((uint64_t)0x70000000ULL)

/** @brief 内核 UART 基址（用于加载日志） */
#define ELF_UART_BASE        ((uint64_t)0x09000000ULL)

/**
 * @brief 从 ELF 权限标志转换为 page_perm_t
 */
static page_perm_t elf_prot_to_perm(uint8_t prot)
{
    page_perm_t perm = PAGE_PERM_READ;

    if ((prot & ELF_PF_W) != 0U)
    {
        perm |= PAGE_PERM_WRITE;
    }
    if ((prot & ELF_PF_X) != 0U)
    {
        perm |= PAGE_PERM_EXEC;
    }
    return perm;
}

/**
 * @brief 将数据拷贝到用户地址空间
 *
 * @details 临时切换 TTBR0 到用户 PGD，通过用户虚拟地址写数据，然后切回内核 PGD。
 *          mmu_create_user_pgd 复制了 TTBR1 内核映射，切换 TTBR0 后内核代码
 *          仍可通过 TTBR1 访问，安全。
 */
/**
 * @brief 将数据拷贝到用户地址空间（通过物理地址恒等映射）
 *
 * @details 不切换 TTBR0（避免 TLB 刷新导致内核代码不可执行），
 *          而是通过 page_table_lookup 查找用户虚拟地址对应的物理地址，
 *          利用内核的恒等映射（物理地址=虚拟地址）直接写物理页。
 */
static void copy_to_user_space(page_table_t *user_pgd, uint64_t user_vaddr,
                                const uint8_t *src, uint64_t size)
{
    uint64_t offset = 0ULL;

    while (offset < size)
    {
        paddr_t paddr;
        uint64_t page_offset;
        uint64_t chunk_size;
        uint8_t *dst;

        /* 查找用户虚拟地址对应的物理地址 */
        if (page_table_lookup(user_pgd, user_vaddr + offset, &paddr) != KERNEL_OK)
        {
            break;  /* 映射失败 */
        }

        /* 通过内核恒等映射（PA=VA）写物理页 */
        page_offset = (user_vaddr + offset) & (PAGE_SIZE_4K - 1ULL);
        chunk_size = PAGE_SIZE_4K - page_offset;
        if (chunk_size > (size - offset))
        {
            chunk_size = size - offset;
        }

        dst = (uint8_t *)(uintptr_t)(paddr + page_offset);
        {
            uint64_t i;
            for (i = 0ULL; i < chunk_size; i++)
            {
                dst[i] = src[offset + i];
            }
        }

        offset += chunk_size;
    }
}

kernel_status_t elf_load_and_run(const uint8_t *elf_data, uint32_t elf_size,
                                  const char *thread_name)
{
    elf_header_t header;
    elf_segment_t segments[ELF_MAX_SEGMENTS];
    uint32_t seg_count = 0U;
    uint32_t i;
    elf_error_t err;
    uint64_t user_pgd;
    page_table_t *user_pgd_ptr;
    thread_id_t tid;
    KThread_t *thread;
    uint64_t user_sp = ELF_USER_STACK_TOP;

    if ((elf_data == NULL) || (elf_size == 0U))
    {
        return -(int32_t)EINVAL;
    }

    /* 解析 ELF */
    err = elf_loader_init(elf_data, elf_size, &header,
                          segments, ELF_MAX_SEGMENTS, &seg_count);
    if (err != ELF_OK)
    {
        hal_uart_puts(ELF_UART_BASE, "[ELF] parse FAIL\n");
        return -(int32_t)EINVAL;
    }

    hal_uart_puts(ELF_UART_BASE, "[ELF] Loading ");
    hal_uart_puts(ELF_UART_BASE, thread_name);
    hal_uart_puts(ELF_UART_BASE, " entry=0x");
    {
        char hex[16];
        uint32_t j;
        uint64_t entry = header.e_entry;
        for (j = 0U; j < 16U; j++)
        {
            uint8_t nibble = (uint8_t)((entry >> ((15U - j) * 4U)) & 0xFU);
            hex[j] = (char)((nibble < 10U) ? ('0' + nibble) : ('a' + nibble - 10U));
        }
        for (j = 0U; j < 16U; j++)
        {
            hal_uart_putc(ELF_UART_BASE, hex[j]);
        }
    }
    hal_uart_putc(ELF_UART_BASE, '\n');

    /* 创建用户地址空间 */
    user_pgd = mmu_create_user_pgd();
    if (user_pgd == 0ULL)
    {
        hal_uart_puts(ELF_UART_BASE, "[ELF] PGD alloc FAIL\n");
        return -(int32_t)ENOMEM;
    }
    user_pgd_ptr = (page_table_t *)(uintptr_t)user_pgd;

    /* 映射每个 PT_LOAD 段 */
    for (i = 0U; i < seg_count; i++)
    {
        uint64_t offset;
        page_perm_t perm = elf_prot_to_perm(segments[i].prot);
        /* 记录每页的物理地址，用于直接拷贝数据（恒等映射） */
        paddr_t seg_pa[16];  /* 最多 16 页（64KB 段足够） */
        uint32_t page_count = 0U;

        for (offset = 0ULL; offset < segments[i].length; offset += (uint64_t)PAGE_SIZE_4K)
        {
            paddr_t paddr = phys_mem_alloc_page();
            uint64_t vaddr = segments[i].vaddr + offset;

            if (paddr == 0ULL)
            {
                hal_uart_puts(ELF_UART_BASE, "[ELF] page alloc FAIL\n");
                return -(int32_t)ENOMEM;
            }

            if (page_table_map(user_pgd_ptr, vaddr, paddr, perm, true) != KERNEL_OK)
            {
                hal_uart_puts(ELF_UART_BASE, "[ELF] map FAIL\n");
                return -(int32_t)ENOMEM;
            }

            if (page_count < 16U)
            {
                seg_pa[page_count] = paddr;
                page_count++;
            }
        }

        /* 拷贝段数据：通过物理地址恒等映射直接写物理页 */
        if (segments[i].filesz > 0ULL)
        {
            uint64_t copied = 0ULL;
            uint32_t pi;
            for (pi = 0U; pi < page_count && copied < segments[i].filesz; pi++)
            {
                uint64_t page_off = (pi * (uint64_t)PAGE_SIZE_4K);
                uint64_t file_off = segments[i].offset + page_off;
                uint64_t chunk = (uint64_t)PAGE_SIZE_4K;
                uint8_t *dst;

                if (chunk > (segments[i].filesz - copied))
                {
                    chunk = segments[i].filesz - copied;
                }

                /* 通过内核恒等映射写物理页 */
                dst = (uint8_t *)(uintptr_t)seg_pa[pi];
                {
                    uint64_t k;
                    for (k = 0ULL; k < chunk; k++)
                    {
                        dst[k] = elf_data[file_off + k];
                    }
                }
                copied += chunk;
            }
        }

        /* BSS 清零：filesz 之后的页通过物理地址清零 */
        if (segments[i].length > segments[i].filesz)
        {
            uint64_t bss_start_page = segments[i].filesz / (uint64_t)PAGE_SIZE_4K;
            uint32_t pi;
            for (pi = (uint32_t)bss_start_page; pi < page_count; pi++)
            {
                uint8_t *bptr = (uint8_t *)(uintptr_t)seg_pa[pi];
                uint64_t bk;
                for (bk = 0ULL; bk < (uint64_t)PAGE_SIZE_4K; bk++)
                {
                    bptr[bk] = 0U;
                }
            }
        }

        hal_uart_puts(ELF_UART_BASE, "[ELF] seg loaded vaddr=0x");
        {
            char hex[16];
            uint32_t j;
            uint64_t v = segments[i].vaddr;
            for (j = 0U; j < 16U; j++)
            {
                uint8_t nibble = (uint8_t)((v >> ((15U - j) * 4U)) & 0xFU);
                hex[j] = (char)((nibble < 10U) ? ('0' + nibble) : ('a' + nibble - 10U));
            }
            for (j = 0U; j < 16U; j++)
            {
                hal_uart_putc(ELF_UART_BASE, hex[j]);
            }
        }
        hal_uart_puts(ELF_UART_BASE, " pages=");
        {
            char dec[8];
            uint32_t j;
            uint32_t v = page_count;
            for (j = 0U; j < 8U; j++) { dec[j] = (char)('0' + (v % 10U)); v /= 10U; }
            j = 8U; while (j > 1U && dec[j-1U] == '0') j--;
            while (j > 0U) hal_uart_putc(ELF_UART_BASE, dec[--j]);
        }
        hal_uart_putc(ELF_UART_BASE, '\n');

        /* 验证映射：lookup 第一个页 */
        {
            paddr_t vp;
            if (page_table_lookup(user_pgd_ptr, segments[i].vaddr, &vp) == KERNEL_OK)
            {
                hal_uart_puts(ELF_UART_BASE, "[ELF] lookup OK paddr=0x");
                {
                    char h[16];
                    uint32_t j;
                    for (j = 0U; j < 16U; j++)
                    {
                        uint8_t n = (uint8_t)((vp >> ((15U-j)*4U)) & 0xFU);
                        h[j] = (char)((n<10U)?('0'+n):('a'+n-10U));
                    }
                    for (j = 0U; j < 16U; j++) hal_uart_putc(ELF_UART_BASE, h[j]);
                }
                hal_uart_putc(ELF_UART_BASE, '\n');
            }
            else
            {
                hal_uart_puts(ELF_UART_BASE, "[ELF] lookup FAIL\n");
            }
        }
    }

    /* 分配用户栈（映射 ELF_USER_STACK_TOP 下方若干页） */
    {
        uint64_t stack_offset;
        for (stack_offset = 0ULL; stack_offset < (uint64_t)ELF_USER_STACK_SIZE;
             stack_offset += (uint64_t)PAGE_SIZE_4K)
        {
            paddr_t paddr = phys_mem_alloc_page();
            uint64_t vaddr = ELF_USER_STACK_TOP - (uint64_t)ELF_USER_STACK_SIZE + stack_offset;

            if (paddr == 0ULL)
            {
                hal_uart_puts(ELF_UART_BASE, "[ELF] stack alloc FAIL\n");
                return -(int32_t)ENOMEM;
            }

            if (page_table_map(user_pgd_ptr, vaddr, paddr,
                               PAGE_PERM_RW, true) != KERNEL_OK)
            {
                hal_uart_puts(ELF_UART_BASE, "[ELF] stack map FAIL\n");
                return -(int32_t)ENOMEM;
            }
        }
    }

    /* 创建内核线程（kthread_create 分配内核栈） */
    tid = kthread_create(thread_name,
                         (kthread_entry_t)header.e_entry,
                         NULL,
                         (priority_t)200U,
                         KTHREAD_POLICY_FIFO,
                         CONFIG_STACK_SIZE_DEFAULT);
    if (tid == THREAD_ID_INVALID)
    {
        hal_uart_puts(ELF_UART_BASE, "[ELF] thread create FAIL\n");
        return -(int32_t)ENOMEM;
    }

    /* 配置为用户态线程 */
    thread = &g_scheduler.thread_table[tid];
    thread->is_user = 1U;
    thread->user_sp = (vaddr_t)user_sp;
    thread->user_pgd = user_pgd;

    /* 设置 ELF 上下文（TTBR0 入口） */
    {
        uint64_t kernel_sp = (uint64_t)(uintptr_t)thread->stack_base + thread->stack_size;
        arch_setup_elf_thread_context(thread->context,
                                       header.e_entry,
                                       0U,
                                       kernel_sp,
                                       user_sp);
    }

    hal_uart_puts(ELF_UART_BASE, "[ELF] Loaded tid=");
    {
        char dec[8];
        uint32_t j;
        uint64_t val = (uint64_t)tid;
        for (j = 0U; j < 8U; j++)
        {
            dec[j] = (char)('0' + (val % 10ULL));
            val /= 10ULL;
        }
        /* 跳过前导零 */
        j = 8U;
        while (j > 1U && dec[j - 1U] == '0')
        {
            j--;
        }
        while (j > 0U)
        {
            hal_uart_putc(ELF_UART_BASE, dec[j - 1U]);
            j--;
        }
    }
    hal_uart_putc(ELF_UART_BASE, '\n');

    return KERNEL_OK;
}
