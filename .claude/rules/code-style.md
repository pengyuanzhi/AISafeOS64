## 4. 代码风格规范

### 4.1 命名规范

#### 4.1.1 函数命名
```c
/* 格式: <模块>_<对象>_<动作> */
uint32_t scheduler_task_create(void (*entry)(void), uint8_t prio);
void scheduler_task_delete(uint32_t task_id);
void memory_pool_init(uint32_t pool_id);

/* 简短函数可以省略模块名（如果明确） */
uint32_t task_create(void (*entry)(void), uint8_t prio);
```

#### 4.1.2 变量命名
```c
/* 局部变量: 小写 + 下划线 */
uint32_t task_count;
uint64_t system_ticks;
TCB_t *current_task;

/* 全局变量: 加g_前缀 */
uint32_t g_max_tasks;
Scheduler_t g_scheduler;

/* 静态全局变量: 加s_前缀 */
static uint32_t s_initialized = 0U;
static TCB_t *s_idle_task = NULL;

/* 常量: 全大写 + _后缀表示类型 */
#define MAX_TASK_COUNT     256U
#define TICK_RATE_HZ       1000U
#define STACK_SIZE_MIN     4096U

/* 枚举值: 全大写 + 前缀 */
typedef enum 
{
    TASK_READY = 0U,      /* 就绪态：等待CPU调度 */
    TASK_RUNNING,         /* 运行态：正在执行 */
    TASK_BLOCKED,         /* 阻塞态：等待资源（信号量、消息队列） */
    TASK_SLEEPING,        /* 休眠态：延时等待，超时自动唤醒 */
    TASK_SUSPENDED        /* 挂起态：被挂起，需要显式恢复 */
} TaskState_t;
```

#### 4.1.3 类型命名
```c
/* 结构体和联合体: _t后缀 */
typedef struct TaskControlBlock TCB_t;
typedef struct Mutex Mutex_t;
typedef union RegisterValue RegValue_t;

/* 函数指针: _fn或_cb后缀 */
typedef void (*TaskEntry_fn)(void);
typedef uint32_t (*ErrorCallback_fn)(uint32_t error);
```

### 4.2 格式规范

#### 4.2.1 缩进和空格
```c
/* 使用4个空格缩进（不使用Tab） */
void function(void) 
{
    uint32_t x = 10U;

    if (x > 5U) 
    {
        x = x + 1U;
    }
}

/* 运算符两边加空格 */
x = a + b * c;        /* ❌ 错误: *两边没有空格 */
x = a + (b * c);      /* ✅ 正确 */

/* 函数参数: 左括号前不加空格 */
func (arg);           /* ❌ 错误 */
func(arg);            /* ✅ 正确 */

/* 控制语句: 括号前加空格 */
if(condition)         /* ❌ 错误 */
if (condition)        /* ✅ 正确 */
```

#### 4.2.2 大括号规范（Allman风格）
```c
/* Allman风格：左大括号必须换行 */
void function(void)
{                    /* ✅ 正确 - Allman风格 */
    /* code */
}

void function(void) { /* ❌ 错误 - K&R风格 */
    /* code */
}

/* 单语句也必须使用大括号 */
if (condition)
    x = 1;           /* ❌ 错误：缺少大括号 */

if (condition)
{                    /* ✅ 正确：Allman风格 */
    x = 1;
}

/* 控制语句必须使用Allman风格 */
if (condition)
{
    do_something();
}
else
{
    do_other_thing();
}

while (condition)
{
    do_something();
}

for (int i = 0; i < max; i++)
{
    do_something();
}

/* 函数定义必须使用Allman风格 */
void function_name(parameter1, parameter2)
{
    /* 函数体 */
}

/* 结构体定义必须使用Allman风格 */
typedef struct StructureName
{
    uint32_t field1;
    uint32_t field2;
} StructureName_t;
```

