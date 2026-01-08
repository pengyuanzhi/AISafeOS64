/**
 * @file syscall.c
 * @brief AISafe64 RTOS - 系统调用实现
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 系统调用分发和实现
 *          - 系统调用表
 *          - 系统调用分发器
 *          - 基础系统调用实现
 *
 * @note MISRA-C:2012合规
 * @note 后续扩展为完整的系统调用表
 */

#include "syscall.h"
#include "sched.h"
#include "time.h"
#include "printk.h"
#include "types.h"

/**
 * @brief 外部函数声明
 */
extern void task_yield(void);
extern void task_exit(int exit_code);
extern int task_getpid(void);

/**
 * @brief 系统调用处理函数原型
 */
typedef int64_t (*syscall_func_t)(uint64_t *params);

/**
 * @brief 系统调用实现：写控制台
 * @param params 参数数组 [0]=buf, [1]=count
 * @return 成功写入的字节数
 */
static int64_t sys_write_impl(uint64_t *params)
{
    if (params == NULL)
    {
        return -SYS_ERROR_INVAL;
    }

    const char *buf = (const char *)params[0];
    uint64_t count = params[1];

    /* 参数验证 */
    if (buf == NULL)
    {
        return -SYS_ERROR_INVAL;
    }

    /* 简单实现：直接使用printk */
    /* TODO: 改进为直接写入UART */
    uint64_t written = 0UL;
    for (uint64_t i = 0UL; i < count; i++)
    {
        printk("%c", buf[i]);
        written++;
    }

    return (int64_t)written;
}

/**
 * @brief 系统调用实现：读控制台
 * @param params 参数数组 [0]=buf, [1]=count
 * @return 成功读取的字节数
 */
static int64_t sys_read_impl(uint64_t *params)
{
    if (params == NULL)
    {
        return -SYS_ERROR_INVAL;
    }

    char *buf = (char *)params[0];
    uint64_t count = params[1];

    /* 参数验证 */
    if (buf == NULL)
    {
        return -SYS_ERROR_INVAL;
    }

    /* 简单实现：暂不支持（需要UART输入驱动） */
    (void)count;
    printk("[WARNING] sys_read not implemented yet\n");
    return -SYS_ERROR_NOSYS;
}

/**
 * @brief 系统调用实现：退出任务
 * @param params 参数数组 [0]=exit_code
 * @return 不返回
 */
static int64_t sys_exit_impl(uint64_t *params)
{
    if (params == NULL)
    {
        return -SYS_ERROR_INVAL;
    }

    int exit_code = (int)params[0];

    /* 调用任务退出函数 */
    task_exit(exit_code);

    /* 永不返回 */
    return 0;
}

/**
 * @brief 系统调用实现：获取任务ID
 * @param params 参数数组（未使用）
 * @return 任务ID
 */
static int64_t sys_getpid_impl(uint64_t *params)
{
    (void)params; /* 未使用参数 */

    /* 获取当前任务ID */
    return task_getpid();
}

/**
 * @brief 系统调用实现：让出CPU
 * @param params 参数数组（未使用）
 * @return 成功返回0
 */
static int64_t sys_yield_impl(uint64_t *params)
{
    (void)params; /* 未使用参数 */

    /* 让出CPU */
    task_yield();
    return SYS_SUCCESS;
}

/**
 * @brief 系统调用实现：睡眠
 * @param params 参数数组 [0]=ms
 * @return 成功返回0
 */
static int64_t sys_sleep_impl(uint64_t *params)
{
    if (params == NULL)
    {
        return -SYS_ERROR_INVAL;
    }

    uint64_t ms = params[0];

    /* 调用msleep实现任务睡眠 */
    int ret = msleep(ms);

    if (ret != 0)
    {
        return -SYS_ERROR_INVAL;
    }

    return SYS_SUCCESS;
}

/**
 * @brief 系统调用实现：分配内存
 * @param params 参数数组 [0]=size
 * @return 内存指针，失败返回NULL
 */
static int64_t sys_malloc_impl(uint64_t *params)
{
    if (params == NULL)
    {
        return -SYS_ERROR_INVAL;
    }

    uint64_t size = params[0];

    /* 参数验证 */
    if (size == 0UL)
    {
        return -SYS_ERROR_INVAL;
    }

    /* 调用内核内存分配 */
    extern void *kmalloc(uint64_t size);
    void *ptr = kmalloc(size);

    if (ptr == NULL)
    {
        return -SYS_ERROR_NOMEM;
    }

    return (int64_t)ptr;
}

/**
 * @brief 系统调用实现：释放内存
 * @param params 参数数组 [0]=ptr
 * @return 成功返回0
 */
static int64_t sys_free_impl(uint64_t *params)
{
    if (params == NULL)
    {
        return -SYS_ERROR_INVAL;
    }

    void *ptr = (void *)params[0];

    /* 参数验证 */
    if (ptr == NULL)
    {
        return -SYS_ERROR_INVAL;
    }

    /* 调用内核内存释放 */
    extern void kfree(void *ptr);
    kfree(ptr);

    return SYS_SUCCESS;
}

/**
 * @brief 系统调用实现：获取系统时间
 * @param params 参数数组（未使用）
 * @return 系统时间（毫秒）
 */
static int64_t sys_gettime_impl(uint64_t *params)
{
    (void)params; /* 未使用参数 */

    /* 返回系统运行时间（毫秒） */
    return (int64_t)get_system_time_ms();
}

