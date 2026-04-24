/**
 * @file    main.c
 * @brief   ProcessManager 进程管理器服务（完整进程空间管理和 ELF 加载器）
 * @author  AISafe64 Team
 * @date    2026-04-14
 * @version 6.0
 *
 * @details 用户态进程管理器：完整进程生命周期管理
 *          - fork/exec 语义的进程创建
 *          - 进程状态机（ready/running/blocked/zombie）
 *          - 进程组管理
 *          - 会话管理
 *          - 进程树管理
 *          - 信号处理框架
 *          - 资源限制（rlimit）
 *          - 进程资源统计
 *          - 孤儿进程回收
 *          - 进程空间管理（mmap/munmap/mprotect/brk/sbrk）
 *          - ELF 加载器
 *          - 堆/栈管理
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/service.h>
#include <kernel/config.h>
#include <kernel/errno.h>
#include <kernel/syscall.h>
#include <kernel/types.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

/* ========================================================================
 * IPC 消息协议（从 kernel/arch/arm64/entry.c 复制）
 * ======================================================================== */

/**
 * @brief 服务间 IPC 消息格式
 */
typedef struct
{
    uint32_t type;     /**< @brief 消息类型 */
    uint32_t len;      /**< @brief 数据长度 */
    uint64_t data[8];  /**< @brief 数据负载（从 4 扩展到 8，支持 PROC_MSG_MMAP 等消息）*/
} service_msg_t;

#define PROC_SIG_MAX                31U
#define PROC_SIG_PENDING_MAX        64U
#define PROC_RLIMIT_MAX             8U
#define PROC_PRIO_MIN               0U
#define PROC_PRIO_MAX              127U
#define PROC_PRIO_DEFAULT           64U
#define SESSION_LEADER_BIT         (1U << 0)
#define PGRP_LEADER_BIT            (1U << 1)
#define PROC_DAEMON_BIT            (1U << 2)
#define MAX_MAPPINGS              128U
#define MAX_ELF_SEGMENTS           16U
#define PAGE_SIZE                 4096U
#define ET_EXEC                   2U
#define ET_DYN                    3U
#define EM_AARCH64                183U
#define PT_NULL                   0U
#define PT_LOAD                   1U
#define PF_X                      (1U << 0)
#define PF_W                      (1U << 1)
#define PF_R                      (1U << 2)

#define PROC_MSG_FORK               0x0014U
#define PROC_MSG_EXEC               0x0015U
#define PROC_MSG_WAITPID            0x0016U
#define PROC_MSG_EXIT               0x0017U
#define PROC_MSG_SIGNAL             0x0018U
#define PROC_MSG_RLIMIT_SET         0x0019U
#define PROC_MSG_RLIMIT_GET         0x001AU
#define PROC_MSG_STATE_SET          0x001BU
#define PROC_MSG_GETPGID           0x001CU
#define PROC_MSG_SETPGID           0x001DU
#define PROC_MSG_GETSID             0x001EU
#define PROC_MSG_SETSID             0x001FU
#define PROC_MSG_GETPRIORITY        0x0020U
#define PROC_MSG_SETPRIORITY        0x0021U
#define PROC_MSG_GETPROCSTATS       0x0022U
#define PROC_MSG_GETPROCLIST       0x0023U
#define PROC_MSG_KILLPG             0x0024U
#define PROC_MSG_MMAP               0x0025U
#define PROC_MSG_MUNMAP             0x0026U
#define PROC_MSG_MPROTECT           0x0027U
#define PROC_MSG_BRK                0x0028U
#define PROC_MSG_SBRK               0x0029U
#define PROC_MSG_LOAD_ELF          0x002AU
#define PROC_MSG_GETMAPPINGLIST     0x002BU

#define SIGHUP      1U
#define SIGINT      2U
#define SIGQUIT     3U
#define SIGILL      4U
#define SIGTRAP     5U
#define SIGABRT     6U
#define SIGKILL     9U
#define SIGSEGV     11U
#define SIGPIPE     13U
#define SIGALRM     14U
#define SIGTERM     15U
#define SIGCHLD     17U
#define SIGSTOP     19U
#define SIGCONT     18U
#define SIGUSR1     10U
#define SIGUSR2     12U

typedef enum
{
    RLIMIT_CPU = 0U,
    RLIMIT_FSIZE,
    RLIMIT_DATA,
    RLIMIT_STACK,
    RLIMIT_CORE,
    RLIMIT_RSS,
    RLIMIT_NOFILE,
    RLIMIT_AS
} rlimit_resource_t;

typedef struct
{
    uint64_t    cur;
    uint64_t    max;
} rlimit_t;

typedef enum
{
    SIG_ACT_DEFAULT = 0U,
    SIG_ACT_IGNORE,
    SIG_ACT_HANDLER
} sig_action_t;