#### 4.2.3 无限循环规范
```c
/* 无限循环必须使用 for (;;) 而不是 while (1) 或 while (true) */
for (;;)
{
    /* 无限循环体 */
    do_something();
}

/* ❌ 错误：使用 while (1) */
while (1)
{
    /* 不推荐的做法 */
    do_something();
}

/* ❌ 错误：使用 while (true) */
while (true)
{
    /* 不推荐的做法 */
    do_something();
}

/* ✅ 正确：for (;;) 是标准的无限循环写法 */
/* 理由：
 * 1. for (;;) 是明确表达"无限循环"的惯用写法
 * 2. 避免魔法数字（1）或布尔值（true）
 * 3. 更好的编译器优化
 * 4. MISRA-C:2012 规则 15.1 推荐做法
 */
void idle_task(void)
{
    for (;;)
    {
        /* 等待中断或执行空闲任务 */
        __asm__ volatile("wfe");
    }
}
```

#### 4.2.4 行长度
```c
/* 每行最多120个字符 */
uint32_t result = function_with_very_long_name(argument1, argument2, argument3, argument4);

/* 超过120字符需要换行 */
uint32_t result = function_with_very_long_name(
    argument1,
    argument2,
    argument3,
    argument4
);

/* 函数调用换行对齐 */
uint32_t result = scheduler_task_create(
    task_entry_function,
    priority_value,
    stack_size_bytes,
    task_name_string
);
```

### 4.3 注释规范

#### 4.3.0 语言规范

**AISafe64 项目强制使用中文编写所有代码注释和文档**。

- ✅ **必须**：所有注释使用中文（包括 Doxygen 文档）
- ✅ **必须**：使用 Doxygen 风格的文档注释
- ✅ **推荐**：代码注释简洁明了，使用中文
- ❌ **禁止**：使用英文编写注释（除非特殊情况并经团队同意）

**示例对比**：

```c
/* ✅ 正确：中文 Doxygen 注释 */
/**
 * @brief 创建新任务
 * @param entry 任务入口函数指针
 * @return 成功返回任务ID，失败返回0
 */

/* ❌ 错误：英文注释 */
/**
 * @brief Create new task
 * @param entry Task entry point
 * @return Task ID on success, 0 on failure
 */
```

#### 4.3.1 文件头注释（Doxygen 格式）

**必须使用 Doxygen 格式的文件头注释**。

```c
/**
 * @file    scheduler.c
 * @brief   任务调度器实现
 * @author  AISafe64 Team
 * @date    2025-01-07
 * @version 1.0
 *
 * @details 本文件实现了256级优先级的多核任务调度器
 *          - 支持抢占式调度
 *          - 支持负载均衡和任务迁移
 *          - O(1)时间复杂度的调度算法
 *
 * @note MISRA-C:2012 合规
 * @warning 调度器必须在多核环境下使用
 *
 * @copyright Copyright (c) 2025 AISafe64 Team
 */
```

**Doxygen 标签说明**：

| 标签 | 说明 | 必填 |
|------|------|------|
| `@file` | 文件名 | ✅ |
| `@brief` | 简短描述 | ✅ |
| `@author` | 作者 | ✅ |
| `@date` | 创建日期 | ✅ |
| `@version` | 版本号 | ✅ |
| `@details` | 详细描述 | 推荐 |
| `@note` | 注意事项 | 可选 |
| `@warning` | 警告信息 | 可选 |
| `@copyright` | 版权声明 | ✅ |

#### 4.3.2 函数注释（Doxygen 格式）

**所有公共函数必须使用 Doxygen 格式的注释**。

```c
/**
 * @brief 创建新任务
 *
 * @details 创建一个新的任务并添加到调度器就绪队列
 *          任务将在调度器启动后开始执行
 *
 * @param entry 任务入口函数指针（不能为NULL）
 * @param priority 任务优先级（0-255，255为最高）
 * @param stack_size 堆栈大小（字节，最小4096）
 * @param name 任务名称（最多16字符）
 *
 * @return 成功返回任务ID，失败返回0
 *
 * @retval 非0 任务创建成功，返回任务ID
 * @retval 0 任务创建失败
 *
 * @note 必须在调度器启动前调用
 * @note 任务入口函数不得返回，应使用 task_exit() 退出
 * @warning 优先级值过大会导致低优先级任务饥饿
 *
 * @par 示例代码
 * @code
 * uint32_t tid = task_create(my_task, 255, 8192, "MyTask");
 * if (tid != 0U)
 * {
 *     printf("任务创建成功: %u\n", tid);
 * }
 * @endcode
 *
 * @see task_exit()
 * @see scheduler_start()
 *
 * @since 1.0.0
 */
uint32_t task_create(void (*entry)(void),
                    uint8_t priority,
                    uint32_t stack_size,
                    const char *name);
```

