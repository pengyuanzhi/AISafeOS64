/**
 * @file    main.c
 * @brief   init 服务入口
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 2.0
 *
 * @details init 服务是内核启动后创建的第一个用户态服务。
 *          其职责包括：
 *          - 打印启动信息
 *          - 注册到 PathManager（路径管理器）
 *          - 启动核心管理器服务（ProcessManager、MemoryManager、PathManager）
 *          - 进入主循环等待消息
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: KR-024, API-001~004
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/syscall.h>
#include <kernel/service.h>
#include <stdint.h>

/* ========================================================================
 * 外部函数声明（libkernel 系统调用绑定）
 * ======================================================================== */

/**
 * @brief 内核调试打印
 *
 * @param str 要输出的字符串指针
 * @param len 字符串长度（字节）
 *
 * @return 成功返回 0，失败返回负错误码
 */
extern int sys_debug_print(const char *str, uint64_t len);

/**
 * @brief 让出 CPU
 *
 * @details 主动放弃当前线程的剩余时间片，让调度器选择下一个线程运行。
 */
extern void sys_thread_yield(void);

/* ========================================================================
 * init 服务入口
 * ======================================================================== */

/**
 * @brief init 服务主入口函数
 *
 * @details 内核创建的第一个用户态线程入口。
 *          执行以下初始化步骤：
 *          1. 打印启动信息
 *          2. 注册到 PathManager（简化版本中跳过）
 *          3. 启动 ProcessManager、MemoryManager、PathManager
 *          4. 进入主循环等待消息
 *
 * @return 理论上不会返回（init 服务持续运行）
 *
 * @note 当前为简化实现，仅打印启动信息后进入让出循环。
 *       完整实现将通过 IPC 消息机制管理子服务。
 */
int main(void)
{
    /* 打印启动信息 */
    const char *msg = "[init] AISafeOS64 init service started\n";
    sys_debug_print(msg, 35U);

    /*
     * 简化实现：直接进入让出循环
     *
     * 完整实现应执行以下步骤：
     * 1. sys_channel_create(0) - 创建 IPC 通道
     * 2. 通过 PathManager 注册自身
     * 3. 启动 ProcessManager、MemoryManager、PathManager 子服务
     * 4. sys_msg_recv() 循环接收和处理管理消息
     */
    for (;;)
    {
        sys_thread_yield();
    }

    /* 不会到达此处 */
    return 0;
}