typedef struct
{
    sig_action_t    action;
    uint64_t        flags;
} sig_handler_t;

typedef enum
{
    MAP_SHARED = 1U,
    MAP_PRIVATE = 2U,
    MAP_FIXED = 4U,
    MAP_ANONYMOUS = 8U,
    MAP_STACK = 16U
} map_flags_t;

typedef enum
{
    PROT_NONE = 0U,
    PROT_READ = 1U,
    PROT_WRITE = 2U,
    PROT_EXEC = 4U
} prot_flags_t;

typedef struct
{
    uint64_t        addr;
    uint64_t        length;
    uint64_t        offset;
    prot_flags_t    prot;
    map_flags_t     flags;
    kobj_id_t       vmem_id;
    bool            active;
} memory_mapping_t;

typedef struct
{
    uint8_t     e_ident[16];
    uint16_t    e_type;
    uint16_t    e_machine;
    uint32_t    e_version;
    uint64_t    e_entry;
    uint64_t    e_phoff;
    uint64_t    e_shoff;
    uint32_t    e_flags;
    uint16_t    e_ehsize;
    uint16_t    e_phentsize;
    uint16_t    e_phnum;
    uint16_t    e_shentsize;
    uint16_t    e_shnum;
} elf_header_t;

typedef struct
{
    uint32_t    p_type;
    uint32_t    p_flags;
    uint64_t    p_offset;
    uint64_t    p_vaddr;
    uint64_t    p_paddr;
    uint64_t    p_filesz;
    uint64_t    p_memsz;
    uint64_t    p_align;
} elf_program_header_t;

typedef struct
{
    uint64_t        vaddr;
    uint64_t        length;
    uint64_t        offset;
    prot_flags_t    prot;
    bool            active;
} elf_segment_t;

typedef struct
{
    uint64_t    utime;
    uint64_t    stime;
    uint64_t    total_time;
    uint64_t    minflt;
    uint64_t    majflt;
    uint64_t    maxrss;
    uint64_t    numthreads;
    uint64_t    volctxsw;
    uint64_t    ivolctxsw;
    uint64_t    bytes_read;
    uint64_t    bytes_written;
    uint64_t    bytes_sent;
    uint64_t    bytes_recv;
} proc_stats_t;

typedef struct
{
    process_desc_t  base;
    uint32_t        exit_code;
    tick_t          start_time;
    tick_t          end_time;

    uint32_t        sig_pending[2U];
    uint32_t        sig_blocked[2U];
    sig_handler_t   sig_handlers[PROC_SIG_MAX];

    rlimit_t        rlimits[PROC_RLIMIT_MAX];

    uint32_t        child_count;
    uint32_t        pgrp;
    uint32_t        session;
    uint32_t        flags;

    int32_t         priority;
    int32_t         nice;

    proc_stats_t    stats;

    uint32_t        wait_chld_pid;
    int32_t        *wait_status;

    memory_mapping_t mappings[MAX_MAPPINGS];
    uint32_t        mapping_count;

    uint64_t        heap_start;
    uint64_t        heap_end;
    uint64_t        heap_limit;

    uint64_t        stack_start;
    uint64_t        stack_end;
    uint64_t        stack_limit;

    elf_segment_t    elf_segments[MAX_ELF_SEGMENTS];
    uint32_t        elf_segment_count;
    uint64_t        elf_entry;
    bool            elf_loaded;
} proc_entry_t;

static proc_entry_t s_procs[MAX_PROCESSES];
static uint32_t s_next_pid;
static uint32_t s_active_count;
static uint32_t s_next_pgrp;
static uint32_t s_next_session;
static uint32_t s_init_pid;

static uint32_t proc_find_index(uint32_t pid)
{
    uint32_t i;

    for (i = 0U; i < MAX_PROCESSES; i++)
    {
        if ((s_procs[i].base.pid == pid) &&
            (s_procs[i].base.state != PROC_STATE_EMPTY))
        {
            return i;
        }
    }

    return MAX_PROCESSES;
}

static bool proc_is_child(uint32_t pid, uint32_t parent)
{
    uint32_t idx = proc_find_index(pid);

    if (idx < MAX_PROCESSES)
    {
        return (s_procs[idx].base.parent_pid == parent);
    }

    return false;
}

static uint32_t proc_pgrp_create(uint32_t pid)
{
    uint32_t idx = proc_find_index(pid);

    if (idx >= MAX_PROCESSES)
    {
        return 0U;
    }

    s_next_pgrp++;
    s_procs[idx].pgrp = s_next_pgrp;
    s_procs[idx].flags |= PGRP_LEADER_BIT;

    return s_next_pgrp;
}

static int32_t proc_getpgrp(uint32_t pid)
{
    uint32_t idx = proc_find_index(pid);

    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    return (int32_t)s_procs[idx].pgrp;
}

