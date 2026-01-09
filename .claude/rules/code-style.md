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

#### 4.3.1 文件头注释
```c
/**
 * @file    scheduler.c
 * @brief   任务调度器实现
 * @author  AISafe64 Team
 * @date    2025-01-07
 * @version 1.0
 *
 * @details 本文件实现了256级优先级的多核任务调度器
 *          支持抢占式调度、负载均衡和任务迁移
 *
 * @copyright Copyright (c) 2025 AISafe64 Team
 */
```

#### 4.3.2 函数注释
```c
/**
 * @brief 创建新任务
 *
 * @param entry 任务入口函数指针（不能为NULL）
 * @param priority 任务优先级（0-255，255为最高）
 * @param stack_size 堆栈大小（字节，最小4096）
 * @param name 任务名称（最多16字符）
 *
 * @return 成功返回任务ID，失败返回0
 *
 * @note 必须在调度器启动前调用
 * @warning 任务入口函数不得返回
 *
 * @code
 * uint32_t tid = task_create(my_task, 255, 8192, "MyTask");
 * if (tid != 0U) {
 *     printf("Task created: %u\n", tid);
 * }
 * @endcode
 */
uint32_t task_create(void (*entry)(void),
                    uint8_t priority,
                    uint32_t stack_size,
                    const char *name);
```

#### 4.3.3 代码注释
```c
/* 单行注释: 简短说明 */
uint32_t task_id;  /* 任务唯一标识 */

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

/* TODO注释: 标记待完成的工作 */
/* TODO: 实现优先级捐赠算法 */

/* FIXME注释: 标记已知问题 */
/* FIXME: 负载均衡在高负载下效率低 */

/* HACK注释: 标记临时解决方案 */
/* HACK: 临时使用忙等待，后续改为WFE指令 */
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

