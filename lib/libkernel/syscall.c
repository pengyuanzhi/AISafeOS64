/**
 * @file    syscall.c
 * @brief   系统调用 C 语言绑定库
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details 本文件提供用户态友好的系统调用封装函数。
 *          每个函数内部调用 syscall.h 中定义的 syscall0~syscall3
 *          底层桩函数，将 ARMv8-A SVC 调用封装为标准 C 接口。
 *
 *          封装分类：
 *          - 线程管理：创建、退出、挂起、恢复、优先级、亲和性、让出、获取 ID
 *          - IPC 操作：通道、连接、消息、脉冲、通知
 *          - 内存管理：地址空间、页面映射、权限修改
 *          - 能力管理：能力空间、复制、撤销、删除
 *          - 中断管理：中断绑定与解绑
 *          - 调试输出：内核调试打印
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: API-001~004
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/syscall.h>
#include <kernel/config.h>
#include <stdint.h>

/* ========================================================================
 * 线程管理系统调用
 * ======================================================================== */

/**
 * @brief 创建新线程
 *
 * @details 通过系统调用创建一个新的用户态线程。
 *          线程将在指定的入口地址开始执行，使用给定的栈和优先级。
 *          name 参数受限于 syscall3 的参数上限，暂未传递给内核。
 *
 * @param entry    线程入口函数地址
 * @param stack    栈基地址
 * @param priority 线程优先级（0-255）
 * @param name     线程名称字符串指针（预留参数）
 *
 * @return 成功返回线程 ID（正数），失败返回负错误码
 *
 * @retval >0 线程创建成功，返回线程 ID
 * @retval <0 线程创建失败，返回负 POSIX 错误码
 *
 * @note 入口函数不得返回，应调用 sys_thread_exit() 退出
 */
int sys_thread_create(uint64_t entry, uint64_t stack,
                      uint32_t priority, const char *name)
{
    (void)name; /* 预留参数，受限于 syscall3 参数上限暂未传递 */

    return (int)syscall3(SYS_THREAD_CREATE,
                         entry,
                         stack,
                         (uint64_t)priority);
}

/**
 * @brief 退出当前线程
 *
 * @details 终止当前正在运行的线程，返回退出状态码。
 *          此函数不会返回。
 *
 * @param status 线程退出状态码
 *
 * @note 此函数不会返回
 */
void sys_thread_exit(int status)
{
    (void)syscall1(SYS_THREAD_EXIT, (uint64_t)(int64_t)status);
}

/**
 * @brief 挂起指定线程
 *
 * @details 将目标线程从调度队列中移除，使其进入挂起状态。
 *          挂起的线程不会参与调度，直到被 sys_thread_resume() 恢复。
 *
 * @param thread_id 目标线程 ID
 *
 * @return 成功返回 0，失败返回负错误码
 *
 * @retval 0  挂起成功
 * @retval <0 挂起失败，返回负 POSIX 错误码
 */
int sys_thread_suspend(uint32_t thread_id)
{
    return (int)syscall1(SYS_THREAD_SUSPEND, (uint64_t)thread_id);
}

/**
 * @brief 恢复指定线程
 *
 * @details 将挂起的线程重新加入调度队列，使其恢复执行。
 *
 * @param thread_id 目标线程 ID
 *
 * @return 成功返回 0，失败返回负错误码
 *
 * @retval 0  恢复成功
 * @retval <0 恢复失败，返回负 POSIX 错误码
 */
int sys_thread_resume(uint32_t thread_id)
{
    return (int)syscall1(SYS_THREAD_RESUME, (uint64_t)thread_id);
}

/**
 * @brief 设置线程优先级
 *
 * @details 修改目标线程的调度优先级。
 *          优先级值越大，线程获得 CPU 时间的机会越多。
 *
 * @param thread_id 目标线程 ID
 * @param priority  新的优先级值（0-255）
 *
 * @return 成功返回 0，失败返回负错误码
 *
 * @retval 0  设置成功
 * @retval <0 设置失败，返回负 POSIX 错误码
 */
int sys_thread_set_priority(uint32_t thread_id, uint32_t priority)
{
    return (int)syscall2(SYS_THREAD_SET_PRIORITY,
                         (uint64_t)thread_id,
                         (uint64_t)priority);
}

/**
 * @brief 设置线程 CPU 亲和性
 *
 * @details 限制线程只能在指定的 CPU 核心集合上运行。
 *          通过位掩码指定允许运行的 CPU 核心列表。
 *
 * @param thread_id 目标线程 ID
 * @param cpu_mask  CPU 核心位掩码（bit i = 1 表示允许在 CPU i 上运行）
 *
 * @return 成功返回 0，失败返回负错误码
 *
 * @retval 0  设置成功
 * @retval <0 设置失败，返回负 POSIX 错误码
 */
