/**
 * @file    syscall_dispatch.c
 * @brief   系统调用分发器实现
 * @author  AISafe64 Team
 * @date    2026-04-05
 * @version 1.0
 *
 * @details 本文件实现了内核态系统调用分发器：
 *          - 从 syscall_frame_t 提取系统调用号和参数
 *          - 根据调用号分发到对应子系统 API
 *          - 将返回值写入 frame->x0
 *
 *          ARM64 约定：
 *          - x8 = 系统调用号
 *          - x0-x6 = 参数（最多 7 个）
 *          - x0 = 返回值
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: API-001~004
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */

#include <kernel/syscall.h>
#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/errno.h>
#include <kernel/uaccess.h>
#include <kernel/klog.h>
#include <kernel/ramfs.h>
#include <stdint.h>

/* 子系统头文件 */
#include <kernel/ipc_channel.h>
#include <kernel/ipc_endpoint.h>
#include <kernel/ipc_notification.h>
#include <kernel/capability.h>
#include <kernel/cspace.h>
#include <kernel/page_table.h>
#include <kernel/irq.h>
#include <kernel/phys_mem.h>
#include <kernel/mmu.h>
#include "thread.h"

/* ========================================================================
 * 用户态驱动内存映射支持
 *
 * @details 简单的用户虚拟地址 bump allocator，用于 SYS_VM_MAP 分配
 *          用户态 MMIO/DMA 映射的虚拟地址。从 0x10000000 开始递增
 *          （避开 ELF 加载区 0x400000 和栈区）。
 * ======================================================================== */

/** @brief 用户驱动映射区起始地址（避开 ELF@0x400000 和栈） */
#define USER_MMAP_BASE  ((uint64_t)0x10000000ULL)

/** @brief 每个线程的映射区跨度（16MB） */
#define USER_MMAP_GAP   ((uint64_t)0x01000000ULL)

/**
 * @brief 每线程的映射区当前分配指针（bump allocator）
 *
 * @details 原先使用全局计数器，多进程会互相覆盖地址区间。
 *          现改为 per-thread 起始值，每个用户线程从
 *          USER_MMAP_BASE + tid * USER_MMAP_GAP 开始独立分配。
 */
static uint64_t s_thread_mmap_ptr[CONFIG_MAX_THREADS];

/**
 * @brief 分配用户虚拟地址区间（per-thread）
 *
 * @param tid  当前线程 ID
 * @param size 需要的字节数（将按页对齐）
 *
 * @return 分配的起始虚拟地址，tid 越界返回 0
 */
static uint64_t user_mmap_alloc(thread_id_t tid, uint64_t size)
{
    uint64_t addr;
    uint64_t aligned_size;

    if ((uint32_t)tid >= CONFIG_MAX_THREADS)
    {
        return 0ULL;
    }

    /* 首次分配：从该线程的基址开始 */
    if (s_thread_mmap_ptr[tid] == 0ULL)
    {
        s_thread_mmap_ptr[tid] = USER_MMAP_BASE
                               + (uint64_t)tid * USER_MMAP_GAP;
    }

    addr = s_thread_mmap_ptr[tid];
    aligned_size = (size + (uint64_t)PAGE_SIZE_4K - 1ULL)
                 & ~((uint64_t)PAGE_SIZE_4K - 1ULL);
    s_thread_mmap_ptr[tid] += aligned_size;

    return addr;
}

/**
 * @brief 获取当前线程的用户 PGD（用于页表映射）
 *
 * @return 用户 PGD 虚拟地址，非用户线程返回 0
 */
static page_table_t *get_current_user_pgd(void)
{
    struct KThread *current = kthread_get_current();

    if ((current == NULL) || (current->is_user == 0U) || (current->user_pgd == 0ULL))
    {
        return NULL;
    }
    return (page_table_t *)(uintptr_t)current->user_pgd;
}

/**
 * @brief 校验物理地址是否为合法的设备 MMIO 区域
 *
 * @details SYS_VM_MAP 在 paddr != 0 时做 MMIO 直接映射，原先允许映射任意
 *          物理地址，攻击者可映射内核物理区（0x40000000+）读取内核内存。
 *          现通过白名单仅放行已知设备的 MMIO 地址范围。
 *
 * @param paddr 待校验的物理地址
 *
 * @retval true  地址落在允许的设备 MMIO 范围内
 * @retval false 地址不在白名单内（拒绝映射）
 */
static bool is_valid_mmio_addr(uint64_t paddr)
{
    /* UART: 0x09000000-0x09000FFF */
    if ((paddr >= 0x09000000ULL) && (paddr < 0x09001000ULL))
    {
        return true;
    }
    /* GIC: 0x08000000-0x08010FFF */
    if ((paddr >= 0x08000000ULL) && (paddr < 0x08011000ULL))
    {
        return true;
    }
    /* virtio-mmio: 0x0A000000-0x0A003FFF */
    if ((paddr >= 0x0A000000ULL) && (paddr < 0x0A004000ULL))
    {
        return true;
    }
    return false;
}

/* ========================================================================
 * ENOSYS 定义
 * ======================================================================== */

#ifndef ENOSYS
#define ENOSYS  38
#endif

