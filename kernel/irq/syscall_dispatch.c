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
#include <stdint.h>

/* 子系统头文件 */
#include <kernel/ipc_channel.h>
#include <kernel/ipc_endpoint.h>
#include <kernel/ipc_notification.h>
#include <kernel/capability.h>
#include <kernel/cspace.h>
#include <kernel/page_table.h>
#include <kernel/interrupt.h>
#include "thread.h"

/* HAL 用于 debug print */
extern void hal_uart_puts(uint64_t base, const char *str);
extern void hal_uart_putc(uint64_t base, char c);


#define QEMU_UART0_BASE  0x09000000UL

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
            /* x0=entry, x1=stack, x2=priority */
            /* TODO: 需要从调度器获取 thread_create API */
            (void)frame->x0;
            (void)frame->x1;
            (void)frame->x2;
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }

        case SYS_THREAD_EXIT:
        {
            /* x0=status */
            /* TODO: 需要 thread_exit API */
            (void)frame->x0;
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }

        case SYS_THREAD_SUSPEND:
        case SYS_THREAD_RESUME:
        case SYS_THREAD_SET_PRIORITY:
        case SYS_THREAD_SET_AFFINITY:
        {
            /* TODO: 需要线程管理 API */
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
            /* x0=ep_id, x1=send_buf, x2=send_size */
            kobj_id_t ep_id = (kobj_id_t)frame->x0;
            ipc_msg_tag_t tag;
            tag.value = 0ULL;
            const void *send_buf = (const void *)(uintptr_t)frame->x1;
            uint32_t send_size = (uint32_t)frame->x2;
            ret = ipc_msg_send(ep_id, tag, send_buf, send_size, NULL, 0U);
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
            /* x0=vspace_id, x1=addr, x2=flags */
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }

        case SYS_VM_UNMAP:
        {
            /* x0=vspace_id, x1=addr, x2=size */
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }

        case SYS_VM_PROTECT:
        {
            /* x0=vspace_id, x1=addr, x2=flags */
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
            /* x0=irq, x1=notification_cap */
            /* TODO: 需要中断绑定到通知对象的 API */
            frame->x0 = (uint64_t)(-(int64_t)ENOSYS);
            break;
        }

        case SYS_INTERRUPT_DETACH:
        {
            /* x0=irq */
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