int sys_thread_set_affinity(uint32_t thread_id, uint32_t cpu_mask)
{
    return (int)syscall2(SYS_THREAD_SET_AFFINITY,
                         (uint64_t)thread_id,
                         (uint64_t)cpu_mask);
}

/**
 * @brief 让出 CPU
 *
 * @details 主动放弃当前线程的剩余时间片，
 *          让调度器选择下一个线程运行。
 *
 * @note 此调用始终成功，不会返回错误
 */
void sys_thread_yield(void)
{
    (void)syscall0(SYS_THREAD_YIELD);
}

/**
 * @brief 获取当前线程 ID
 *
 * @details 返回调用者所在线程的唯一标识符。
 *
 * @return 当前线程 ID
 *
 * @retval >0 当前线程 ID
 */
uint32_t sys_thread_get_id(void)
{
    return (uint32_t)syscall0(SYS_THREAD_GET_ID);
}

/* ========================================================================
 * IPC 操作系统调用
 * ======================================================================== */

/**
 * @brief 创建 IPC 通道
 *
 * @details 创建一个新的 IPC 通道，用于接收来自客户端的消息和脉冲。
 *          通道是 QNX 风格消息传递的服务端入口点。
 *
 * @param flags 通道属性标志
 *
 * @return 成功返回通道 ID，失败返回负错误码
 *
 * @retval >0 通道创建成功，返回通道 ID
 * @retval <0 通道创建失败，返回负 POSIX 错误码
 */
int sys_channel_create(uint32_t flags)
{
    return (int)syscall1(SYS_CHANNEL_CREATE, (uint64_t)flags);
}

/**
 * @brief 销毁 IPC 通道
 *
 * @details 销毁指定的 IPC 通道，释放所有关联资源。
 *          所有附加到该通道的连接将变为无效。
 *
 * @param channel_id 要销毁的通道 ID
 *
 * @return 成功返回 0，失败返回负错误码
 *
 * @retval 0  销毁成功
 * @retval <0 销毁失败，返回负 POSIX 错误码
 */
int sys_channel_destroy(uint32_t channel_id)
{
    return (int)syscall1(SYS_CHANNEL_DESTROY, (uint64_t)channel_id);
}

/**
 * @brief 附加到 IPC 通道
 *
 * @details 客户端通过此调用建立到指定通道的连接，
 *          获取连接 ID 用于后续消息发送。
 *
 * @param channel_id 目标通道 ID
 *
 * @return 成功返回连接 ID，失败返回负错误码
 *
 * @retval >0 附加成功，返回连接 ID
 * @retval <0 附加失败，返回负 POSIX 错误码
 */
int sys_connect_attach(uint32_t channel_id)
{
    return (int)syscall1(SYS_CONNECT_ATTACH, (uint64_t)channel_id);
}

/**
 * @brief 分离 IPC 连接
 *
 * @details 断开客户端与服务端通道的连接，
 *          释放连接占用的资源。
 *
 * @param conn_id 要分离的连接 ID
 *
 * @return 成功返回 0，失败返回负错误码
 *
 * @retval 0  分离成功
 * @retval <0 分离失败，返回负 POSIX 错误码
 */
int sys_connect_detach(uint32_t conn_id)
{
    return (int)syscall1(SYS_CONNECT_DETACH, (uint64_t)conn_id);
}

/**
 * @brief 同步发送 IPC 消息
 *
 * @details 向指定连接对应的服务端通道发送消息。
 *          此调用为同步阻塞操作：发送线程将被阻塞，
 *          直到服务端处理完消息并通过 sys_msg_reply() 回复。
 *
 * @param conn_id   连接/端点 ID
 * @param send_buf  发送消息缓冲区指针
 * @param send_size 发送消息大小（字节）
 * @param recv_buf  接收回复缓冲区指针（可为 NULL）
 * @param recv_size 接收回复缓冲区大小（字节）
 *
 * @return 成功返回 0，失败返回负错误码
 *
 * @retval 0  消息发送成功且收到回复
 * @retval <0 消息发送失败，返回负 POSIX 错误码
 */
int sys_msg_send(uint32_t conn_id, const void *send_buf, uint64_t send_size,
                 void *recv_buf, uint64_t recv_size)
{
    return (int)syscall5(SYS_MSG_SEND,
                         (uint64_t)conn_id,
                         (uint64_t)(uintptr_t)send_buf,
                         send_size,
                         (uint64_t)(uintptr_t)recv_buf,
                         recv_size);
}