**函数注释 Doxygen 标签说明**：

| 标签 | 说明 | 用法 |
|------|------|------|
| `@brief` | 简短描述（一行） | 必填 |
| `@details` | 详细描述 | 推荐 |
| `@param` | 参数说明 | 每个参数必填 |
| `@return` | 返回值描述 | 必填 |
| `@retval` | 特定返回值说明 | 多种返回值时使用 |
| `@note` | 注意事项 | 可选 |
| `@warning` | 警告信息 | 有风险时必填 |
| `@par` | 段落标题 | 可选 |
| `@code` | 代码示例开始 | 配合 @endcode |
| `@endcode` | 代码示例结束 | 配合 @code |
| `@see` | 参考其他函数 | 可选 |
| `@since` | 起始版本 | 推荐 |

#### 4.3.3 结构体注释（Doxygen 格式）

```c
/**
 * @brief 任务控制块（TCB）
 *
 * @details 包含任务的所有状态信息
 *          - 寄存器保存
 *          - 堆栈信息
 *          - 调度信息
 *          - 优先级
 *          - 状态
 */
typedef struct TaskControlBlock
{
    uint64_t regs[32];      /**< @brief 寄存器保存区 */
    uint64_t stack_base;    /**< @brief 堆栈基地址 */
    uint64_t stack_ptr;     /**< @brief 当前堆栈指针 */
    uint64_t stack_size;    /**< @brief 堆栈大小（字节） */
    uint8_t  priority;      /**< @brief 任务优先级（0-255） */
    uint8_t  state;         /**< @brief 任务状态 */
    uint32_t time_slice;    /**< @brief 时间片（毫秒） */
    char     name[16];      /**< @brief 任务名称 */
} TCB_t;
```

**结构体成员注释**：
- 使用 `/**< @brief 说明 */` 行内注释
- 或在结构体定义后单独注释

#### 4.3.4 宏定义注释（Doxygen 格式）

```c
/**
 * @def MAX_TASK_COUNT
 * @brief 系统支持的最大任务数量
 *
 * @details 最大任务数受限于：
 *          - 可用内存大小
 *          - 调度器性能
 *          - 硬件配置
 */
#define MAX_TASK_COUNT     256U

/**
 * @def TICK_RATE_HZ
 * @brief 系统时钟节拍频率（Hz）
 *
 * @note 建议值为 100Hz、1000Hz 或 10000Hz
 */
#define TICK_RATE_HZ       1000U
```

#### 4.3.5 枚举注释（Doxygen 格式）

```c
/**
 * @brief 任务状态枚举
 *
 * @details 定义任务的所有可能状态
 */
typedef enum
{
    TASK_READY = 0U,      /**< @brief 就绪态：等待CPU调度 */
    TASK_RUNNING,         /**< @brief 运行态：正在执行 */
    TASK_BLOCKED,         /**< @brief 阻塞态：等待资源（信号量、消息队列） */
    TASK_SLEEPING,        /**< @brief 休眠态：延时等待，超时自动唤醒 */
    TASK_SUSPENDED        /**< @brief 挂起态：被挂起，需要显式恢复 */
} TaskState_t;
```

#### 4.3.6 类型定义注释（Doxygen 格式）

```c
/**
 * @brief 信号量类型定义
 *
 * @details 用于进程间同步和互斥
 *          - 支持二值信号量（max_count=1）
 *          - 支持计数信号量（max_count>1）
 */
typedef struct Semaphore
{
    int32_t count;              /**< @brief 当前计数值 */
    int32_t max_count;          /**< @brief 最大计数值 */
    struct list_head wait_queue; /**< @brief 等待队列 */
    spinlock_t lock;            /**< @brief 自旋锁 */
} semaphore_t;
```

#### 4.3.7 行内注释规范

**简洁的行内注释使用 `//` 或 `/* */`**。

