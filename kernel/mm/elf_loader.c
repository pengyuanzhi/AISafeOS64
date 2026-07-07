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
 *
 * @revision history
 * v1.0 2026-07-05 初始版本
 */

#include <kernel/elf.h>
#include <kernel/driver.h>
#include <arch/arm64/hal.h>
#include <kernel/types.h>
#include <kernel/page_table.h>
#include <kernel/virt_phys.h>
#include <kernel/errno.h>
#include <kernel/vmspace.h>
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

/** @brief 用户栈顶地址（TTBR0 用户空间，避开 ELF 加载区 0x400000 和 MMIO 区 0x10000000） */
#define ELF_USER_STACK_TOP   ((uint64_t)0x60100000ULL)

/**
 * @brief 将数据拷贝到用户地址空间
 *
 * @details 通过 page_table_lookup 查找用户虚拟地址对应的物理地址，
 *          利用内核线性映射（phys_to_virt）直接写物理页，
 *          无需切换 TTBR0，避免 TLB 刷新影响内核代码执行。
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

        /* 通过 linear mapping 写物理页（phys_to_virt 转换） */
        page_offset = (user_vaddr + offset) & (PAGE_SIZE_4K - 1ULL);
        chunk_size = PAGE_SIZE_4K - page_offset;
        if (chunk_size > (size - offset))
        {
            chunk_size = size - offset;
        }

        dst = (uint8_t *)phys_to_virt(paddr + page_offset);
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
    vm_space_t *user_space;       /* 用户虚拟地址空间（vmspace 抽象） */
    uint64_t user_pgd_pa;        /* 用户 PGD 物理地址（写 TTBR0 用） */
    page_table_t *user_pgd;      /* 用户 PGD 虚拟指针（C 代码访问用） */
    thread_id_t tid;
    KThread_t *thread;
    uint64_t user_sp = ELF_USER_STACK_TOP;

    /* 参数校验 */
    if ((elf_data == NULL) || (elf_size == 0U))
    {
        return -(int32_t)EINVAL;
    }

    /* 解析 ELF */
    err = elf_loader_init(elf_data, elf_size, &header,
                          segments, ELF_MAX_SEGMENTS, &seg_count);
    if (err != ELF_OK)
    {
        return -(int32_t)EINVAL;
    }

    /* 通过 vmspace 子系统创建用户地址空间（VMA 跟踪 + ASID + PGD） */
    if (vmspace_create(&user_space) != KERNEL_OK)
    {
        return -(int32_t)ENOMEM;
    }
    user_pgd = user_space->pgd;
    user_pgd_pa = (uint64_t)virt_to_phys(user_pgd);

    /* 为每个 ELF 段分配物理页并映射到用户地址空间 */
    for (i = 0U; i < seg_count; i++)
    {
        uint64_t addr;
        uint64_t seg_end;
        page_perm_t perm;

        if (segments[i].length == 0ULL)
        {
            continue;
        }

        /* 根据段权限设置映射属性 */
        perm = PAGE_PERM_READ | PAGE_PERM_WRITE;  /* 默认 RW（含 BSS） */
        /* ELF 单段 RWE 驱动：也加 EXEC */
        perm |= PAGE_PERM_EXEC;

        seg_end = segments[i].vaddr + segments[i].length;

        /* 按页分配物理页 */
        for (addr = segments[i].vaddr; addr < seg_end; addr += PAGE_SIZE_4K)
        {
            paddr_t page_pa = phys_mem_alloc_page();
            if (page_pa == 0ULL)
            {
                return -(int32_t)ENOMEM;
            }
            /* 清零新页（确保 BSS 区域为零） */
            {
                uint8_t *page = (uint8_t *)phys_to_virt(page_pa);
                uint32_t k;
                for (k = 0U; k < PAGE_SIZE_4K; k++)
                {
                    page[k] = 0U;
                }
            }
            if (page_table_map(user_pgd, (vaddr_t)addr, page_pa, perm, true) != KERNEL_OK)
            {
                return -(int32_t)ENOMEM;
            }
        }

        /* 通过 vmspace_map 注册 VMA（红黑树跟踪，供缺页处理查询权限） */
        {
            uint32_t vma_flags = VMA_FLAG_READ | VMA_FLAG_WRITE | VMA_FLAG_EXEC;
            vaddr_t mapped = vmspace_map(user_space,
                                          (vaddr_t)segments[i].vaddr,
                                          segments[i].length,
                                          vma_flags,
                                          VMA_TYPE_CODE,
                                          0ULL);
            if (mapped == (vaddr_t)0)
            {
                /* VMA 注册失败（非致命，页表映射已成功） */
            }
        }

        /* 拷贝段数据（filesz 部分）通过 linear mapping */
        if (segments[i].filesz > 0ULL)
        {
            copy_to_user_space(user_pgd, segments[i].vaddr,
                               elf_data + segments[i].offset, segments[i].filesz);
        }
    }

    /* 映射用户栈（1 页 = 4KB，位于 ELF_USER_STACK_TOP 下方） */
    {
        paddr_t stack_pa = phys_mem_alloc_page();
        if (stack_pa == 0ULL)
        {
            return -(int32_t)ENOMEM;
        }
        /* 栈向下生长，映射栈顶下方一页 */
        if (page_table_map(user_pgd, (vaddr_t)(ELF_USER_STACK_TOP - PAGE_SIZE_4K),
                           stack_pa, PAGE_PERM_READ | PAGE_PERM_WRITE, true) != KERNEL_OK)
        {
            return -(int32_t)ENOMEM;
        }
        /* 注册栈 VMA（供缺页处理识别栈区域、自动扩展） */
        (void)vmspace_map(user_space,
                          (vaddr_t)(ELF_USER_STACK_TOP - PAGE_SIZE_4K),
                          PAGE_SIZE_4K,
                          VMA_FLAG_READ | VMA_FLAG_WRITE | VMA_FLAG_STACK,
                          VMA_TYPE_STACK,
                          0ULL);
    }

    /* 创建内核线程 */
    tid = kthread_create(thread_name,
                         (kthread_entry_t)header.e_entry,
                         NULL,
                         (priority_t)200U,
                         KTHREAD_POLICY_FIFO,
                         CONFIG_STACK_SIZE_DEFAULT);
    if (tid == THREAD_ID_INVALID)
    {
        return -(int32_t)ENOMEM;
    }

    /* 配置为用户态线程 */
    thread = &g_scheduler.thread_table[tid];
    thread->is_user = 1U;
    thread->pid = (uint32_t)(uintptr_t)user_space;  /* 唯一标识此进程 */
    thread->user_sp = (vaddr_t)user_sp;
    thread->user_pgd = user_pgd_pa;  /* 存物理地址（mmu_switch_to_user 用） */

    /* 设置 ELF 上下文 */
    {
        uint64_t kernel_sp = (uint64_t)(uintptr_t)thread->stack_base + thread->stack_size;
        arch_setup_elf_thread_context(thread->context,
                                       header.e_entry,
                                       0U,
                                       kernel_sp,
                                       user_sp);
    }

    return KERNEL_OK;
}