/**
 * @brief 接收 IPC 消息
 *
 * @details 从指定端点接收一条消息。
 *          如果没有待处理消息，调用线程将阻塞等待。
 *
 * @param endpoint_id 端点 ID（通常为通道 ID）
 * @param msg         接收消息缓冲区指针
 * @param size        输入为缓冲区大小，输出为实际接收的消息大小
 *
 * @return 成功返回 0，失败返回负错误码
 *
 * @retval 0  消息接收成功
 * @retval <0 消息接收失败，返回负 POSIX 错误码
 */
int sys_msg_recv(uint32_t endpoint_id, void *msg, uint64_t *size)
{
    return (int)syscall3(SYS_MSG_RECV,
                         (uint64_t)endpoint_id,
                         (uint64_t)(uintptr_t)msg,
                         (uint64_t)(uintptr_t)size);
}

/**
 * @brief 回复 IPC 消息
 *
 * @details 服务端处理完消息后，通过此调用向发送方回复。
 *          回复后，被阻塞的发送方线程将被唤醒。
 *
 * @param endpoint_id 端点 ID
 * @param reply       回复缓冲区指针
 * @param size        回复数据大小（字节）
 *
 * @return 成功返回 0，失败返回负错误码
 *
 * @retval 0  回复成功
 * @retval <0 回复失败，返回负 POSIX 错误码
 */
int sys_msg_reply(uint32_t endpoint_id, void *reply, uint64_t size)
{
    return (int)syscall3(SYS_MSG_REPLY,
                         (uint64_t)endpoint_id,
                         (uint64_t)(uintptr_t)reply,
                         size);
}

/**
 * @brief 发送异步脉冲
 *
 * @details 向指定连接发送一个轻量级异步脉冲消息。
 *          脉冲不会阻塞发送方，也不会等待回复。
 *          脉冲按优先级排队在通道的脉冲队列中。
 *
 * @param conn_id 连接 ID
 * @param code    脉冲代码（标识脉冲类型）
 * @param value   脉冲值（携带的数据）
 *
 * @return 成功返回 0，失败返回负错误码
 *
 * @retval 0  脉冲发送成功
 * @retval <0 脉冲发送失败，返回负 POSIX 错误码
 */
int sys_pulse_send(uint32_t conn_id, uint32_t code, uint32_t value)
{
    return (int)syscall3(SYS_PULSE_SEND,
                         (uint64_t)conn_id,
                         (uint64_t)code,
                         (uint64_t)value);
}

/**
 * @brief 信号通知
 *
 * @details 向指定通知对象发送信号。
 *          通知对象可用于等待多个异步事件。
 *
 * @param notif_id 通知对象 ID
 * @param signals  信号位掩码（每位代表一个信号）
 *
 * @return 成功返回 0，失败返回负错误码
 *
 * @retval 0  信号发送成功
 * @retval <0 信号发送失败，返回负 POSIX 错误码
 */
int sys_notification_signal(uint32_t notif_id, uint64_t signals)
{
    return (int)syscall2(SYS_NOTIFICATION_SIGNAL,
                         (uint64_t)notif_id,
                         signals);
}

/**
 * @brief 等待通知
 *
 * @details 阻塞等待指定通知对象上的信号。
 *          当有信号到达时，返回激活的信号位掩码。
 *
 * @param notif_id 通知对象 ID
 * @param signals  输出参数，返回接收到的信号位掩码
 *
 * @return 成功返回 0，失败返回负错误码
 *
 * @retval 0  等待成功，signals 中包含收到的信号
 * @retval <0 等待失败，返回负 POSIX 错误码
 */
int sys_notification_wait(uint32_t notif_id, uint64_t *signals)
{
    return (int)syscall2(SYS_NOTIFICATION_WAIT,
                         (uint64_t)notif_id,
                         (uint64_t)(uintptr_t)signals);
}

/* ========================================================================
 * 内存管理系统调用
 * ======================================================================== */

/**
 * @brief 创建虚拟地址空间
 *
 * @details 创建一个新的虚拟地址空间，包含独立的页表。
 *          新地址空间初始为空，需通过 sys_vm_map() 添加映射。
 *
 * @return 成功返回地址空间 ID，失败返回负错误码
 *
 * @retval >0 地址空间创建成功，返回 vspace ID
 * @retval <0 地址空间创建失败，返回负 POSIX 错误码
 */
