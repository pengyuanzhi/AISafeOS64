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
#define ELF_USER_STACK_TOP   ((uint64_t)0x60100000ULL)

/** @brief 内核 UART 基址（用于加载日志） */
#define ELF_UART_BASE        ((uint64_t)0x09000000ULL)

/**
 * @brief 从 ELF 权限标志转换为 page_perm_t
 */
static page_perm_t elf_prot_to_perm(uint8_t prot)
{
    /* 使用 bit 7 (0x80) 作为全局映射标志，避免 nG 位导致 ASID 不匹配 */
    page_perm_t perm = (page_perm_t)(PAGE_PERM_READ | 0x80U);  /* 全局映射 */

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

    /*
     * 修改内核 PMD 使 ELF 区域（0x60000000+）对 EL0 可访问。
     *
     * 内核 PMD 的 PMD[1..511] 是 2MB Normal block（AP=PRIV_RW，EL0 禁止）。
     * 修改 ELF 加载地址对应的 PMD 条目的 AP 位为 ALL_RW（EL0+EL1 RW），
     * 并清除 XN/PXN（允许执行）。这是恒等映射（VA=PA），无需四级遍历。
     */
    {
        extern uint64_t hal_read_ttbr0(void);
        volatile uint64_t *pud;
        volatile uint64_t *pmd;
        uint64_t pud1;
        uint32_t pmd_idx;
        user_pgd = hal_read_ttbr0();
        user_pgd_ptr = (page_table_t *)(uintptr_t)user_pgd;

        /* PUD[1] → s_pmd_kernel（内核 PMD table） */
        pud = (volatile uint64_t *)0x40036000ULL;
        pud1 = pud[1];
        pmd = (volatile uint64_t *)(uintptr_t)(pud1 & 0x0000FFFFFFFFF000ULL);

        /* 0x60000000 的 PMD index = (0x60000000 >> 21) & 511 = 256 */
        /* 修改 PMD[256]（0x60000000-0x601FFFFF）和 PMD[257]（0x60200000） */
        for (pmd_idx = 256U; pmd_idx <= 257U; pmd_idx++)
        {
            /* 完全重写 PMD 条目：2MB block，Normal，EL0+EL1 RWX
             * 物理地址 = PMD index 对应的 0x60000000/0x60200000 */
            uint64_t blk_addr = 0x40000000ULL + (uint64_t)pmd_idx * 0x200000ULL;
            pmd[pmd_idx] = 1ULL            /* valid */
                         | (1ULL << 10)    /* AF */
                         | (1ULL << 6)     /* AP=ALL_RW (EL0+EL1) */
                         | (3ULL << 8)     /* SH=Inner */
                         | blk_addr;       /* 物理 2MB block 地址 */
            /* 不设 XN/PXN（bit 54/53=0），允许执行 */
        }

        __asm__ volatile("tlbi vmalle1" ::: "memory");
        __asm__ volatile("dsb nsh; isb");

        /* 诊断：打印 PMD[256] 和 PMD 指针 */
        hal_uart_puts(ELF_UART_BASE, "[ELF] PMD256=0x");
        { char h[16]; uint32_t j;
          uint64_t v = pmd[256U];
          for(j=0U;j<16U;j++){uint8_t n=(uint8_t)((v>>((15U-j)*4U))&0xFU);h[j]=(char)((n<10U)?('0'+n):('a'+n-10U));}
          for(j=0U;j<16U;j++) hal_uart_putc(ELF_UART_BASE,h[j]); }
        hal_uart_puts(ELF_UART_BASE, " PMDptr=0x");
        { char h[16]; uint32_t j;
          uint64_t v = (uint64_t)(uintptr_t)pmd;
          for(j=0U;j<16U;j++){uint8_t n=(uint8_t)((v>>((15U-j)*4U))&0xFU);h[j]=(char)((n<10U)?('0'+n):('a'+n-10U));}
          for(j=0U;j<16U;j++) hal_uart_putc(ELF_UART_BASE,h[j]); }
        hal_uart_putc(ELF_UART_BASE,'\n');
    }

    /*
     * 段数据拷贝（恒等映射：PUD[2] 的 1GB block 已覆盖 0x80000000+）
     * 直接写虚拟地址 = 物理地址（0x80000000+）。
     */
    for (i = 0U; i < seg_count; i++)
    {
        if (segments[i].filesz > 0ULL)
        {
            uint8_t *dst = (uint8_t *)(uintptr_t)segments[i].vaddr;
            uint64_t k;
            for (k = 0ULL; k < segments[i].filesz; k++)
            {
                dst[k] = elf_data[segments[i].offset + k];
            }
        }
        /* BSS 清零（memsz > filesz 部分） */
        if (segments[i].length > segments[i].filesz)
        {
            uint8_t *bss = (uint8_t *)(uintptr_t)(segments[i].vaddr + segments[i].filesz);
            uint64_t bss_len = segments[i].length - segments[i].filesz;
            uint64_t k;
            for (k = 0ULL; k < bss_len; k++)
            {
                bss[k] = 0U;
            }
        }
        hal_uart_puts(ELF_UART_BASE, "[ELF] seg copied vaddr=0x");
        { char h[16]; uint32_t j;
          uint64_t v = segments[i].vaddr;
          for(j=0U;j<16U;j++){uint8_t n=(uint8_t)((v>>((15U-j)*4U))&0xFU);h[j]=(char)((n<10U)?('0'+n):('a'+n-10U));}
          for(j=0U;j<16U;j++) hal_uart_putc(ELF_UART_BASE,h[j]); }
        hal_uart_putc(ELF_UART_BASE,'\n');
    }

    /* 用户栈：PUD[2] 的 1GB block 已覆盖 0x80000000-0xBFFFFFFF（恒等映射），
     * 栈地址 0x8FF80000 在此范围内，无需额外映射。 */

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
        /* 诊断：验证 context */
        hal_uart_puts(ELF_UART_BASE, "[ELF] ctx ELR=0x");
        {
            char h[16]; uint32_t j;
            uint64_t v = thread->context[14U];
            for (j = 0U; j < 16U; j++) { uint8_t n=(uint8_t)((v>>((15U-j)*4U))&0xFU); h[j]=(char)((n<10U)?('0'+n):('a'+n-10U)); }
            for (j = 0U; j < 16U; j++) hal_uart_putc(ELF_UART_BASE, h[j]);
        }
        hal_uart_puts(ELF_UART_BASE, " SPSR=0x");
        {
            char h[16]; uint32_t j;
            uint64_t v = thread->context[13U];
            for (j = 0U; j < 16U; j++) { uint8_t n=(uint8_t)((v>>((15U-j)*4U))&0xFU); h[j]=(char)((n<10U)?('0'+n):('a'+n-10U)); }
            for (j = 0U; j < 16U; j++) hal_uart_putc(ELF_UART_BASE, h[j]);
        }
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