```c
/* ✅ 正确：简洁的中文注释 */
uint32_t task_id;  /* 任务唯一标识 */
uint64_t system_ticks;  /* 系统时钟计数器 */

/* 多行注释: 详细说明 */
/*
 * 256级优先级位图实现：
 * - 使用4个uint64_t表示256位
 * - bitmap[0]: 优先级 0-63
 * - bitmap[1]: 优先级 64-127
 * - bitmap[2]: 优先级 128-191
 * - bitmap[3]: 优先级 192-255
 */
static uint64_t priority_bitmap[4];
```

#### 4.3.8 特殊注释标记

```c
/* TODO注释: 标记待完成的工作 */
/* TODO: 实现优先级捐赠算法 */

/* FIXME注释: 标记已知问题 */
/* FIXME: 负载均衡在高负载下效率低 */

/* HACK注释: 标记临时解决方案 */
/* HACK: 临时使用忙等待，后续改为WFE指令 */

/* XXX注释: 标记需要警惕的代码 */
/* XXX: 此处未做边界检查，调用方需确保参数有效 */

/* NOTE注释: 重要说明 */
/* NOTE: 此函数必须在锁保护下调用 */
```

#### 4.3.9 注释最佳实践

**DO（推荐做法）**：

```c
/* ✅ 1. 函数必须有 Doxygen 注释 */
/**
 * @brief 计算两个数的和
 * @param a 第一个数
 * @param b 第二个数
 * @return 两数之和
 */
int32_t add(int32_t a, int32_t b);

/* ✅ 2. 复杂逻辑需要注释 */
/* 检查页表项是否有效 */
if ((pte & PTE_VALID) == PTE_VALID)
{
    /* 提取物理页号 */
    uint64_t phys_page = pte >> PTE_PHYS_SHIFT;

    /* 映射到虚拟地址空间 */
    void *virt_addr = phys_to_virt(phys_page);
}

/* ✅ 3. 使用有意义的变量名 */
uint32_t timeout_ms;  /* 超时时间（毫秒） */

/* ✅ 4. 注释解释"为什么"而非"是什么" */
/* 使用CLZ指令而非循环，性能提升10倍 */
priority = 63U - (uint8_t)__builtin_clzll(bitmap);
```

**DON'T（不推荐做法）**：

```c
/* ❌ 1. 不要使用英文注释 */
/* Calculate the sum of two numbers */

/* ❌ 2. 不要注释显而易见的代码 */
/* 将a加1 */
a = a + 1U;

/* ❌ 3. 不要使用过长的注释 */
/*
 * 这是一个非常非常非常非常非常非常非常非常非常长的注释，
 * 读者很难一眼看明白它想说什么。
 */

/* ❌ 4. 不要使用错误的Doxygen标签 */
/**
 * @brief 简短描述
 * @description 详细描述  ❌ 应使用 @details
 */
```

### 4.4 文件组织规范

#### 4.4.1 头文件结构
```c
/**
 * @file    scheduler.h
 * @brief   任务调度器头文件
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

/* 1. 包含其他头文件 */
#include "types.h"
#include "task.h"

/* 2. 宏定义 */
#define MAX_PRIORITY      255U
#define MIN_PRIORITY      0U
#define PRIORITY_LEVELS   256U

/* 3. 类型定义 */
typedef struct Scheduler Scheduler_t;

/* 4. 函数声明 */
void scheduler_init(void);
void scheduler_start(void);

/* 5. 内联函数（如果需要） */
static inline uint32_t scheduler_get_cpu_count(void) 
{
    return MAX_CPUS;
}

#endif /* SCHEDULER_H */
```

#### 4.4.2 源文件结构
```c
/**
 * @file    scheduler.c
 * @brief   任务调度器实现
 */

/* 1. 包含头文件 */
#include "scheduler.h"
#include <string.h>

/* 2. 宏定义（仅本文件使用） */
#define SCHEDULER_LOCK_TIMEOUT_US  1000U

/* 3. 类型定义（仅本文件使用） */
typedef struct 
{
    uint32_t count;
    uint64_t time;
} ScheduleStat_t;

/* 4. 全局变量 */
static Scheduler_t s_scheduler;
static bool s_initialized = false;

/* 5. 内部函数声明 */
static void schedule_internal(void);
static uint8_t find_highest_priority(void);

/* 6. 公共函数实现 */
void scheduler_init(void) 
{
    /* 实现代码 */
}

/* 7. 内部函数实现 */
static void schedule_internal(void) 
{
    /* 实现代码 */
}
```

---