int sys_vmspace_create(void)
{
    return (int)syscall0(SYS_VMSPACE_CREATE);
}

/**
 * @brief 销毁虚拟地址空间
 *
 * @details 销毁指定的虚拟地址空间，释放所有映射和页表资源。
 *          销毁后所有关联的虚拟地址将不再有效。
 *
 * @param vspace_id 要销毁的地址空间 ID
 *
 * @return 成功返回 0，失败返回负错误码
 *
 * @retval 0  销毁成功
 * @retval <0 销毁失败，返回负 POSIX 错误码
 */
int sys_vmspace_destroy(uint32_t vspace_id)
{
    return (int)syscall1(SYS_VMSPACE_DESTROY, (uint64_t)vspace_id);
}

/**
 * @brief 映射虚拟内存区域
 *
 * @details 在指定地址空间中创建虚拟内存映射。
 *          可以指定映射的起始地址（addr 非 NULL 时）或由内核选择地址。
 *          size 参数受限于 syscall3 参数上限，暂未传递给内核。
 *
 * @param vspace_id 地址空间 ID
 * @param addr      期望的映射起始地址（NULL 表示由内核选择）
 * @param size      映射区域大小（预留参数，受限于 syscall3）
 * @param flags     映射属性标志（读/写/执行等）
 *
 * @return 成功返回映射的起始地址，失败返回 NULL
 *
 * @retval !=NULL 映射成功，返回虚拟地址
 * @retval NULL   映射失败
 */
void *sys_vm_map(uint32_t vspace_id, void *addr,
                 uint64_t size, uint32_t flags)
{
    int64_t ret;

    (void)size; /* 预留参数，受限于 syscall3 参数上限暂未传递 */

    ret = syscall3(SYS_VM_MAP,
                   (uint64_t)vspace_id,
                   (uint64_t)(uintptr_t)addr,
                   (uint64_t)flags);

    return (void *)(uintptr_t)ret;
}

/**
 * @brief 解除虚拟内存映射
 *
 * @details 取消指定地址空间中一段虚拟内存区域的映射。
 *          解除映射后，访问该区域将触发页错误。
 *
 * @param vspace_id 地址空间 ID
 * @param addr      要解除映射的起始地址
 * @param size      要解除映射的区域大小（字节）
 *
 * @return 成功返回 0，失败返回负错误码
 *
 * @retval 0  解除映射成功
 * @retval <0 解除映射失败，返回负 POSIX 错误码
 */
int sys_vm_unmap(uint32_t vspace_id, void *addr, uint64_t size)
{
    return (int)syscall3(SYS_VM_UNMAP,
                         (uint64_t)vspace_id,
                         (uint64_t)(uintptr_t)addr,
                         size);
}

/**
 * @brief 修改虚拟内存区域权限
 *
 * @details 修改指定地址空间中一段虚拟内存区域的访问权限。
 *          size 参数受限于 syscall3 参数上限，暂未传递给内核。
 *
 * @param vspace_id 地址空间 ID
 * @param addr      要修改权限的起始地址
 * @param size      要修改权限的区域大小（预留参数，受限于 syscall3）
 * @param flags     新的权限标志（读/写/执行等）
 *
 * @return 成功返回 0，失败返回负错误码
 *
 * @retval 0  权限修改成功
 * @retval <0 权限修改失败，返回负 POSIX 错误码
 */
int sys_vm_protect(uint32_t vspace_id, void *addr,
                   uint64_t size, uint32_t flags)
{
    (void)size; /* 预留参数，受限于 syscall3 参数上限暂未传递 */

    return (int)syscall3(SYS_VM_PROTECT,
                         (uint64_t)vspace_id,
                         (uint64_t)(uintptr_t)addr,
                         (uint64_t)flags);
}

/* ========================================================================
 * 能力管理系统调用
 * ======================================================================== */

/**
 * @brief 创建能力空间
 *
 * @details 创建一个新的能力空间（CSpace），
 *          包含指定数量的能力槽位。
 *          能力空间用于管理线程对内核对象的访问权限。
 *
 * @param capacity 能力空间的槽位数量
 *
 * @return 成功返回 CSpace ID，失败返回负错误码
 *
 * @retval >0 创建成功，返回 CSpace ID
 * @retval <0 创建失败，返回负 POSIX 错误码
 */
int sys_cspace_create(uint32_t capacity)
{
    return (int)syscall1(SYS_CSPACE_CREATE, (uint64_t)capacity);
}

