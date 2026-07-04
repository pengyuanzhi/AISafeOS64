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
#include <stdint.h>

/* 子系统头文件 */
#include <kernel/ipc_channel.h>
#include <kernel/ipc_endpoint.h>
#include <kernel/ipc_notification.h>
#include <kernel/capability.h>
#include <kernel/cspace.h>
#include <kernel/page_table.h>
#include <kernel/interrupt.h>
#include <kernel/phys_mem.h>
#include <kernel/mmu.h>
#include "thread.h"

/* HAL 用于 debug print */
extern void hal_uart_puts(uint64_t base, const char *str);
extern void hal_uart_putc(uint64_t base, char c);


#define QEMU_UART0_BASE  0x09000000UL

/* ========================================================================
 * 用户态驱动内存映射支持
 *
 * @details 简单的用户虚拟地址 bump allocator，用于 SYS_VM_MAP 分配
 *          用户态 MMIO/DMA 映射的虚拟地址。从 0x10000000 开始递增
 *          （避开 ELF 加载区 0x400000 和栈区）。
 * ======================================================================== */

/** @brief 用户驱动映射区起始地址（避开 ELF@0x400000 和栈） */
#define USER_MMAP_BASE  ((uint64_t)0x10000000ULL)

/** @brief 用户驱动映射区当前分配指针（bump allocator） */
static uint64_t s_user_mmap_ptr = USER_MMAP_BASE;

/**
 * @brief 分配用户虚拟地址区间
 *
 * @param size 需要的字节数（将按页对齐）
 *
 * @return 分配的起始虚拟地址
 */
static uint64_t user_mmap_alloc(uint64_t size)
{
    uint64_t addr = s_user_mmap_ptr;
    uint64_t aligned_size = (size + (uint64_t)PAGE_SIZE_4K - 1ULL)
                          & ~((uint64_t)PAGE_SIZE_4K - 1ULL);
    s_user_mmap_ptr += aligned_size;
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

/* ========================================================================
 * ENOSYS 定义
 * ======================================================================== */

#ifndef ENOSYS
#define ENOSYS  38
#endif

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
            /* 内核 API 返回负错误码，直接传递 */
            thread_id_t tid = (thread_id_t)frame->x0;
            kernel_status_t ret = kthread_suspend(tid);
            frame->x0 = (uint64_t)(int64_t)ret;
            break;
        }

        case SYS_THREAD_RESUME:
        {
            thread_id_t tid = (thread_id_t)frame->x0;
            kernel_status_t ret = kthread_resume(tid);
            frame->x0 = (uint64_t)(int64_t)ret;
            break;
        }

        case SYS_THREAD_SET_PRIORITY:
        {
            thread_id_t tid = (thread_id_t)frame->x0;
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

            /* 分配用户虚拟地址 */
            uint64_t vaddr = user_mmap_alloc(size);
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
                    /* MMIO 直接映射 */
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

    switch (nr)
    {
        case SYS_CSPACE_CREATE:
        {
            /* x0=capacity */
            uint32_t capacity = (uint32_t)frame->x0;
            cspace_t *cs;
            ret = cspace_create(capacity, &cs);
            if (ret == KERNEL_OK)
            {
                frame->x0 = (uint64_t)cs->header.id;
            }
            else
            {
                frame->x0 = (uint64_t)(-(int64_t)ret);
            }
            break;
        }

        case SYS_CAP_COPY:
        {
            /* x0=src_cspace, x1=src_slot, x2=dest_cspace */
            cap_slot_t src_cs = (cap_slot_t)frame->x0;
            cap_slot_t src_slot = (cap_slot_t)frame->x1;
            cap_slot_t dst_cs = (cap_slot_t)frame->x2;
            /* 默认分配到 slot 1，不降权 */
            ret = cap_copy(src_cs, src_slot, dst_cs, (cap_slot_t)1U, 0U);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_CAP_MOVE:
        {
            /* x0=src_cspace, x1=src_slot, x2=dest_cspace */
            cap_slot_t src_cs = (cap_slot_t)frame->x0;
            cap_slot_t src_slot = (cap_slot_t)frame->x1;
            cap_slot_t dst_cs = (cap_slot_t)frame->x2;
            ret = cap_move(src_cs, src_slot, dst_cs, (cap_slot_t)1U);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_CAP_REVOKE:
        {
            /* x0=cspace, x1=slot */
            cap_slot_t cs_root = (cap_slot_t)frame->x0;
            cap_slot_t slot = (cap_slot_t)frame->x1;
            ret = cap_revoke(cs_root, slot);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_CAP_DELETE:
        {
            /* x0=cspace, x1=slot */
            cap_slot_t cs_root = (cap_slot_t)frame->x0;
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
             * 返回：x0 = 0 成功 或负错误码
             *
             * 内核 interrupt_attach 配置 GIC 并在中断发生时
             * 调用 ipc_notification_signal 通知用户态。
             */
            uint32_t irq = (uint32_t)frame->x0;
            kobj_id_t notif_id = (kobj_id_t)frame->x1;
            kernel_status_t ret;

            /* IRQ_TRIGGER_EDGE_FALLING=2, 优先级 0xA0 */
            ret = interrupt_attach(irq, notif_id, 2U, (uint8_t)0xA0U);
            frame->x0 = (uint64_t)((ret == KERNEL_OK) ? 0ULL : (uint64_t)(-(int64_t)ret));
            break;
        }

        case SYS_INTERRUPT_DETACH:
        {
            /* x0=irq */
            uint32_t irq = (uint32_t)frame->x0;
            kernel_status_t ret = interrupt_detach(irq);
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
 * 调试/信息系统调用分发
 * ======================================================================== */

/**
 * @brief 调试/信息系统调用分发
 *
 * @param frame 系统调用栈帧
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
            hal_uart_putc((uint64_t)QEMU_UART0_BASE, '<');

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
                    hal_uart_putc((uint64_t)QEMU_UART0_BASE, c);
                }
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
            /* 调试/信息 0x0500-0x05FF */
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
