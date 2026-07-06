/**
 * @file    process.c
 * @brief   进程管理子系统实现
 * @author  AISafe64 Team
 * @date    2026-07-05
 * @version 1.0
 *
 * @details 进程 = 地址空间 + 线程组 + 资源限额。
 *          一个进程包含多个线程，共享 vmspace 和 cspace。
 *
 * @note MISRA-C:2012 合规
 *
 * @revision history
 * v1.0 2026-07-05 初始版本
 */

#include <kernel/process.h>
#include <kernel/config.h>
#include <kernel/errno.h>
#include <kernel/vmspace.h>
#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <kernel/smp.h>
#include <stdint.h>
#include <string.h>
#include "../../sched/thread.h"

#ifndef CONFIG_MAX_PROCESSES
#define CONFIG_MAX_PROCESSES 16U
#endif

/** @brief 进程描述符静态池 */
static process_t s_processes[CONFIG_MAX_PROCESSES];

/** @brief 下一个 PID（递增，不复用） */
static uint32_t s_next_pid = 1U;

/** @brief 进程子系统锁 */
static TicketLock_t s_process_lock;

/** @brief 初始化标志 */
static bool s_initialized = false;

/* ========================================================================
 * 内部辅助
 * ======================================================================== */

/**
 * @brief 分配空闲进程槽位
 *
 * @details 线性扫描找 in_use=false 的槽位。调用者持锁。
 *
 * @return 槽位指针，无空闲返回 NULL
 */
static process_t *process_alloc_slot(void)
{
    uint32_t i;
    for (i = 0U; i < CONFIG_MAX_PROCESSES; i++)
    {
        if (!s_processes[i].in_use)
        {
            return &s_processes[i];
        }
    }
    return NULL;
}

/**
 * @brief 按 PID 查找进程描述符
 *
 * @param pid 进程 ID
 * @return 描述符指针，未找到返回 NULL
 */
static process_t *process_find(uint32_t pid)
{
    uint32_t i;
    for (i = 0U; i < CONFIG_MAX_PROCESSES; i++)
    {
        if (s_processes[i].in_use && (s_processes[i].pid == pid))
        {
            return &s_processes[i];
        }
    }
    return NULL;
}

/* ========================================================================
 * 公共 API
 * ======================================================================== */

kernel_status_t process_subsys_init(void)
{
    uint32_t i;

    for (i = 0U; i < CONFIG_MAX_PROCESSES; i++)
    {
        s_processes[i].pid = 0U;
        s_processes[i].parent_pid = 0U;
        s_processes[i].vmspace = NULL;
        s_processes[i].cspace = NULL;
        s_processes[i].thread_list = NULL;
        s_processes[i].thread_count = 0U;
        s_processes[i].exit_status = 0;
        s_processes[i].in_use = false;
    }

    ticket_lock_init(&s_process_lock);
    s_next_pid = 1U;
    s_initialized = true;

    return KERNEL_OK;
}

kernel_status_t process_create(uint32_t parent_pid, uint32_t *out_pid)
{
    process_t *proc;
    vm_space_t *space;

    if ((out_pid == NULL) || !s_initialized)
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&s_process_lock);

    proc = process_alloc_slot();
    if (proc == NULL)
    {
        ticket_lock_release(&s_process_lock);
        return -(int32_t)ENOMEM;
    }

    if (vmspace_create(&space) != KERNEL_OK)
    {
        ticket_lock_release(&s_process_lock);
        return -(int32_t)ENOMEM;
    }

    proc->pid = s_next_pid;
    s_next_pid++;
    proc->parent_pid = parent_pid;
    proc->vmspace = space;
    proc->cspace = NULL;
    proc->thread_list = NULL;
    proc->thread_count = 0U;
    proc->exit_status = 0;
    proc->in_use = true;

    *out_pid = proc->pid;

    ticket_lock_release(&s_process_lock);

    return KERNEL_OK;
}

kernel_status_t process_exit(uint32_t pid, int32_t status)
{
    process_t *proc;

    if (!s_initialized)
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&s_process_lock);

    proc = process_find(pid);
    if (proc == NULL)
    {
        ticket_lock_release(&s_process_lock);
        return -(int32_t)ESRCH;
    }

    proc->exit_status = status;
    proc->in_use = false;

    if (proc->vmspace != NULL)
    {
        vmspace_destroy(proc->vmspace);
        proc->vmspace = NULL;
    }

    proc->thread_count = 0U;

    ticket_lock_release(&s_process_lock);

    return KERNEL_OK;
}

kernel_status_t process_wait(uint32_t pid, int32_t *out_status)
{
    process_t *proc;

    if ((out_status == NULL) || !s_initialized)
    {
        return -(int32_t)EINVAL;
    }

    ticket_lock_acquire(&s_process_lock);

    proc = process_find(pid);
    if (proc == NULL)
    {
        ticket_lock_release(&s_process_lock);
        *out_status = 0;
        return KERNEL_OK;
    }

    if (!proc->in_use)
    {
        *out_status = proc->exit_status;
        ticket_lock_release(&s_process_lock);
        return KERNEL_OK;
    }

    ticket_lock_release(&s_process_lock);

    return -(int32_t)EAGAIN;
}

uint32_t process_getpid(void)
{
    KThread_t *current;

    current = kthread_get_current();
    if (current == NULL)
    {
        return 0U;
    }

    return current->pid;
}