/* ========================================================================
 * 线程能力检查（P1-13）
 * ======================================================================== */

/**
 * @brief 检查当前线程是否拥有对目标 tid 的线程能力
 *
 * @details 安全模型要求：跨线程管理操作（SUSPEND/RESUME/SET_PRIORITY）
 *          必须验证调用者持有指向目标线程的能力（KOBJ_THREAD + WRITE）。
 *
 *          双路径访问控制（与 endpoint_check_access 一致）：
 *          1. 当前线程有 CSpace 时，遍历能力表查找指向目标 tid 的线程能力，
 *             通过权限位校验 WRITE 权限。未找到或权限不足返回 -EACCES。
 *          2. 当前线程无 CSpace 时：内核线程（!is_user）放行，保持与内核
 *             管理路径的兼容性；用户态线程（is_user）必须有 CSpace，
 *             否则返回 -EACCES 以防止绕过权限检查。
 *
 * @param target_tid 目标线程 ID
 *
 * @return KERNEL_OK 有权限（有 CSpace 且找到匹配能力 / 内核线程放行）
 * @return -EACCES   用户态线程无 CSpace，或有 CSpace 但无匹配能力/权限不足
 */
static kernel_status_t thread_check_cap(thread_id_t target_tid)
{
    KThread_t *current = kthread_get_current();

    if (current == NULL)
    {
        return -(int32_t)ESRCH;
    }

    /*
     * 无 CSpace 的线程：内核线程（!is_user）放行，兼容内核管理路径；
     * 用户态线程（is_user）必须有能力空间，否则拒绝以避免绕过权限检查。
     */
    if (current->cspace == NULL)
    {
        if (current->is_user != 0U)
        {
            return -(int32_t)EACCES;
        }
        return KERNEL_OK;
    }

    /* 有 CSpace：必须持有指向目标线程的 KOBJ_THREAD + WRITE 能力 */
    {
        cspace_t *cs = (cspace_t *)current->cspace;
        cap_slot_t slot;
        bool found = false;

        ticket_lock_acquire(&cs->lock);
        for (slot = 0U; slot < cs->capacity; slot++)
        {
            cap_t *cap = cspace_lookup(cs, slot);
            if ((cap != NULL) &&
                (cap->state == CAP_STATE_VALID) &&
                (cap->kobj_type == KOBJ_THREAD) &&
                (cap->kobj_id == (kobj_id_t)target_tid))
            {
                /* 找到匹配能力，校验 WRITE 权限 */
                if ((cap->rights & CAP_RIGHT_WRITE) == CAP_RIGHT_WRITE)
                {
                    found = true;
                }
                break;
            }
        }
        ticket_lock_release(&cs->lock);

        if (!found)
        {
            return -(int32_t)EACCES;
        }
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 线程管理系统调用分发
 * ======================================================================== */

/**
 * @brief 线程管理系统调用分发
 *
 * @param frame 系统调用栈帧
 *
 * @note 对应需求: API-001
 */
static void dispatch_thread(syscall_frame_t *frame)
{
    uint32_t nr = (uint32_t)frame->x8;

    switch (nr)
    {
        case SYS_THREAD_CREATE:
        {
            /* x0=entry 函数地址, x1=arg, x2=priority */
            kthread_entry_t entry = (kthread_entry_t)(uintptr_t)frame->x0;
            void *arg = (void *)(uintptr_t)frame->x1;
            priority_t prio = (priority_t)frame->x2;
            thread_id_t tid;

            if (entry == NULL)
            {
                frame->x0 = (uint64_t)(-(int64_t)EINVAL);
                break;
            }

            tid = kthread_create("user_thr", entry, arg, prio,
                                   KTHREAD_POLICY_RR,
                                   CONFIG_STACK_SIZE_DEFAULT);
            if (tid != THREAD_ID_INVALID)
            {
                frame->x0 = (uint64_t)tid;
            }
            else
            {
                frame->x0 = (uint64_t)(-(int64_t)ENOMEM);
            }
            break;
        }

        case SYS_THREAD_EXIT:
        {
            /* x0=status - 调用 kthread_exit 退出当前线程 */
            (void)frame->x0;
            kthread_exit();
            /* kthread_exit 不返回 */
            frame->x0 = 0U;
            break;
        }

        case SYS_THREAD_SUSPEND:
        {
            /* P1-13：跨线程操作必须校验线程能力（KOBJ_THREAD + WRITE） */
            thread_id_t tid = (thread_id_t)frame->x0;
            kernel_status_t acc = thread_check_cap(tid);
            if (acc != KERNEL_OK)
            {
                frame->x0 = (uint64_t)(int64_t)acc;
                break;
            }
            /* 内核 API 返回负错误码，直接传递 */
            kernel_status_t ret = kthread_suspend(tid);
            frame->x0 = (uint64_t)(int64_t)ret;
            break;
        }

        case SYS_THREAD_RESUME:
        {
            /* P1-13：跨线程操作必须校验线程能力（KOBJ_THREAD + WRITE） */
            thread_id_t tid = (thread_id_t)frame->x0;
            kernel_status_t acc = thread_check_cap(tid);
            if (acc != KERNEL_OK)
            {
                frame->x0 = (uint64_t)(int64_t)acc;
                break;
            }
            kernel_status_t ret = kthread_resume(tid);
            frame->x0 = (uint64_t)(int64_t)ret;
            break;
        }

        case SYS_THREAD_SET_PRIORITY:
        {
            /* P1-13：跨线程操作必须校验线程能力（KOBJ_THREAD + WRITE） */
            thread_id_t tid = (thread_id_t)frame->x0;
            kernel_status_t acc = thread_check_cap(tid);
            if (acc != KERNEL_OK)
            {
                frame->x0 = (uint64_t)(int64_t)acc;
                break;
            }
            priority_t prio = (priority_t)frame->x1;
            kernel_status_t ret = kthread_set_priority(tid, prio);
            frame->x0 = (uint64_t)(int64_t)ret;
            break;
        }

        case SYS_THREAD_SET_AFFINITY:
        {
            /* x0=tid, x1=affinity_mask */
            (void)frame->x0;
            (void)frame->x1;
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }

        case SYS_THREAD_YIELD:
        {
            /* 让出 CPU - 调用调度器 */
            extern void schedule(void);
            schedule();
            frame->x0 = 0U;
            break;
        }

        case SYS_THREAD_GET_ID:
        {
            /* 从当前 TCB 获取线程 ID */
            thread_id_t tid = kthread_get_current_tid();
            if (tid != THREAD_ID_INVALID)
            {
                frame->x0 = (uint64_t)tid;
            }
            else
            {
                frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            }
            break;
        }

        default:
        {
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }
    }
}

/* ========================================================================
 * IPC 操作系统调用分发
 * ======================================================================== */

/**
 * @brief IPC 操作系统调用分发
 *
 * @param frame 系统调用栈帧
 *
 * @note 对应需求: API-002
 */
static void dispatch_ipc(syscall_frame_t *frame)
{
    uint32_t nr = (uint32_t)frame->x8;
    kernel_status_t ret;

    switch (nr)
    {
        case SYS_CHANNEL_CREATE:
        {
            /* x0=owner_tid (0=current) */
            kobj_id_t ch_id;
            thread_id_t tid = (thread_id_t)frame->x0;
            ret = ipc_channel_create(tid, &ch_id);
            if (ret == KERNEL_OK)
            {
                frame->x0 = (uint64_t)ch_id;
            }
            else
            {
                frame->x0 = (uint64_t)(-(int64_t)ret);
            }
            break;
        }

        case SYS_CHANNEL_DESTROY:
        {
            /* x0=channel_id */
            kobj_id_t ch_id = (kobj_id_t)frame->x0;
            ret = ipc_channel_destroy(ch_id);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_CONNECT_ATTACH:
        {
            /* x0=client_tid, x1=channel_id */
            kobj_id_t conn_id;
            thread_id_t client_tid = (thread_id_t)frame->x0;
            kobj_id_t ch_id = (kobj_id_t)frame->x1;
            ret = ipc_connect_attach(client_tid, ch_id, &conn_id);
            if (ret == KERNEL_OK)
            {
                frame->x0 = (uint64_t)conn_id;
            }
            else
            {
                frame->x0 = (uint64_t)(-(int64_t)ret);
            }
            break;
        }

        case SYS_CONNECT_DETACH:
        {
            /* x0=conn_id */
            kobj_id_t conn_id = (kobj_id_t)frame->x0;
            ret = ipc_connect_detach(conn_id);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_MSG_SEND:
        {
            /* x0=ep_id, x1=send_buf, x2=send_size, x3=recv_buf, x4=recv_size */
            kobj_id_t ep_id = (kobj_id_t)frame->x0;
            ipc_msg_tag_t tag;
            tag.value = 0ULL;
            const void *send_buf = (const void *)(uintptr_t)frame->x1;
            uint32_t send_size = (uint32_t)frame->x2;
            void *recv_buf = (void *)(uintptr_t)frame->x3;
            uint32_t recv_size = (uint32_t)frame->x4;

            /* 用户指针验证 */
            if ((send_size > 0U) && (!access_ok(send_buf, send_size)))
            {
                frame->x0 = (uint64_t)(-(int64_t)EFAULT);
                break;
            }
            if ((recv_size > 0U) && (!access_ok(recv_buf, recv_size)))
            {
                frame->x0 = (uint64_t)(-(int64_t)EFAULT);
                break;
            }

            ret = ipc_msg_send(ep_id, tag, send_buf, send_size, recv_buf, recv_size);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_MSG_RECV:
        {
            /* x0=ep_id, x1=recv_buf, x2=recv_size */
            kobj_id_t ep_id = (kobj_id_t)frame->x0;
            ipc_msg_tag_t tag;
            tag.value = 0ULL;
            void *recv_buf = (void *)(uintptr_t)frame->x1;
            uint32_t recv_size = (uint32_t)frame->x2;

            /* 用户指针验证 */
            if ((recv_size > 0U) && (!access_ok(recv_buf, recv_size)))
            {
                frame->x0 = (uint64_t)(-(int64_t)EFAULT);
                break;
            }

            ret = ipc_msg_receive(ep_id, &tag, recv_buf, recv_size);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_MSG_REPLY:
        {
            /* x0=ep_id, x1=reply_buf, x2=reply_size */
            kobj_id_t ep_id = (kobj_id_t)frame->x0;
            const void *reply_buf = (const void *)(uintptr_t)frame->x1;
            uint32_t reply_size = (uint32_t)frame->x2;

            /* 用户指针验证 */
            if ((reply_size > 0U) && (!access_ok(reply_buf, reply_size)))
            {
                frame->x0 = (uint64_t)(-(int64_t)EFAULT);
                break;
            }

            ret = ipc_msg_reply(ep_id, 0, reply_buf, reply_size);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_PULSE_SEND:
        {
            /* x0=conn_id, x1=code, x2=value */
            kobj_id_t conn_id = (kobj_id_t)frame->x0;
            int32_t code = (int32_t)frame->x1;
            int32_t value = (int32_t)frame->x2;
            ret = ipc_pulse_send(conn_id, (priority_t)128U, code, value);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_NOTIFICATION_SIGNAL:
        {
            /* x0=notif_id, x1=signals */
            kobj_id_t notif_id = (kobj_id_t)frame->x0;
            uint64_t signals = frame->x1;
            ret = ipc_notification_signal(notif_id, signals);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_NOTIFICATION_WAIT:
        {
            /* x0=notif_id, x1=signals_ptr */
            kobj_id_t notif_id = (kobj_id_t)frame->x0;
            uint64_t *signals_ptr = (uint64_t *)(uintptr_t)frame->x1;
            uint64_t triggered = 0ULL;

            /* 用户指针验证 */
            if ((signals_ptr != NULL) && (!access_ok(signals_ptr, sizeof(uint64_t))))
            {
                frame->x0 = (uint64_t)(-(int64_t)EFAULT);
                break;
            }

            ret = ipc_notification_wait(notif_id, 0xFFFFFFFFFFFFFFFFULL, &triggered);
            if ((ret == KERNEL_OK) && (signals_ptr != NULL))
            {
                *signals_ptr = triggered;
            }
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_EP_CREATE:
        {
            /* x0=owner_tid (0=current) */
            kobj_id_t ep_id;
            thread_id_t tid = (thread_id_t)frame->x0;
            if (tid == 0U)
            {
                tid = kthread_get_current_tid();
            }
            ret = ipc_endpoint_create(tid, &ep_id);
            if (ret == KERNEL_OK)
            {
                frame->x0 = (uint64_t)ep_id;
            }
            else
            {
                frame->x0 = (uint64_t)(-(int64_t)ret);
            }
            break;
        }

        default:
        {
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }
    }
}

/* ========================================================================
 * 内存管理系统调用分发
 * ======================================================================== */

/**
 * @brief 内存管理系统调用分发
 *
 * @param frame 系统调用栈帧
 *
 * @note 对应需求: API-003
 */
static void dispatch_memory(syscall_frame_t *frame)
{
    uint32_t nr = (uint32_t)frame->x8;

    switch (nr)
    {
        case SYS_VMSPACE_CREATE:
        {
            /* TODO: 需要 vmspace_create 返回 ID */
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }

        case SYS_VMSPACE_DESTROY:
        {
            /* x0=vspace_id */
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }

        case SYS_VM_MAP:
        {
            /*
             * 映射物理内存到用户空间（MMIO 或 DMA）
             *
             * 参数：x0 = paddr（0=分配 DMA 物理页，非 0=MMIO 直接映射）
             *       x1 = size（字节数）
             *       x2 = perm_flags（PAGE_PERM_* 组合）
             * 返回：x0 = 用户虚拟地址（成功）或负错误码
             */
            uint64_t paddr = frame->x0;
            uint64_t size = frame->x1;
            page_perm_t perm = (page_perm_t)frame->x2;
            page_table_t *user_pgd = get_current_user_pgd();

            if (user_pgd == NULL)
            {
                frame->x0 = (uint64_t)(-(int64_t)EPERM);
                break;
            }

            /*
             * MMIO 白名单校验：paddr != 0 时必须是合法的设备 MMIO 地址，
             * 防止用户态映射内核物理内存（0x40000000+）。逐页校验，确保
             * 整个请求区间 [paddr, paddr+size) 均落在白名单范围内。
             */
            if (paddr != 0ULL)
            {
                uint64_t mmio_off;
                for (mmio_off = 0U; mmio_off < size;
                     mmio_off += (uint64_t)PAGE_SIZE_4K)
                {
                    if (!is_valid_mmio_addr(paddr + mmio_off))
                    {
                        frame->x0 = (uint64_t)(-(int64_t)EPERM);
                        break;
                    }
                }
                if (mmio_off < size)
                {
                    break;
                }
            }

            /* 分配用户虚拟地址（per-thread） */
            uint64_t vaddr = user_mmap_alloc(kthread_get_current_tid(), size);
            uint64_t offset;
            kernel_status_t map_ret = KERNEL_OK;

            /* 逐页映射 */
            for (offset = 0U; offset < size; offset += (uint64_t)PAGE_SIZE_4K)
            {
                uint64_t cur_paddr;
                if (paddr == 0ULL)
                {
                    /* DMA 分配：分配连续物理页 */
                    cur_paddr = phys_mem_alloc_page();
                    if (cur_paddr == 0ULL)
                    {
                        frame->x0 = (uint64_t)(-(int64_t)ENOMEM);
                        break;
                    }
                }
                else
                {
                    /* MMIO 直接映射（已通过白名单校验） */
                    cur_paddr = paddr + offset;
                }

                map_ret = page_table_map(user_pgd, vaddr + offset,
                                         cur_paddr, perm, true);
                if (map_ret != KERNEL_OK)
                {
                    frame->x0 = (uint64_t)(-(int64_t)ENOMEM);
                    break;
                }
            }

            if (offset >= size)
            {
                frame->x0 = vaddr;
            }
            break;
        }

        case SYS_VM_UNMAP:
        {
            /* x0=addr, x1=size（简化实现：仅释放虚拟地址，物理页由 DMA 跟踪释放） */
            /* 当前简化：返回成功（物理页释放延后实现） */
            frame->x0 = 0ULL;
            break;
        }

        case SYS_VM_PROTECT:
        {
            /* x0=vspace_id, x1=addr, x2=flags */
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }

        case SYS_VIRT_TO_PHYS:
        {
            /*
             * 查询用户虚拟地址对应的物理地址（DMA 用，写 virtqueue 寄存器需要）
             *
             * 参数：x0 = 用户虚拟地址
             * 返回：x0 = 物理地址（成功）或负错误码
             */
            page_table_t *user_pgd = get_current_user_pgd();
            paddr_t paddr_result;

            if (user_pgd == NULL)
            {
                frame->x0 = (uint64_t)(-(int64_t)EPERM);
                break;
            }

            if (page_table_lookup(user_pgd, (vaddr_t)frame->x0, &paddr_result) == KERNEL_OK)
            {
                frame->x0 = (uint64_t)paddr_result;
            }
            else
            {
                frame->x0 = (uint64_t)(-(int64_t)EFAULT);
            }
            break;
        }

        default:
        {
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }
    }
}

/* ========================================================================
 * 能力管理系统调用分发
 * ======================================================================== */

/**
 * @brief 能力管理系统调用分发
 *
 * @param frame 系统调用栈帧
 *
 * @note 对应需求: API-004
 */
static void dispatch_capability(syscall_frame_t *frame)
{
    uint32_t nr = (uint32_t)frame->x8;
    kernel_status_t ret;

    /*
     * P1-13 安全修复：能力操作必须绑定到当前线程的 CSpace。
     *
     * 原先 cap_copy/move/revoke/delete 直接采用用户态传入的 src_cspace/
     * dst_cspace 参数，未校验其是否归属当前线程，导致任何线程可操作任意
     * CSpace（跨 CSpace 越权）。
     *
     * 现统一从 current->cspace 取出 root_slot 作为操作的目标 CSpace 根，
     * 禁止用户直接指定 CSpace root。无 CSpace 的线程（早期内核线程）返回
     * -ENOSYS，使其无法触达能力管理路径。
     */
    cspace_t *cs = (cspace_t *)kthread_get_current()->cspace;
    if (cs == NULL)
    {
        frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
        return;
    }
    cap_slot_t cs_root = cs->root_slot;

    switch (nr)
    {
        case SYS_CSPACE_CREATE:
        {
            /* x0=capacity */
            uint32_t capacity = (uint32_t)frame->x0;
            cspace_t *new_cs;
            ret = cspace_create(capacity, &new_cs);
            if (ret == KERNEL_OK)
            {
                frame->x0 = (uint64_t)new_cs->header.id;
            }
            else
            {
                frame->x0 = (uint64_t)(-(int64_t)ret);
            }
            break;
        }

        case SYS_CAP_COPY:
        {
            /* x1=src_slot；源/目标 CSpace 均强制为当前线程的 CSpace */
            cap_slot_t src_slot = (cap_slot_t)frame->x1;
            /* 默认分配到 slot 1，不降权 */
            ret = cap_copy(cs_root, src_slot, cs_root, (cap_slot_t)1U, 0U);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_CAP_MOVE:
        {
            /* x1=src_slot；源/目标 CSpace 均强制为当前线程的 CSpace */
            cap_slot_t src_slot = (cap_slot_t)frame->x1;
            ret = cap_move(cs_root, src_slot, cs_root, (cap_slot_t)1U);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_CAP_REVOKE:
        {
            /* x1=slot；CSpace 根强制为当前线程的 CSpace */
            cap_slot_t slot = (cap_slot_t)frame->x1;
            ret = cap_revoke(cs_root, slot);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_CAP_DELETE:
        {
            /* x1=slot；CSpace 根强制为当前线程的 CSpace */
            cap_slot_t slot = (cap_slot_t)frame->x1;
            ret = cap_delete(cs_root, slot);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        default:
        {
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }
    }
}

/* ========================================================================
 * 中断管理系统调用分发
 * ======================================================================== */

/**
 * @brief 中断管理系统调用分发
 *
 * @details 对接新中断管理子系统（）：
 *          - SYS_INTERRUPT_ATTACH：绑定中断到 notification 对象，返回 attach_id
 *          - SYS_INTERRUPT_DETACH_BY_ID：按 attach_id 精确解绑
 *          - SYS_INTERRUPT_DETACH：按 irq 号解绑所有
 *          - SYS_INTERRUPT_MASK/UNMASK：临时屏蔽/恢复
 *          - SYS_INTERRUPT_GET_STATS：查询统计
 *
 *          attach 成功返回 >0 的 attach_id（写入 x0），失败返回负错误码。
 *
 * @param frame 系统调用栈帧
 */
static void dispatch_interrupt(syscall_frame_t *frame)
{
    uint32_t nr = (uint32_t)frame->x8;

    switch (nr)
    {
        case SYS_INTERRUPT_ATTACH:
        {
            /*
             * 绑定硬件中断到 notification 对象
             *
             * 参数：x0 = irq 中断号
             *       x1 = notification_id 通知对象 ID
             *       x2 = trigger 触发模式（0=上升沿 1=下降沿 2=高电平 3=低电平）
             *       x3 = priority 优先级
             * 返回：x0 = attach_id(>0) 成功 或负错误码
             *
             * 内核 irq_attach 校验 IRQ 能力后配置 GIC，并在中断发生时
             * 调用 ipc_notification_signal 通知用户态。
             */
            uint32_t irq = (uint32_t)frame->x0;
            kobj_id_t notif_id = (kobj_id_t)frame->x1;
            irq_trigger_t trigger = (irq_trigger_t)frame->x2;
            uint8_t priority = (uint8_t)frame->x3;
            uint32_t flags = (uint32_t)frame->x4;
            int32_t attach_id;

            /* 默认触发模式：高电平 */
            if (trigger > IRQ_TRIGGER_LEVEL_LOW)
            {
                trigger = IRQ_TRIGGER_LEVEL_HIGH;
            }
            attach_id = irq_attach(irq, NULL, NULL, notif_id,
                                   trigger, priority, flags);
            /* attach_id > 0 成功；<= 0 为负错误码 */
            frame->x0 = (uint64_t)(int64_t)attach_id;
            break;
        }

        case SYS_INTERRUPT_DETACH:
        {
            /* x0=irq：解绑该中断的所有 handler */
            uint32_t irq = (uint32_t)frame->x0;
            kernel_status_t ret = irq_detach_all(irq);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL
                                 : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_INTERRUPT_DETACH_BY_ID:
        {
            /* x0=irq, x1=attach_id：按 IRQ+ID 精确解绑 */
            uint32_t irq = (uint32_t)frame->x0;
            uint32_t attach_id = (uint32_t)frame->x1;
            kernel_status_t ret = irq_detach_by_id(irq, attach_id);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL
                                 : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_INTERRUPT_MASK:
        {
            /* x0=irq：临时屏蔽 */
            uint32_t irq = (uint32_t)frame->x0;
            kernel_status_t ret = irq_mask(irq);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL
                                 : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_INTERRUPT_UNMASK:
        {
            /* x0=irq：恢复屏蔽 */
            uint32_t irq = (uint32_t)frame->x0;
            kernel_status_t ret = irq_unmask(irq);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL
                                 : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_INTERRUPT_GET_STATS:
        {
            /* x0=irq, x1=stats_ptr（用户缓冲） */
            uint32_t irq = (uint32_t)frame->x0;
            irq_stats_t *user_stats = (irq_stats_t *)(uintptr_t)frame->x1;
            irq_stats_t stats;

            if (!access_ok(user_stats, sizeof(irq_stats_t)))
            {
                frame->x0 = (uint64_t)(-(int64_t)EFAULT);
                break;
            }

            kernel_status_t ret = irq_get_stats(irq, &stats);
            if (ret == KERNEL_OK)
            {
                *user_stats = stats;
                frame->x0 = 0ULL;
            }
            else
            {
                frame->x0 = (uint64_t)(-(int64_t)ret);
            }
            break;
        }

        default:
        {
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }
    }
}

/* ========================================================================
 * 进程管理系统调用分发
 * ======================================================================== */

/**
 * @brief 进程管理系统调用分发（0x0500-0x05FF）
 */
static void dispatch_process(syscall_frame_t *frame)
{
    extern kernel_status_t process_create(uint32_t parent_pid, uint32_t *out_pid);
    extern kernel_status_t process_exit(uint32_t pid, int32_t status);
    extern kernel_status_t process_wait(uint32_t pid, int32_t *out_status);
    extern uint32_t process_getpid(void);

    uint32_t nr = (uint32_t)frame->x8;

    switch (nr)
    {
        case SYS_PROCESS_CREATE:
        {
            uint32_t parent_pid = (uint32_t)frame->x0;
            uint32_t pid = 0U;
            kernel_status_t ret = process_create(parent_pid, &pid);
            if (ret == KERNEL_OK)
            {
                frame->x0 = (uint64_t)pid;
            }
            else
            {
                frame->x0 = (uint64_t)(-(int64_t)ret);
            }
            break;
        }

        case SYS_PROCESS_EXIT:
        {
            uint32_t pid = (uint32_t)frame->x0;
            int32_t status = (int32_t)frame->x1;
            kernel_status_t ret = process_exit(pid, status);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_PROCESS_WAIT:
        {
            uint32_t pid = (uint32_t)frame->x0;
            int32_t status = 0;
            kernel_status_t ret = process_wait(pid, &status);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? (uint64_t)status : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_PROCESS_GETPID:
        {
            frame->x0 = (uint64_t)process_getpid();
            break;
        }

        default:
        {
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }
    }
}

/* ========================================================================
 * 信号系统调用分发
 * ======================================================================== */

/**
 * @brief 信号系统调用分发（0x0600-0x06FF）
 */
static void dispatch_filesys(syscall_frame_t *frame);
static void dispatch_signal(syscall_frame_t *frame)
{
    extern kernel_status_t signal_kill(thread_id_t target_tid, uint32_t sig);
    extern kernel_status_t signal_action(uint32_t sig, uint64_t handler);
    extern kernel_status_t signal_procmask(uint64_t mask);

    uint32_t nr = (uint32_t)frame->x8;

    switch (nr)
    {
        case SYS_SIGNAL_ACTION:
        {
            uint32_t sig = (uint32_t)frame->x0;
            uint64_t handler = frame->x1;
            kernel_status_t ret = signal_action(sig, handler);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_SIGNAL_KILL:
        {
            thread_id_t tid = (thread_id_t)frame->x0;
            uint32_t sig = (uint32_t)frame->x1;
            kernel_status_t ret = signal_kill(tid, sig);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_SIGNAL_PROCMASK:
        {
            uint64_t mask = frame->x0;
            kernel_status_t ret = signal_procmask(mask);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        default:
        {
            /* 文件系统 syscall 0x0680-0x0685 */
            if ((nr >= 0x0680U) && (nr <= 0x0685U))
            {
                dispatch_filesys(frame);
                break;
            }
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }
    }
}

/* ========================================================================
 * 文件系统系统调用分发（0x0680-0x0685，RAMFS 直通）
 * ======================================================================== */

/**
 * @brief 文件系统系统调用分发
 *
 * @details 直接调用内核 RAMFS，不需要用户态 FS 服务。
 *          musl 的 fs_ipc.c 在 s_fs_endpoint == -1 时回退到此路径。
 *
 * @param frame 系统调用栈帧
 */
static void dispatch_filesys(syscall_frame_t *frame)
{
    uint32_t nr = (uint32_t)frame->x8;

    switch (nr)
    {
        case SYS_OPEN:
        {
            const char *path = (const char *)(uintptr_t)frame->x0;
            uint32_t flags = (uint32_t)frame->x1;
            int32_t fd = ramfs_open(path, flags);
            frame->x0 = (uint64_t)((fd >= 0) ? (uint64_t)fd : (uint64_t)(-(int64_t)EINVAL));
            break;
        }

        case SYS_CLOSE:
        {
            int32_t fd = (int32_t)frame->x0;
            int32_t ret = ramfs_close(fd);
            frame->x0 = (uint64_t)((ret >= 0) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_READ:
        {
            int32_t fd = (int32_t)frame->x0;
            void *buf = (void *)(uintptr_t)frame->x1;
            uint32_t count = (uint32_t)frame->x2;

            if ((count > 0U) && (!access_ok(buf, count)))
            {
                frame->x0 = (uint64_t)(-(int64_t)EFAULT);
                break;
            }

            frame->x0 = (uint64_t)(int64_t)ramfs_read(fd, buf, count);
            break;
        }

        case SYS_WRITE:
        {
            int32_t fd = (int32_t)frame->x0;
            const void *buf = (const void *)(uintptr_t)frame->x1;
            uint32_t count = (uint32_t)frame->x2;

            if ((count > 0U) && (!access_ok(buf, count)))
            {
                frame->x0 = (uint64_t)(-(int64_t)EFAULT);
                break;
            }

            frame->x0 = (uint64_t)(int64_t)ramfs_write(fd, buf, count);
            /* fd 1/2 是 stdout/stderr → flush 到 UART */
            if ((fd == 1) || (fd == 2))
            {
                klog_flush();
            }
            break;
        }

        case SYS_LSEEK:
        {
            int32_t fd = (int32_t)frame->x0;
            int32_t offset = (int32_t)frame->x1;
            uint32_t whence = (uint32_t)frame->x2;
            frame->x0 = (uint64_t)(int64_t)ramfs_lseek(fd, offset, whence);
            break;
        }

        case SYS_FSTAT:
        {
            int32_t fd = (int32_t)frame->x0;
            void *statbuf = (void *)(uintptr_t)frame->x1;

            if (!access_ok(statbuf, 64U))
            {
                frame->x0 = (uint64_t)(-(int64_t)EFAULT);
                break;
            }

            frame->x0 = (uint64_t)((ramfs_fstat(fd, statbuf) == 0) ? 0ULL : (uint64_t)(-(int64_t)EINVAL));
            break;
        }

        default:
        {
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }
    }
}

/* ========================================================================
 * 定时器系统调用分发
 * ======================================================================== */

/**
 * @brief 定时器系统调用分发（0x0700-0x07FF）
 */
static void dispatch_timer(syscall_frame_t *frame)
{
    extern kernel_status_t user_timer_create(uint32_t owner_tid, uint32_t notify_ep, uint32_t *out_id);
    extern kernel_status_t user_timer_settime(uint32_t timer_id, uint32_t ms, uint32_t interval_ms);
    extern kernel_status_t user_timer_delete(uint32_t timer_id);
    extern uint64_t user_clock_gettime(void);
    extern kernel_status_t user_nanosleep(uint64_t ns);

    uint32_t nr = (uint32_t)frame->x8;

    switch (nr)
    {
        case SYS_TIMER_CREATE:
        {
            uint32_t notify_ep = (uint32_t)frame->x0;
            uint32_t timer_id = 0U;
            thread_id_t tid = kthread_get_current_tid();
            kernel_status_t ret = user_timer_create(tid, notify_ep, &timer_id);
            if (ret == KERNEL_OK)
            {
                frame->x0 = (uint64_t)timer_id;
            }
            else
            {
                frame->x0 = (uint64_t)(-(int64_t)ret);
            }
            break;
        }

        case SYS_TIMER_SETTIME:
        {
            uint32_t timer_id = (uint32_t)frame->x0;
            uint32_t ms = (uint32_t)frame->x1;
            uint32_t interval_ms = (uint32_t)frame->x2;
            kernel_status_t ret = user_timer_settime(timer_id, ms, interval_ms);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_TIMER_DELETE:
        {
            uint32_t timer_id = (uint32_t)frame->x0;
            kernel_status_t ret = user_timer_delete(timer_id);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_NANOSLEEP:
        {
            uint64_t ns = frame->x0;
            kernel_status_t ret = user_nanosleep(ns);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_CLOCK_GETTIME:
        {
            frame->x0 = user_clock_gettime();
            break;
        }

        default:
        {
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }
    }
}

/* ========================================================================
 * 调试/信息系统调用分发
 * ======================================================================== */

/**
 * @brief 调试/信息系统调用分发（0x0800-0x08FF）
 */
static void dispatch_debug(syscall_frame_t *frame)
{
    uint32_t nr = (uint32_t)frame->x8;

    switch (nr)
    {
        case SYS_DEBUG_PRINT:
        {
            /* x0=string_ptr, x1=length */
            const char *str = (const char *)(uintptr_t)frame->x0;
            uint64_t len = frame->x1;

            /* 安全验证：用户指针必须可访问 */
            if (!access_ok(str, len > 256U ? 256U : len))
            {
                frame->x0 = (uint64_t)(-(int64_t)EFAULT);
                break;
            }

            /* 诊断标记：确认 SVC 到达内核 */
            klog_putc('<');

            if (str != NULL)
            {
                uint64_t i;
                uint64_t max_len = (len > 256U) ? 256U : len;

                for (i = 0U; i < max_len; i++)
                {
                    char c = str[i];
                    if (c == '\0')
                    {
                        break;
                    }
                    klog_putc(c);
                }
                /* 用户态打印后立即 flush 到 UART */
                klog_flush();
                frame->x0 = 0U;
            }
            else
            {
                frame->x0 = (uint64_t)(-(int64_t)EINVAL);
            }
            break;
        }

        case SYS_SYSTEM_INFO:
        {
            /* TODO: 返回系统信息结构 */
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }

        default:
        {
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }
    }
}

/* ========================================================================
 * 系统调用主分发器
 * ======================================================================== */

/**
 * @brief 系统调用处理函数入口
 *
 * @details 由 exception.S 的 SVC handler 调用。
 *          从 frame->x8 读取系统调用号，按类别分发：
 *          - 0x00xx: 线程管理
 *          - 0x01xx: IPC 操作
 *          - 0x02xx: 内存管理
 *          - 0x03xx: 能力管理
 *          - 0x04xx: 中断管理
 *          - 0x05xx: 调试/信息
 *
 * @param frame 系统调用栈帧指针
 *
 * @note 对应需求: API-001~004
 */
void syscall_handler(syscall_frame_t *frame)
{
    uint32_t nr;
    uint8_t category;

    if (frame == NULL)
    {
        return;
    }

    nr = (uint32_t)frame->x8;
    category = (uint8_t)((nr >> 8U) & 0xFFU);

    /* 按类别分发 */
    switch ((uint32_t)category)
    {
        case 0x00U:
        {
            /* 线程管理 0x0000-0x00FF */
            dispatch_thread(frame);
            break;
        }

        case 0x01U:
        {
            /* IPC 操作 0x0100-0x01FF */
            dispatch_ipc(frame);
            break;
        }

        case 0x02U:
        {
            /* 内存管理 0x0200-0x02FF */
            dispatch_memory(frame);
            break;
        }

        case 0x03U:
        {
            /* 能力管理 0x0300-0x03FF */
            dispatch_capability(frame);
            break;
        }

        case 0x04U:
        {
            /* 中断管理 0x0400-0x04FF */
            dispatch_interrupt(frame);
            break;
        }

        case 0x05U:
        {
            /* 进程管理 0x0500-0x05FF */
            dispatch_process(frame);
            break;
        }

        case 0x06U:
        {
            /* 信号 0x0600-0x06FF */
            dispatch_signal(frame);
            break;
        }

        case 0x07U:
        {
            /* 定时器 0x0700-0x07FF */
            dispatch_timer(frame);
            break;
        }

        case 0x08U:
        {
            /* 调试/信息 0x0800-0x08FF */
            dispatch_debug(frame);
            break;
        }

        default:
        {
            /* 未识别的系统调用类别 */
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }
    }
}