/**
 * @brief 系统调用实现：设置调度参数
 * @param params 参数数组 [0]=policy, [1]=priority
 * @return 成功返回0
 *
 * @details 设置当前任务的调度参数
 *          - policy: 调度策略（0=FIFO, 1=EDF, 2=CFS, 3=RR, 4=IDLE）
 *          - priority: 优先级（0-255，255为最高）
 */
static int64_t sys_sched_set_impl(uint64_t *params)
{
    if (params == NULL)
    {
        return -SYS_ERROR_INVAL;
    }

    /* 获取当前任务 */
    TCB_t *task = get_current_task();
    if (task == NULL)
    {
        return -SYS_ERROR_INVAL;
    }

    uint32_t policy = (uint32_t)params[0];
    uint32_t priority = (uint32_t)params[1];

    /* 参数验证 */
    if (policy > 4U) /* 4种调度策略 + IDLE */
    {
        return -SYS_ERROR_INVAL;
    }

    if (priority >= PRIORITY_LEVELS)
    {
        return -SYS_ERROR_INVAL;
    }

    /* 设置优先级 */
    int ret = set_task_priority(task, (uint8_t)priority);
    if (ret != 0)
    {
        return -SYS_ERROR_INVAL;
    }

    /* TODO: 实现调度策略切换 */
    /* 需要调用 sched_switch_class() */

    return SYS_SUCCESS;
}

/**
 * @brief 系统调用实现：获取调度参数
 * @param params 参数数组 [0]=policy_ptr, [1]=priority_ptr
 * @return 成功返回0
 *
 * @details 获取当前任务的调度参数
 *          - policy_ptr: 输出调度策略的指针
 *          - priority_ptr: 输出优先级的指针
 */
static int64_t sys_sched_get_impl(uint64_t *params)
{
    if (params == NULL)
    {
        return -SYS_ERROR_INVAL;
    }

    /* 获取当前任务 */
    TCB_t *task = get_current_task();
    if (task == NULL)
    {
        return -SYS_ERROR_INVAL;
    }

    uint32_t *policy_ptr = (uint32_t *)params[0];
    uint32_t *priority_ptr = (uint32_t *)params[1];

    /* 输出参数 */
    if (policy_ptr != NULL)
    {
        /* 返回调度策略ID */
        if (task->sched_class != NULL)
        {
            *policy_ptr = task->sched_class->id;
        }
        else
        {
            *policy_ptr = 4U; /* 默认IDLE */
        }
    }

    if (priority_ptr != NULL)
    {
        *priority_ptr = task->prio;
    }

    return SYS_SUCCESS;
}

/**
 * @brief 系统调用表
 * @details 索引对应系统调用号
 */
static const syscall_func_t g_syscall_table[] = {
    NULL,               /* 0: 未使用 */
    sys_write_impl,     /* 1: SYS_WRITE */
    sys_read_impl,      /* 2: SYS_READ */
    sys_exit_impl,      /* 3: SYS_EXIT */
    sys_getpid_impl,    /* 4: SYS_GETPID */
    sys_yield_impl,     /* 5: SYS_YIELD */
    sys_sleep_impl,     /* 6: SYS_SLEEP */
    sys_malloc_impl,    /* 7: SYS_MALLOC */
    sys_free_impl,      /* 8: SYS_FREE */
    sys_gettime_impl,   /* 9: SYS_GETTIME */
    sys_sched_set_impl, /* 10: SYS_SCHED_SET */
    sys_sched_get_impl, /* 11: SYS_SCHED_GET */
};

/**
 * @brief 系统调用表大小
 */
#define SYSCALL_TABLE_SIZE (sizeof(g_syscall_table) / sizeof(g_syscall_table[0]))

/**
 * @brief 系统调用处理函数（分发器）
 * @param syscall_nr 系统调用号
 * @param params 参数数组指针（指向栈上的参数）
 * @return 系统调用返回值
 *
 * @details 由start.S的SVC异常处理调用
 *          - 验证系统调用号
 *          - 查表调用对应的系统调用实现
 *          - 返回结果到用户空间
 */
int64_t syscall_handler(uint64_t syscall_nr, uint64_t *params)
{
    /* 参数验证 */
    if (params == NULL)
    {
        return -SYS_ERROR_INVAL;
    }

    /* 验证系统调用号 */
    if (syscall_nr >= SYSCALL_TABLE_SIZE)
    {
        printk("[SYSCALL] Invalid system call number: %lu\n", syscall_nr);
        return -SYS_ERROR_NOSYS;
    }

    /* 查表获取系统调用函数 */
    syscall_func_t func = g_syscall_table[syscall_nr];
    if (func == NULL)
    {
        printk("[SYSCALL] Unimplemented system call: %lu\n", syscall_nr);
        return -SYS_ERROR_NOSYS;
    }

    /* 调用系统调用实现 */
    int64_t ret = func(params);

    /* 调试输出（可选） */
    /* printk("[SYSCALL] syscall %lu returned %ld\n", syscall_nr, ret); */

    return ret;
}

/**
 * @brief 空任务退出函数（占位符）
 * @param exit_code 退出码
 */
void task_exit(int exit_code)
{
    printk("[TASK] Task exited with code %d\n", exit_code);
    printk("[TASK] Halting...\n");

    while (true)
    {
        __asm__ volatile("wfe");
    }
}

/**
 * @brief 让出CPU函数
 */
void task_yield(void)
{
    /* 调用调度器的yield函数 */
    yield();
}

/**
 * @brief 获取任务ID函数
 * @return 任务ID
 */
int task_getpid(void)
{
    /* 获取当前任务 */
    TCB_t *task = get_current_task();
    if (task == NULL)
    {
        return 0;
    }

    return (int)task->tid;
}