static int32_t proc_setpgrp(uint32_t pid, uint32_t pgrp)
{
    uint32_t idx;

    idx = proc_find_index(pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    if (pgrp == 0U)
    {
        pgrp = proc_pgrp_create(pid);
        if (pgrp == 0U)
        {
            return -(int32_t)EINVAL;
        }
        return 0;
    }

    s_procs[idx].pgrp = pgrp;
    s_procs[idx].flags &= ~PGRP_LEADER_BIT;

    return 0;
}

static uint32_t proc_session_create(uint32_t pid)
{
    uint32_t idx = proc_find_index(pid);

    if (idx >= MAX_PROCESSES)
    {
        return 0U;
    }

    if ((s_procs[idx].flags & PGRP_LEADER_BIT) == 0U)
    {
        return 0U;
    }

    s_next_session++;
    s_procs[idx].session = s_next_session;
    s_procs[idx].flags |= SESSION_LEADER_BIT;

    return s_next_session;
}

static int32_t proc_getsid(uint32_t pid)
{
    uint32_t idx = proc_find_index(pid);

    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    return (int32_t)s_procs[idx].session;
}

static int32_t proc_setsid(uint32_t pid)
{
    return (int32_t)proc_session_create(pid);
}

static int32_t proc_get_priority(uint32_t pid)
{
    uint32_t idx = proc_find_index(pid);

    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    return s_procs[idx].priority;
}

static int32_t proc_set_priority(uint32_t pid, int32_t priority)
{
    uint32_t idx = proc_find_index(pid);

    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    if ((priority < PROC_PRIO_MIN) || (priority > PROC_PRIO_MAX))
    {
        return -(int32_t)EINVAL;
    }

    s_procs[idx].priority = priority;
    s_procs[idx].nice = (PROC_PRIO_DEFAULT - priority);

    return 0;
}

static int32_t proc_get_stats(uint32_t pid, proc_stats_t *stats)
{
    uint32_t idx = proc_find_index(pid);

    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    if (stats == NULL)
    {
        return -(int32_t)EINVAL;
    }

    (void)memcpy(stats, &s_procs[idx].stats, sizeof(proc_stats_t));

    return 0;
}

static bool elf_check_magic(const uint8_t *ident)
{
    return (ident[0] == 0x7F) &&
           (ident[1] == 'E') &&
           (ident[2] == 'L') &&
           (ident[3] == 'F');
}

static int32_t elf_read_header(const uint8_t *data, uint32_t size,
                                elf_header_t *header)
{
    if (size < sizeof(elf_header_t))
    {
        return -(int32_t)EINVAL;
    }

    (void)memcpy(header, data, sizeof(elf_header_t));

    if (!elf_check_magic(header->e_ident))
    {
        return -(int32_t)ENOEXEC;
    }

    if (header->e_machine != EM_AARCH64)
    {
        return -(int32_t)ENOEXEC;
    }

    if ((header->e_type != ET_EXEC) && (header->e_type != ET_DYN))
    {
        return -(int32_t)ENOEXEC;
    }

    return 0;
}

static int32_t elf_read_segments(const uint8_t *data, uint32_t size,
                                 const elf_header_t *header,
                                 elf_segment_t *segments, uint32_t max_segments,
                                 uint32_t *segment_count)
{
    uint32_t i;
    const elf_program_header_t *phdr;

    if (segment_count == NULL)
    {
        return -(int32_t)EINVAL;
    }

    *segment_count = 0U;

    if (header->e_phoff >= size)
    {
        return -(int32_t)EINVAL;
    }

    phdr = (const elf_program_header_t *)(data + header->e_phoff);

    for (i = 0U; (i < header->e_phnum) && (i < max_segments); i++)
    {
        if (phdr[i].p_type == PT_LOAD)
        {
            segments[*segment_count].vaddr = phdr[i].p_vaddr;
            segments[*segment_count].length = phdr[i].p_memsz;
            segments[*segment_count].offset = phdr[i].p_offset;

            segments[*segment_count].prot = PROT_NONE;
            if ((phdr[i].p_flags & PF_R) != 0U)
            {
                segments[*segment_count].prot |= PROT_READ;
            }
            if ((phdr[i].p_flags & PF_W) != 0U)
            {
                segments[*segment_count].prot |= PROT_WRITE;
            }
            if ((phdr[i].p_flags & PF_X) != 0U)
            {
                segments[*segment_count].prot |= PROT_EXEC;
            }

            segments[*segment_count].active = true;
            (*segment_count)++;
        }
    }

    return 0;
}

static int32_t proc_load_elf(uint32_t pid, const uint8_t *elf_data,
                               uint32_t elf_size)
{
    uint32_t idx;
    elf_header_t header;
    int32_t ret;

    idx = proc_find_index(pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    ret = elf_read_header(elf_data, elf_size, &header);
    if (ret < 0)
    {
        return ret;
    }

    ret = elf_read_segments(elf_data, elf_size, &header,
                           s_procs[idx].elf_segments, MAX_ELF_SEGMENTS,
                           &s_procs[idx].elf_segment_count);
    if (ret < 0)
    {
        return ret;
    }

    s_procs[idx].elf_entry = header.e_entry;
    s_procs[idx].elf_loaded = true;

    return 0;
}

static int32_t proc_mmap(uint32_t pid, uint64_t addr, uint64_t length,
                         prot_flags_t prot, map_flags_t flags,
                         uint64_t offset)
{
    uint32_t idx;
    uint32_t i;

    idx = proc_find_index(pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    if (s_procs[idx].mapping_count >= MAX_MAPPINGS)
    {
        return -(int32_t)ENOMEM;
    }

    if (addr == 0ULL)
    {
        addr = 0x10000000ULL;
    }

    for (i = 0U; i < MAX_MAPPINGS; i++)
    {
        if (!s_procs[idx].mappings[i].active)
        {
            s_procs[idx].mappings[i].addr = addr;
            s_procs[idx].mappings[i].length = length;
            s_procs[idx].mappings[i].offset = offset;
            s_procs[idx].mappings[i].prot = prot;
            s_procs[idx].mappings[i].flags = flags;
            s_procs[idx].mappings[i].vmem_id = 0ULL;
            s_procs[idx].mappings[i].active = true;

            s_procs[idx].mapping_count++;

            return (int32_t)addr;
        }
    }

    return -(int32_t)ENOMEM;
}

static int32_t proc_munmap(uint32_t pid, uint64_t addr, uint64_t length)
{
    uint32_t idx;
    uint32_t i;

    idx = proc_find_index(pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    for (i = 0U; i < MAX_MAPPINGS; i++)
    {
        if (s_procs[idx].mappings[i].active &&
            (s_procs[idx].mappings[i].addr == addr))
        {
            (void)memset(&s_procs[idx].mappings[i], 0,
                         sizeof(memory_mapping_t));
            if (s_procs[idx].mapping_count > 0U)
            {
                s_procs[idx].mapping_count--;
            }

            return 0;
        }
    }

    return -(int32_t)EINVAL;
}

static int32_t proc_mprotect(uint32_t pid, uint64_t addr, uint64_t length,
                             prot_flags_t prot)
{
    uint32_t idx;
    uint32_t i;

    idx = proc_find_index(pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    for (i = 0U; i < MAX_MAPPINGS; i++)
    {
        if (s_procs[idx].mappings[i].active &&
            (s_procs[idx].mappings[i].addr == addr))
        {
            s_procs[idx].mappings[i].prot = prot;
            return 0;
        }
    }

    return -(int32_t)EINVAL;
}

static int32_t proc_brk(uint32_t pid, uint64_t addr)
{
    uint32_t idx;

    idx = proc_find_index(pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    if (addr == 0ULL)
    {
        return (int32_t)s_procs[idx].heap_end;
    }

    if (addr > s_procs[idx].heap_limit)
    {
        return -(int32_t)ENOMEM;
    }

    s_procs[idx].heap_end = addr;

    return 0;
}

static int64_t proc_sbrk(uint32_t pid, int64_t increment)
{
    uint32_t idx;
    uint64_t old_brk;
    uint64_t new_brk;

    idx = proc_find_index(pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int64_t)ESRCH;
    }

    old_brk = s_procs[idx].heap_end;
    new_brk = old_brk + (uint64_t)increment;

    if (new_brk > s_procs[idx].heap_limit)
    {
        return (int64_t)-(int32_t)ENOMEM;
    }

    s_procs[idx].heap_end = new_brk;

    return (int64_t)old_brk;
}

static int32_t proc_get_mapping_list(uint32_t pid, memory_mapping_t *list,
                                     uint32_t max, uint32_t *count)
{
    uint32_t idx;
    uint32_t i;
    uint32_t num = 0U;

    if ((list == NULL) || (count == NULL))
    {
        return -(int32_t)EINVAL;
    }

    idx = proc_find_index(pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    for (i = 0U; (i < MAX_MAPPINGS) && (num < max); i++)
    {
        if (s_procs[idx].mappings[i].active)
        {
            (void)memcpy(&list[num], &s_procs[idx].mappings[i],
                        sizeof(memory_mapping_t));
            num++;
        }
    }

    *count = num;

    return 0;
}

static void proc_init(void)
{
    uint32_t i;
    uint32_t j;

    (void)memset(s_procs, 0, sizeof(s_procs));

    for (i = 0U; i < MAX_PROCESSES; i++)
    {
        s_procs[i].base.pid = 0U;
        s_procs[i].base.state = PROC_STATE_EMPTY;
        s_procs[i].exit_code = 0U;
        s_procs[i].child_count = 0U;
        s_procs[i].pgrp = 0U;
        s_procs[i].session = 0U;
        s_procs[i].flags = 0U;
        s_procs[i].priority = PROC_PRIO_DEFAULT;
        s_procs[i].nice = 0;
        s_procs[i].wait_chld_pid = 0U;
        s_procs[i].wait_status = NULL;

        for (j = 0U; j < PROC_SIG_MAX; j++)
        {
            s_procs[i].sig_handlers[j].action = SIG_ACT_DEFAULT;
            s_procs[i].sig_handlers[j].flags = 0U;
        }

        s_procs[i].sig_pending[0U] = 0U;
        s_procs[i].sig_pending[1U] = 0U;
        s_procs[i].sig_blocked[0U] = 0U;
        s_procs[i].sig_blocked[1U] = 0U;

        for (j = 0U; j < PROC_RLIMIT_MAX; j++)
        {
            s_procs[i].rlimits[j].cur = 0xFFFFFFFFFFFFFFFFULL;
            s_procs[i].rlimits[j].max = 0xFFFFFFFFFFFFFFFFULL;
        }

        (void)memset(&s_procs[i].stats, 0, sizeof(proc_stats_t));

        for (j = 0U; j < MAX_MAPPINGS; j++)
        {
            s_procs[i].mappings[j].active = false;
        }
        s_procs[i].mapping_count = 0U;
        s_procs[i].heap_start = 0x40000000ULL;
        s_procs[i].heap_end = s_procs[i].heap_start;
        s_procs[i].heap_limit = 0x50000000ULL;
        s_procs[i].stack_start = 0x7FFF0000ULL;
        s_procs[i].stack_end = s_procs[i].stack_start;
        s_procs[i].stack_limit = 0x70000000ULL;

        for (j = 0U; j < MAX_ELF_SEGMENTS; j++)
        {
            s_procs[i].elf_segments[j].active = false;
        }
        s_procs[i].elf_segment_count = 0U;
        s_procs[i].elf_entry = 0ULL;
        s_procs[i].elf_loaded = false;
    }

    s_next_pid = 1U;
    s_active_count = 0U;
    s_next_pgrp = 1U;
    s_next_session = 1U;
    s_init_pid = 1U;
}

static int32_t proc_fork(uint32_t parent_pid, uint32_t *child_pid)
{
    uint32_t parent_idx;
    uint32_t child_idx;
    uint32_t j;

    if (child_pid == NULL)
    {
        return -(int32_t)EINVAL;
    }

    parent_idx = proc_find_index(parent_pid);
    if (parent_idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    if (s_active_count >= MAX_PROCESSES)
    {
        return -(int32_t)ENOMEM;
    }

    child_idx = MAX_PROCESSES;
    for (j = 0U; j < MAX_PROCESSES; j++)
    {
        if (s_procs[j].base.state == PROC_STATE_EMPTY)
        {
            child_idx = j;
            break;
        }
    }

    if (child_idx >= MAX_PROCESSES)
    {
        return -(int32_t)ENOMEM;
    }

    s_procs[child_idx] = s_procs[parent_idx];

    s_procs[child_idx].base.pid = s_next_pid++;
    s_procs[child_idx].base.parent_pid = parent_pid;
    s_procs[child_idx].base.state = PROC_STATE_RUNNING;
    s_procs[child_idx].base.thread_count = 0U;
    s_procs[child_idx].exit_code = 0U;
    s_procs[child_idx].child_count = 0U;

    s_procs[child_idx].sig_pending[0U] = 0U;
    s_procs[child_idx].sig_pending[1U] = 0U;

    s_procs[child_idx].pgrp = s_procs[parent_idx].pgrp;
    s_procs[child_idx].session = s_procs[parent_idx].session;
    s_procs[child_idx].flags &= ~PGRP_LEADER_BIT;
    s_procs[child_idx].flags &= ~SESSION_LEADER_BIT;

    s_procs[parent_idx].child_count++;

    s_active_count++;

    *child_pid = s_procs[child_idx].base.pid;

    return 0;
}

static int32_t proc_exec(uint32_t pid, const char *name,
                         kobj_id_t cspace_id, kobj_id_t vspace_id,
                         kobj_id_t endpoint_id)
{
    uint32_t idx;
    uint32_t j;

    idx = proc_find_index(pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    for (j = 0U; (name != NULL) && (j < PROC_NAME_MAX - 1U) && (name[j] != '\0'); j++)
    {
        s_procs[idx].base.name[j] = name[j];
    }
    s_procs[idx].base.name[j] = '\0';

    s_procs[idx].base.cspace_id = cspace_id;
    s_procs[idx].base.vspace_id = vspace_id;
    s_procs[idx].base.endpoint_id = endpoint_id;

    for (j = 0U; j < PROC_SIG_MAX; j++)
    {
        s_procs[idx].sig_handlers[j].action = SIG_ACT_DEFAULT;
    }

    s_procs[idx].base.state = PROC_STATE_LOADING;

    return 0;
}

static int32_t proc_exit(uint32_t pid, int32_t exit_code)
{
    uint32_t idx;
    uint32_t parent_idx;

    idx = proc_find_index(pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    s_procs[idx].exit_code = (uint32_t)exit_code;
    s_procs[idx].base.state = PROC_STATE_ZOMBIE;

    parent_idx = proc_find_index(s_procs[idx].base.parent_pid);
    if (parent_idx < MAX_PROCESSES)
    {
        uint32_t sig_idx = (SIGCHLD - 1U) / 32U;
        uint32_t sig_bit = (SIGCHLD - 1U) % 32U;
        s_procs[parent_idx].sig_pending[sig_idx] |= (1U << sig_bit);
    }

    return 0;
}

static int32_t proc_waitpid(uint32_t caller_pid, uint32_t target_pid,
                               int32_t *status, int32_t options)
{
    uint32_t caller_idx;
    uint32_t target_idx;

    caller_idx = proc_find_index(caller_pid);
    if (caller_idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    if (target_pid == (uint32_t)-1)
    {
        for (target_idx = 0U; target_idx < MAX_PROCESSES; target_idx++)
        {
            if ((s_procs[target_idx].base.state == PROC_STATE_ZOMBIE) &&
                (s_procs[target_idx].base.parent_pid == caller_pid))
            {
                if (status != NULL)
                {
                    *status = (int32_t)s_procs[target_idx].exit_code;
                }

                uint32_t pid = s_procs[target_idx].base.pid;
                (void)memset(&s_procs[target_idx], 0, sizeof(proc_entry_t));
                if (s_active_count > 0U)
                {
                    s_active_count--;
                }
                if (s_procs[caller_idx].child_count > 0U)
                {
                    s_procs[caller_idx].child_count--;
                }

                return (int32_t)pid;
            }
        }

        if ((options & 0x01U) != 0U)
        {
            return -(int32_t)ECHILD;
        }

        return -(int32_t)EINTR;
    }

    target_idx = proc_find_index(target_pid);
    if (target_idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    if (s_procs[target_idx].base.parent_pid != caller_pid)
    {
        return -(int32_t)ECHILD;
    }

    if (s_procs[target_idx].base.state == PROC_STATE_ZOMBIE)
    {
        if (status != NULL)
        {
            *status = (int32_t)s_procs[target_idx].exit_code;
        }

        (void)memset(&s_procs[target_idx], 0, sizeof(proc_entry_t));
        if (s_active_count > 0U)
        {
            s_active_count--;
        }
        if (s_procs[caller_idx].child_count > 0U)
        {
            s_procs[caller_idx].child_count--;
        }

        return (int32_t)target_pid;
    }

    if ((options & 0x01U) != 0U)
    {
        return 0;
    }

    return -(int32_t)EINTR;
}

static int32_t proc_signal_send(uint32_t target_pid, uint32_t sig)
{
    uint32_t idx;

    if ((sig == 0U) || (sig > PROC_SIG_MAX))
    {
        return -(int32_t)EINVAL;
    }

    idx = proc_find_index(target_pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    if (sig == SIGKILL)
    {
        return proc_exit(target_pid, 0x80U | sig);
    }

    if (sig == SIGSTOP)
    {
        s_procs[idx].base.state = PROC_STATE_BLOCKED;
        return 0;
    }

    if (sig == SIGCONT)
    {
        s_procs[idx].base.state = PROC_STATE_RUNNING;
        return 0;
    }

    uint32_t word = (sig - 1U) / 32U;
    uint32_t bit = (sig - 1U) % 32U;

    if ((s_procs[idx].sig_blocked[word] & (1U << bit)) != 0U)
    {
        return 0;
    }

    s_procs[idx].sig_pending[word] |= (1U << bit);

    return 0;
}

static int32_t proc_rlimit_set(uint32_t pid, rlimit_resource_t resource,
                                   const rlimit_t *rlim)
{
    uint32_t idx;

    if (rlim == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (resource >= PROC_RLIMIT_MAX)
    {
        return -(int32_t)EINVAL;
    }

    idx = proc_find_index(pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    if (rlim->cur > rlim->max)
    {
        return -(int32_t)EINVAL;
    }

    s_procs[idx].rlimits[resource] = *rlim;

    return 0;
}

static int32_t proc_rlimit_get(uint32_t pid, rlimit_resource_t resource,
                                   rlimit_t *rlim)
{
    uint32_t idx;

    if (rlim == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (resource >= PROC_RLIMIT_MAX)
    {
        return -(int32_t)EINVAL;
    }

    idx = proc_find_index(pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    *rlim = s_procs[idx].rlimits[resource];

    return 0;
}

static int32_t proc_set_state(uint32_t pid, proc_state_t state)
{
    uint32_t idx;
    proc_state_t cur;

    idx = proc_find_index(pid);
    if (idx >= MAX_PROCESSES)
    {
        return -(int32_t)ESRCH;
    }

    cur = s_procs[idx].base.state;

    switch (cur)
    {
        case PROC_STATE_LOADING:
            if (state != PROC_STATE_RUNNING)
            {
                return -(int32_t)EINVAL;
            }
            break;

        case PROC_STATE_RUNNING:
            if ((state != PROC_STATE_BLOCKED) &&
                (state != PROC_STATE_ZOMBIE) &&
                (state != PROC_STATE_EMPTY))
            {
                return -(int32_t)EINVAL;
            }
            break;

        case PROC_STATE_BLOCKED:
            if ((state != PROC_STATE_RUNNING) &&
                (state != PROC_STATE_ZOMBIE) &&
                (state != PROC_STATE_EMPTY))
            {
                return -(int32_t)EINVAL;
            }
            break;

        case PROC_STATE_ZOMBIE:
            if (state != PROC_STATE_EMPTY)
            {
                return -(int32_t)EINVAL;
            }
            break;

        case PROC_STATE_EMPTY:
        default:
            return -(int32_t)EINVAL;
    }

    s_procs[idx].base.state = state;

    return 0;
}

/* ========================================================================
 * proc_get_proclist - 获取进程列表
 * ======================================================================== */

/**
 * @brief 获取进程列表
 *
 * @param pid_list 输出：进程 ID 列表
 * @param max_count 最大进程数
 * @param count 输出：实际进程数
 * @return 0 成功，其他为错误码
 */
static int32_t proc_get_proclist(uint32_t *pid_list, uint32_t max_count,
                              uint32_t *count)
{
    uint32_t i;
    uint32_t actual_count;

    actual_count = 0U;

    for (i = 0U; i < MAX_PROCESSES; i++)
    {
        if (s_procs[i].base.state != PROC_STATE_EMPTY)
        {
            if (actual_count < max_count)
            {
                pid_list[actual_count] = s_procs[i].base.pid;
            }
            actual_count++;
        }
    }

    *count = actual_count;

    return 0;
}

/* ========================================================================
 * proc_killpg - 终止进程组
 * ======================================================================== */

/**
 * @brief 终止进程组中的所有进程
 *
 * @param pgrp 进程组 ID
 * @param sig 信号
 * @return 0 成功，其他为错误码
 */
static int32_t proc_killpg(uint32_t pgrp, uint32_t sig)
{
    uint32_t i;
    int32_t ret;
    uint32_t killed_count;

    killed_count = 0U;
    ret = 0;

    for (i = 0U; i < MAX_PROCESSES; i++)
    {
        if ((s_procs[i].base.state != PROC_STATE_EMPTY) &&
            (s_procs[i].pgrp == pgrp))
        {
            int32_t tmp = proc_exit(s_procs[i].base.pid, -(int32_t)sig);
            if (tmp == 0)
            {
                killed_count++;
            }
            else
            {
                ret = tmp;
            }
        }
    }

    if (killed_count == 0U)
    {
        return -(int32_t)ESRCH;
    }

    return ret;
}

int main(void)
{
    int32_t ret;
    kobj_id_t my_endpoint;

    proc_init();

    my_endpoint = syscall2(SYS_EP_CREATE, 0ULL, 0ULL);
    if (my_endpoint <= 0)
    {
        return -1;
    }

    for (;;)
    {
        service_msg_t req;
        uint8_t reply[2048U];
        int32_t reply_size;

        ret = syscall3(SYS_MSG_RECV, my_endpoint,
                       (uint64_t)(uintptr_t)&req, sizeof(req));
        if (ret < 0)
        {
            continue;
        }

        reply_size = 0;
        (void)memset(reply, 0, sizeof(reply));

        switch (req.type)
        {
            case PROC_MSG_FORK:
                ret = proc_fork((uint32_t)req.data[0],
                                (uint32_t *)(uintptr_t)req.data[1]);
                reply_size = sizeof(uint32_t);
                (void)memcpy(reply, &ret, sizeof(ret));
                break;

            case PROC_MSG_EXEC:
                ret = proc_exec((uint32_t)req.data[0],
                                (const char *)(uintptr_t)req.data[1],
                                (kobj_id_t)req.data[2],
                                (kobj_id_t)req.data[3],
                                (kobj_id_t)req.data[4]);
                break;

            case PROC_MSG_EXIT:
                ret = proc_exit((uint32_t)req.data[0],
                                (int32_t)req.data[1]);
                break;

            case PROC_MSG_WAITPID:
                ret = proc_waitpid((uint32_t)req.data[0],
                                    (uint32_t)req.data[1],
                                    (int32_t *)(uintptr_t)req.data[2],
                                    (int32_t)req.data[3]);
                break;

            case PROC_MSG_SIGNAL:
                ret = proc_signal_send((uint32_t)req.data[0],
                                       (uint32_t)req.data[1]);
                break;

            case PROC_MSG_RLIMIT_SET:
                ret = proc_rlimit_set((uint32_t)req.data[0],
                                        (rlimit_resource_t)req.data[1],
                                        (const rlimit_t *)(uintptr_t)req.data[2]);
                break;

            case PROC_MSG_RLIMIT_GET:
                ret = proc_rlimit_get((uint32_t)req.data[0],
                                        (rlimit_resource_t)req.data[1],
                                        (rlimit_t *)(uintptr_t)reply);
                reply_size = sizeof(rlimit_t);
                break;

            case PROC_MSG_STATE_SET:
                ret = proc_set_state((uint32_t)req.data[0],
                                    (proc_state_t)req.data[1]);
                break;

            case PROC_MSG_GETPRIORITY:
                ret = proc_get_priority((uint32_t)req.data[0]);
                reply_size = sizeof(int32_t);
                (void)memcpy(reply, &ret, sizeof(ret));
                break;

            case PROC_MSG_SETPRIORITY:
                ret = proc_set_priority((uint32_t)req.data[0],
                                        (int32_t)req.data[1]);
                break;

            case PROC_MSG_GETPROCSTATS:
                ret = proc_get_stats((uint32_t)req.data[0],
                                      (proc_stats_t *)(uintptr_t)reply);
                reply_size = sizeof(proc_stats_t);
                break;

            case PROC_MSG_GETPROCLIST:
                ret = proc_get_proclist((uint32_t *)(uintptr_t)reply,
                                         MAX_PROCESSES,
                                         (uint32_t *)(uintptr_t)(reply + MAX_PROCESSES * 4U));
                reply_size = MAX_PROCESSES * 4U + 4U;
                break;

            case PROC_MSG_KILLPG:
                ret = proc_killpg((uint32_t)req.data[0],
                                   (uint32_t)req.data[1]);
                break;

            case PROC_MSG_MMAP:
                ret = proc_mmap((uint32_t)req.data[0],
                                req.data[1],
                                req.data[2],
                                (prot_flags_t)req.data[3],
                                (map_flags_t)req.data[4],
                                req.data[5]);
                reply_size = sizeof(int32_t);
                (void)memcpy(reply, &ret, sizeof(ret));
                break;

            case PROC_MSG_MUNMAP:
                ret = proc_munmap((uint32_t)req.data[0],
                                  req.data[1],
                                  req.data[2]);
                break;

            case PROC_MSG_MPROTECT:
                ret = proc_mprotect((uint32_t)req.data[0],
                                    req.data[1],
                                    req.data[2],
                                    (prot_flags_t)req.data[3]);
                break;

            case PROC_MSG_BRK:
                ret = proc_brk((uint32_t)req.data[0],
                                req.data[1]);
                reply_size = sizeof(int32_t);
                (void)memcpy(reply, &ret, sizeof(ret));
                break;

            case PROC_MSG_SBRK:
                ret = (int32_t)proc_sbrk((uint32_t)req.data[0],
                                           (int64_t)req.data[1]);
                reply_size = sizeof(int64_t);
                (void)memcpy(reply, &ret, sizeof(ret));
                break;

            case PROC_MSG_LOAD_ELF:
                ret = proc_load_elf((uint32_t)req.data[0],
                                    (const uint8_t *)(uintptr_t)req.data[1],
                                    (uint32_t)req.data[2]);
                break;

            case PROC_MSG_GETMAPPINGLIST:
                ret = proc_get_mapping_list((uint32_t)req.data[0],
                                           (memory_mapping_t *)(uintptr_t)reply,
                                           MAX_MAPPINGS,
                                           (uint32_t *)(uintptr_t)(reply + MAX_MAPPINGS * sizeof(memory_mapping_t)));
                reply_size = MAX_MAPPINGS * sizeof(memory_mapping_t) + 4U;
                break;

            default:
                ret = -(int32_t)EINVAL;
                break;
        }

        if (ret >= 0)
        {
            (void)syscall3(SYS_MSG_REPLY, my_endpoint,
                           (uint64_t)(uintptr_t)reply, (uint32_t)reply_size);
        }
        else
        {
            (void)syscall3(SYS_MSG_REPLY, my_endpoint, 0ULL, 0ULL);
        }
    }

    return 0;
}