/**
 * @brief 复制能力
 *
 * @details 将源能力空间中指定槽位的能力复制到
 *          目标能力空间的指定槽位，并可缩减权限。
 *          dest_slot 和 rights 参数受限于 syscall3 参数上限，暂未传递。
 *
 * @param src_cspace  源能力空间 ID
 * @param src_slot    源槽位索引
 * @param dest_cspace 目标能力空间 ID
 * @param dest_slot   目标槽位索引（预留参数）
 * @param rights      目标能力的权限掩码（预留参数）
 *
 * @return 成功返回 0，失败返回负错误码
 *
 * @retval 0  复制成功
 * @retval <0 复制失败，返回负 POSIX 错误码
 */
int sys_cap_copy(uint32_t src_cspace, uint32_t src_slot,
                 uint32_t dest_cspace, uint32_t dest_slot,
                 uint32_t rights)
{
    (void)dest_slot; /* 预留参数，受限于 syscall3 参数上限暂未传递 */
    (void)rights;    /* 预留参数，受限于 syscall3 参数上限暂未传递 */

    return (int)syscall3(SYS_CAP_COPY,
                         (uint64_t)src_cspace,
                         (uint64_t)src_slot,
                         (uint64_t)dest_cspace);
}

/**
 * @brief 撤销能力
 *
 * @details 撤销指定能力空间中某个槽位的能力，
 *          同时级联撤销所有从该能力派生的子能力。
 *
 * @param cspace 能力空间 ID
 * @param slot   要撤销的槽位索引
 *
 * @return 成功返回 0，失败返回负错误码
 *
 * @retval 0  撤销成功
 * @retval <0 撤销失败，返回负 POSIX 错误码
 */
int sys_cap_revoke(uint32_t cspace, uint32_t slot)
{
    return (int)syscall2(SYS_CAP_REVOKE,
                         (uint64_t)cspace,
                         (uint64_t)slot);
}

/**
 * @brief 删除能力
 *
 * @details 删除指定能力空间中某个槽位的能力。
 *          与撤销不同，删除仅移除当前槽位的能力，
 *          不影响从该能力派生的子能力。
 *
 * @param cspace 能力空间 ID
 * @param slot   要删除的槽位索引
 *
 * @return 成功返回 0，失败返回负错误码
 *
 * @retval 0  删除成功
 * @retval <0 删除失败，返回负 POSIX 错误码
 */
int sys_cap_delete(uint32_t cspace, uint32_t slot)
{
    return (int)syscall2(SYS_CAP_DELETE,
                         (uint64_t)cspace,
                         (uint64_t)slot);
}

/* ========================================================================
 * 中断管理系统调用
 * ======================================================================== */

/**
 * @brief 绑定中断到通知对象
 *
 * @details 将指定硬件中断号绑定到一个通知对象。
 *          当中断触发时，内核将向通知对象发送信号，
 *          等待该通知的线程将被唤醒。
 *
 * @param irq              硬件中断号
 * @param notification_cap 通知对象的能力槽位索引
 *
 * @return 成功返回 0，失败返回负错误码
 *
 * @retval 0  绑定成功
 * @retval <0 绑定失败，返回负 POSIX 错误码
 */
int sys_interrupt_attach(uint32_t irq, uint32_t notification_cap)
{
    return (int)syscall2(SYS_INTERRUPT_ATTACH,
                         (uint64_t)irq,
                         (uint64_t)notification_cap);
}

/**
 * @brief 解除中断绑定
 *
 * @details 解除指定中断号的绑定，此后该中断不再触发通知。
 *
 * @param irq 要解除绑定的硬件中断号
 *
 * @return 成功返回 0，失败返回负错误码
 *
 * @retval 0  解除绑定成功
 * @retval <0 解除绑定失败，返回负 POSIX 错误码
 */
int sys_interrupt_detach(uint32_t irq)
{
    return (int)syscall1(SYS_INTERRUPT_DETACH, (uint64_t)irq);
}

/* ========================================================================
 * 调试系统调用
 * ======================================================================== */

/**
 * @brief 内核调试打印
 *
 * @details 通过内核调试接口输出字符串。
 *          仅在调试构建中有效，用于用户态服务的调试输出。
 *
 * @param str 要输出的字符串指针
 * @param len 字符串长度（字节）
 *
 * @return 成功返回 0，失败返回负错误码
 *
 * @retval 0  输出成功
 * @retval <0 输出失败，返回负 POSIX 错误码
 */
int sys_debug_print(const char *str, uint64_t len)
{
    return (int)syscall2(SYS_DEBUG_PRINT,
                         (uint64_t)(uintptr_t)str,
                         len);
}
