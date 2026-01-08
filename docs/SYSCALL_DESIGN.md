# AISafe64 系统调用架构设计文档

## 文档信息
- **版本**: 1.0
- **日期**: 2025-01-08
- **作者**: AISafe64架构组

---

## 1. 问题分析

### 1.1 核心问题

AISafe64支持三种任务隔离模式：
1. **TASK_ISOLATION_SHARED**（共享地址空间）
2. **TASK_ISOLATION_PRIVATE**（独立地址空间）
3. **TASK_ISOLATION_HYBRID**（混合模式）

**核心问题**：
- POSIX API在用户空间，内核在内核空间
- 如果任务有独立地址空间，POSIX API如何调用内核？
- 是否需要传统的系统调用机制？

### 1.2 设计约束

**功能安全要求（ISO 26262 ASIL-D）**：
- 必须支持地址空间隔离（安全关键任务需要隔离）
- 必须防止任务间相互干扰
- 系统调用必须是确定性的

**性能要求**：
- 系统调用开销尽可能小（< 100 cycles）
- 避免不必要的模式切换
- 支持高频率的内核调用

**兼容性要求**：
- POSIX API行为符合标准
- 源代码级兼容现有POSIX应用

---

## 2. 方案对比

### 2.1 方案A：无系统调用（单地址空间模型）

#### 架构图
```
┌─────────────────────────────────────────┐
│  物理内存 (Physical Memory)             │
│                                         │
│  ┌──────────────┐  ┌─────────────────┐ │
│  │ 任务1代码    │  │ 内核代码/数据   │ │
│  │ 任务1栈      │  │                 │ │
│  │ 任务1堆      │  │ ┌─────────────┐ │ │
│  │              │  │ │ task_create()│ │ │
│  └──────────────┘  │ │ queue_create()│ │ │
│  ┌──────────────┐  │ │ scheduler()  │ │ │
│  │ 任务2代码    │  │ └─────────────┘ │ │
│  │ 任务2栈      │  │                 │ │
│  │ 任务2堆      │  │ ┌─────────────┐ │ │
│  │              │  │ │ pthread_    │ │ │
│  └──────────────┘  │ │   create()  │ │ │
│  ┌──────────────┐  │ └─────────────┘ │ │
│  │ POSIX适配层  │  │                 │ │
│  └──────────────┘  └─────────────────┘ │
│                                         │
│  ←── 所有实体在同一虚拟地址空间 ───→   │
└─────────────────────────────────────────┘
```

#### 实现方式
```c
/* POSIX适配层：直接函数调用 */
int pthread_create(pthread_t *thread,
                   const pthread_attr_t *attr,
                   void *(*start_routine)(void *),
                   void *arg) {
    /* 直接调用内核函数（无系统调用） */
    return task_create_posix_wrapper(thread, attr, start_routine, arg);
}

/* 链接时解析：libaisafe64.a 与内核静态链接 */
/* 优点：零开销，编译时优化 */
/* 缺点：无地址空间隔离，安全性低 */
```

#### 优缺点分析

| 维度 | 评价 | 说明 |
|------|------|------|
| **性能** | ⭐⭐⭐⭐⭐ | 零系统调用开销，直接函数调用 |
| **安全性** | ⭐ | 无地址空间隔离，任务可访问内核数据 |
| **确定性** | ⭐⭐⭐⭐⭐ | 无模式切换，完全确定性 |
| **代码复杂度** | ⭐⭐⭐⭐⭐ | 实现简单，无系统调用处理 |
| **ASIL合规** | ❌ | 不符合ASIL-D要求（无隔离） |

#### 适用场景
- 性能优先的非安全关键系统
- 单任务或信任所有任务
- 资源受限的微控制器

---

### 2.2 方案B：完整系统调用（多地址空间模型）

#### 架构图
```
┌─────────────────────────────────────────┐
│  物理内存 (Physical Memory)             │
│                                         │
│  ┌──────────────┐  ┌─────────────────┐ │
│  │ 任务1地址空间│  │ 任务2地址空间   │ │
│  │ (TTBR0_1)    │  │ (TTBR0_2)       │ │
│  │              │  │                 │ │
│  │ ┌──────────┐ │  │ ┌───────────┐  │ │
│  │ │POSIX API │ │  │ │POSIX API  │  │ │
│  │ └──────────┘ │  │ └───────────┘  │ │
│  │              │  │               │  │ │
│  │ ┌──────────┐ │  │ ┌───────────┐  │ │
│  │ │用户数据  │ │  │ │ 用户数据   │  │ │
│  │ └──────────┘ │  │ └───────────┘  │ │ │
│  └──────────────┘  └─────────────────┘ │
│         ↕                    ↕          │
│    ┌─────────────────────────────┐     │
│    │   系统调用边界               │     │
│    │   (EL0 → EL1)               │     │
│    └─────────────────────────────┘     │
│         ↕                    ↕          │
│  ┌────────────────────────────────────┐│
│  │  内核地址空间 (TTBR0_0)            ││
│  │  ┌──────────────────────────┐     ││
│  │  │ 系统调用处理程序          │     ││
│  │  │ syscall_handler()         │     ││
│  │  └──────────────────────────┘     ││
│  │  ┌──────────────────────────┐     ││
│  │  │ 内核函数                  │     ││
│  │  │ task_create()             │     ││
│  │  │ queue_create()            │     ││
│  │  └──────────────────────────┘     ││
│  └────────────────────────────────────┘│
│                                         │
│  ←── 每个任务独立虚拟地址空间 ───→    │
└─────────────────────────────────────────┘
```

#### 实现方式
```c
/* POSIX适配层：通过系统调用调用内核 */
int pthread_create(pthread_t *thread,
                   const pthread_attr_t *attr,
                   void *(*start_routine)(void *),
                   void *arg) {
    /* 系统调用号：SYS_TASK_CREATE_POSIX */
    register uint64_t x0 asm("x0") = (uint64_t)thread;
    register uint64_t x1 asm("x1") = (uint64_t)attr;
    register uint64_t x2 asm("x2") = (uint64_t)start_routine;
    register uint64_t x3 asm("x3") = (uint64_t)arg;
    register uint64_t x8 asm("x8") = SYS_TASK_CREATE_POSIX;

    /* ARMv8-A 系统调用指令 (SVC) */
    asm volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x3), "r"(x8)
        : "memory"
    );

    return (int)x0;  /* 返回值 */
}

/* 内核侧：系统调用处理程序 */
void syscall_handler(void) {
    uint64_t syscall_no = get_syscall_number();

    switch (syscall_no) {
        case SYS_TASK_CREATE_POSIX:
            do_task_create_posix();
            break;
        case SYS_MQ_OPEN:
            do_mq_open();
            break;
        /* ... 其他系统调用 ... */
    }
}
```

#### 系统调用开销分析

| 操作 | 周期数 | 说明 |
|------|--------|------|
| SVC指令 | ~10 | 触发异常 |
| 寄存器保存 | ~50 | 保存x0-x30, sp |
| EL切换 | ~20 | EL0 → EL1 |
| 页表切换 | ~30 | TTBR0切换 + TLB失效 |
| 权限检查 | ~10 | 检查参数合法性 |
| 内核执行 | 变化 | 实际功能 |
| 寄存器恢复 | ~50 | 恢复上下文 |
| ERET返回 | ~10 | 返回用户空间 |
| **总计** | **~180+** | 不包括内核执行时间 |

#### 优缺点分析

| 维度 | 评价 | 说明 |
|------|------|------|
| **性能** | ⭐⭐⭐ | 系统调用开销~180+周期 |
| **安全性** | ⭐⭐⭐⭐⭐ | 完整地址空间隔离 |
| **确定性** | ⭐⭐⭐ | 页表切换时间可变（TLB miss） |
| **代码复杂度** | ⭐⭐ | 需要完整的系统调用框架 |
| **ASIL合规** | ✅ | 完全符合ASIL-D要求 |

#### 适用场景
- 安全关键系统（ASIL-D）
- 需要强大隔离的多任务系统
- 运行不可信第三方的应用

---

### 2.3 方案C：混合方案（可配置的系统调用）

#### 核心思想
**根据任务隔离模式动态选择调用方式：**
- 共享地址空间：直接函数调用（零开销）
- 独立地址空间：系统调用（安全隔离）
- 混合模式：根据调用者和被调用者动态选择

#### 架构图
```
┌─────────────────────────────────────────────────┐
│  编译时配置                                      │
│  CONFIG_SYSCALL=n  →  直接函数调用              │
│  CONFIG_SYSCALL=y  →  通过系统调用              │
└─────────────────────────────────────────────────┘
                    ↓
┌─────────────────────────────────────────────────┐
│  运行时决策 (CONFIG_SYSCALL=y)                   │
│                                                 │
│  if (current_task->isolation_mode == SHARED) {  │
│      // 同一地址空间：直接函数调用              │
│      return kernel_function_direct(...);        │
│  } else {                                       │
│      // 不同地址空间：系统调用                  │
│      return syscall(SYS_XXX, ...);              │
│  }                                              │
└─────────────────────────────────────────────────┘
```

#### 实现方式

##### 2.3.1 编译时配置
```c
/* Kconfig配置 */
choice
    prompt "System Call Mode"

config SYSCALL_NONE
    bool "No System Call (Direct Function Call)"
    help
      所有任务在同一地址空间，POSIX API直接调用内核函数。
      优点：零开销，高性能。
      缺点：无地址空间隔离。

config SYSCALL_ALWAYS
    bool "Always Use System Call"
    help
      所有系统调用都通过SVC指令进入内核。
      优点：完整隔离，安全性高。
      缺点：性能开销（~180周期）。

config SYSCALL_ADAPTIVE
    bool "Adaptive System Call (Recommended)"
    help
      根据任务隔离模式动态选择：
      - 共享地址空间：直接函数调用
      - 独立地址空间：系统调用
      优点：平衡性能和安全性。
      缺点：实现复杂度中等。

endchoice
```

##### 2.3.2 自适应系统调用实现
```c
/**
 * @brief 自适应系统调用包装器
 * @param syscall_nr 系统调用号
 * @param ... 参数
 * @return 系统调用返回值
 *
 * @note 根据任务隔离模式自动选择最优调用方式
 */
static inline long adaptive_syscall(long syscall_nr, ...) {
    va_list args;
    TCB_t *current = get_current_task();
    long result;

    va_start(args, syscall_nr);

    /* 运行时决策 */
    if (current->isolation_mode == TASK_ISOLATION_SHARED) {
        /*
         * 共享地址空间：直接函数调用
         *
         * 优点：
         * - 零系统调用开销（< 10 周期）
         * - 编译器可以内联优化
         * - 无需保存/恢复寄存器
         *
         * 安全性：
         * - 仅在共享地址空间任务间使用
         * - 编译时确保所有调用者可信
         */
        result = syscall_direct_call(syscall_nr, args);
    } else {
        /*
         * 独立地址空间：通过系统调用
         *
         * 优点：
         * - 完整地址空间隔离
         * - 安全关键任务的强制隔离
         *
         * 开销：
         * - ~180 周期（可接受）
         * - 仅在需要隔离时才使用
         */
        result = syscall_svc_call(syscall_nr, args);
    }

    va_end(args);
    return result;
}

/**
 * @brief 直接函数调用（共享地址空间）
 */
static inline long syscall_direct_call(long syscall_nr, va_list args) {
    /*
     * 直接函数调用表
     * 编译时解析，零运行时开销
     */
    static const SyscallFunc_t syscall_table[] = {
        [SYS_TASK_CREATE]   = (SyscallFunc_t)task_create,
        [SYS_MQ_OPEN]       = (SyscallFunc_t)mq_open_impl,
        [SYS_MQ_SEND]       = (SyscallFunc_t)mq_send_impl,
        /* ... 其他系统调用 ... */
    };

    if ((syscall_nr < 0) ||
        (syscall_nr >= (long)(sizeof(syscall_table) / sizeof(syscall_table[0])))) {
        return -EINVAL;
    }

    SyscallFunc_t func = syscall_table[syscall_nr];
    if (func == NULL) {
        return -ENOSYS;
    }

    /* 直接调用内核函数 */
    return func(args);
}

/**
 * @brief SVC系统调用（独立地址空间）
 */
static inline long syscall_svc_call(long syscall_nr, va_list args) {
    register long x0 asm("x0") = va_arg(args, long);
    register long x1 asm("x1") = va_arg(args, long);
    register long x2 asm("x2") = va_arg(args, long);
    register long x3 asm("x3") = va_arg(args, long);
    register long x8 asm("x8") = syscall_nr;

    /* ARMv8-A SVC指令触发异常 */
    asm volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x3), "r"(x8)
        : "memory"
    );

    return x0;
}
```

##### 2.3.3 POSIX API实现
```c
/**
 * @brief pthread_create (自适应实现)
 */
int pthread_create(pthread_t *thread,
                   const pthread_attr_t *attr,
                   void *(*start_routine)(void *),
                   void *arg) {
    PthreadCreateParams_t params = {
        .thread = thread,
        .attr = attr,
        .start_routine = start_routine,
        .arg = arg
    };

    /*
     * 自适应系统调用：
     * - 编译时选择：CONFIG_SYSCALL
     * - 运行时决策：任务隔离模式
     */
    long ret = adaptive_syscall(SYS_PTHREAD_CREATE, &params);

    return (int)ret;
}
```

#### 性能对比

| 场景 | 方案A（无系统调用） | 方案B（系统调用） | 方案C（自适应） |
|------|---------------------|-------------------|-----------------|
| 共享地址空间任务 | ~10 周期 | ~180 周期 | **~10 周期** ✅ |
| 独立地址空间任务 | N/A（不安全） | ~180 周期 | **~180 周期** ✅ |
| 混合场景 | N/A | ~180 周期 | **动态最优** ✅ |

#### 优缺点分析

| 维度 | 方案A | 方案B | 方案C |
|------|-------|-------|-------|
| **性能** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | **⭐⭐⭐⭐⭐** ✅ |
| **安全性** | ⭐ | ⭐⭐⭐⭐⭐ | **⭐⭐⭐⭐⭐** ✅ |
| **确定性** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | **⭐⭐⭐⭐** ✅ |
| **代码复杂度** | ⭐⭐⭐⭐⭐ | ⭐⭐ | **⭐⭐⭐⭐** ✅ |
| **ASIL合规** | ❌ | ✅ | **✅** ✅ |
| **灵活性** | ⭐ | ⭐⭐⭐ | **⭐⭐⭐⭐⭐** ✅ |

---

## 3. 推荐方案：方案C（自适应系统调用）

### 3.1 选择理由

1. **满足功能安全要求**
   - 独立地址空间任务使用系统调用，完全隔离
   - 符合ISO 26262 ASIL-D要求

2. **性能最优**
   - 共享地址空间任务零开销
   - 关键任务路径无系统调用负担

3. **灵活配置**
   - 编译时选择：CONFIG_SYSCALL_NONE/ALWAYS/ADAPTIVE
   - 运行时自适应：根据任务隔离模式

4. **兼容性好**
   - POSIX API行为对应用透明
   - 源代码级兼容

### 3.2 系统调用清单

#### 3.2.1 线程相关
```c
SYS_PTHREAD_CREATE        /* 创建线程 */
SYS_PTHREAD_JOIN          /* 等待线程结束 */
SYS_PTHREAD_EXIT          /* 退出线程 */
SYS_PTHREAD_DETACH        /* 分离线程 */
SYS_PTHREAD_CANCEL        /* 取消线程 */
```

#### 3.2.2 同步原语
```c
SYS_MUTEX_INIT            /* 初始化互斥锁 */
SYS_MUTEX_LOCK            /* 锁定互斥锁 */
SYS_MUTEX_UNLOCK          /* 解锁互斥锁 */
SYS_MUTEX_DESTROY         /* 销毁互斥锁 */
SYS_COND_WAIT             /* 等待条件变量 */
SYS_COND_SIGNAL           /* 唤醒一个等待任务 */
SYS_COND_BROADCAST        /* 唤醒所有等待任务 */
SYS_SEM_INIT              /* 初始化信号量 */
SYS_SEM_WAIT              /* 等待信号量 */
SYS_SEM_POST              /* 增加信号量 */
SYS_BARRIER_WAIT          /* 等待屏障 */
```

#### 3.2.3 消息队列
```c
SYS_MQ_OPEN               /* 打开消息队列 */
SYS_MQ_CLOSE              /* 关闭消息队列 */
SYS_MQ_SEND               /* 发送消息 */
SYS_MQ_RECEIVE            /* 接收消息 */
SYS_MQ_UNLINK             /* 删除消息队列 */
```

#### 3.2.4 内存管理
```c
SYS_MMAP                  /* 内存映射（共享内存） */
SYS_MUNMAP                /* 解除映射 */
SYS_SHM_CREATE            /* 创建共享内存 */
SYS_SHM_ATTACH            /* 附加共享内存 */
SYS_SHM_DETACH            /* 分离共享内存 */
```

#### 3.2.5 定时器
```c
SYS_TIMER_CREATE          /* 创建定时器 */
SYS_TIMER_SETTIME         /* 设置定时器 */
SYS_TIMER_GETOVERRUN      /* 获取溢出计数 */
SYS_TIMER_DELETE          /* 删除定时器 */
```

#### 3.2.6 调度控制
```c
SYS_SCHED_SETSCHEDULER    /* 设置调度策略 */
SYS_SCHED_YIELD           /* 让出CPU */
SYS_TASK_SLEEP            /* 任务休眠 */
```

---

## 4. 实现细节

### 4.1 系统调用处理流程

```
┌────────────────────────────────────────┐
│ 1. 用户空间调用 POSIX API              │
│    pthread_create(...)                 │
└────────────────────────────────────────┘
                ↓
┌────────────────────────────────────────┐
│ 2. 自适应决策                          │
│    if (isolation_mode == SHARED)       │
│        → 直接函数调用                  │
│    else                                │
│        → SVC #0 系统调用               │
└────────────────────────────────────────┘
                ↓
┌────────────────────────────────────────┐
│ 3. 内核入口 (EL0 → EL1)                │
│    exception_vector[0] // SVC handler  │
└────────────────────────────────────────┘
                ↓
┌────────────────────────────────────────┐
│ 4. 系统调用分发                        │
│    syscall_table[x8]()                 │
└────────────────────────────────────────┘
                ↓
┌────────────────────────────────────────┐
│ 5. 权限检查                            │
│    - 参数验证                          │
│    - 能力检查 (CAPABILITY)             │
└────────────────────────────────────────┘
                ↓
┌────────────────────────────────────────┐
│ 6. 执行内核功能                        │
│    task_create_posix(...)              │
└────────────────────────────────────────┘
                ↓
┌────────────────────────────────────────┐
│ 7. 返回用户空间 (EL1 → EL0)            │
│    eret                                │
└────────────────────────────────────────┘
```

### 4.2 系统调用参数传递

**ARM64系统调用调用约定：**
```c
/* 系统调用号 */
x8: syscall_number

/* 参数（最多6个） */
x0: arg1 / return_value
x1: arg2
x2: arg3
x3: arg4
x4: arg5
x5: arg6

/* 系统调用指令 */
svc #0  /* 触发异常 */
```

**错误处理：**
```c
/* 返回值约定 */
if (ret >= 0) {
    /* 成功 */
    return ret;
} else {
    /* 失败：设置errno */
    errno = -ret;
    return -1;
}
```

### 4.3 系统调用表定义

```c
/**
 * @brief 系统调用表
 */
typedef long (*SyscallHandler_t)(uint64_t *params);

static const SyscallHandler_t syscall_table[] = {
    [0]  = NULL,                              /* 未使用 */
    [1]  = sys_pthread_create,                /* SYS_PTHREAD_CREATE */
    [2]  = sys_pthread_join,                  /* SYS_PTHREAD_JOIN */
    [3]  = sys_pthread_exit,                  /* SYS_PTHREAD_EXIT */
    /* ... */
    [64] = sys_mq_open,                       /* SYS_MQ_OPEN */
    [65] = sys_mq_close,                      /* SYS_MQ_CLOSE */
    [66] = sys_mq_send,                       /* SYS_MQ_SEND */
    [67] = sys_mq_receive,                    /* SYS_MQ_RECEIVE */
    /* ... */
};

#define SYSCALL_COUNT (sizeof(syscall_table) / sizeof(syscall_table[0]))

/**
 * @brief 系统调用处理入口
 */
void syscall_handler(void) {
    uint64_t syscall_nr;
    uint64_t params[6];
    SyscallHandler_t handler;
    long ret;

    /* 获取系统调用号 */
    syscall_nr = get_syscall_number();

    /* 参数验证 */
    if (syscall_nr >= SYSCALL_COUNT) {
        ret = -ENOSYS;
        goto out;
    }

    /* 获取处理函数 */
    handler = syscall_table[syscall_nr];
    if (handler == NULL) {
        ret = -ENOSYS;
        goto out;
    }

    /* 获取参数 */
    params[0] = get_user_param(0);
    params[1] = get_user_param(1);
    params[2] = get_user_param(2);
    params[3] = get_user_param(3);
    params[4] = get_user_param(4);
    params[5] = get_user_param(5);

    /* 调用处理函数 */
    ret = handler(params);

out:
    /* 设置返回值 */
    set_return_value(ret);

    /* 返回用户空间 */
    return_from_exception();
}
```

---

## 5. 性能优化

### 5.1 快速系统调用（Fast Syscall）

对于高频系统调用，使用快速路径：

```c
/**
 * @brief 快速系统调用（零拷贝）
 * @param syscall_nr 系统调用号
 * @return 返回值
 *
 * @note 仅用于无参数或简单参数的系统调用
 */
static inline long fast_syscall(long syscall_nr) {
    register long x0 asm("x0") = 0;
    register long x8 asm("x8") = syscall_nr;

    asm volatile(
        "svc #0"
        : "=r"(x0)
        : "r"(x8)
        : "memory"
    );

    return x0;
}

/*
 * 性能对比：
 * - 普通系统调用：~180 周期
 * - 快速系统调用：~120 周期（减少参数保存）
 */
```

### 5.2 批量系统调用

```c
/**
 * @brief 批量系统调用
 * @param calls 系统调用数组
 * @param count 数量
 * @return 成功执行的数量
 *
 * @note 减少多次用户空间↔内核空间切换
 */
ssize_t syscall_batch(SyscallBatch_t *calls, size_t count) {
    size_t i;
    ssize_t success = 0;

    /* 一次性进入内核 */
    for (i = 0; i < count; i++) {
        /* 直接调用内核函数（已在内核空间） */
        long ret = syscall_table[calls[i].nr](calls[i].params);
        if (ret >= 0) {
            success++;
            calls[i].ret = ret;
        } else {
            calls[i].ret = -1;
            calls[i].errno = -ret;
        }
    }

    return success;
}
```

### 5.3 系统调用缓存

```c
/**
 * @brief 系统调用结果缓存
 *
 * 对于幂等的系统调用（如gettimeofday），缓存结果
 */
typedef struct {
    uint64_t timestamp;
    long cached_result;
    uint32_t ttl;  /* Time To Live (ticks) */
} SyscallCache_t;

static SyscallCache_t syscall_cache[SYSCALL_COUNT];

long syscall_with_cache(long syscall_nr, uint64_t *params) {
    SyscallCache_t *cache = &syscall_cache[syscall_nr];

    /* 检查缓存有效性 */
    if ((cache->ttl > 0) &&
        (get_system_ticks() - cache->timestamp < cache->ttl)) {
        return cache->cached_result;
    }

    /* 执行系统调用 */
    long ret = syscall_table[syscall_nr](params);

    /* 更新缓存 */
    cache->cached_result = ret;
    cache->timestamp = get_system_ticks();
    cache->ttl = 10;  /* 缓存10个tick */

    return ret;
}
```

---

## 6. 安全性考虑

### 6.1 参数验证

```c
/**
 * @brief 用户空间指针验证
 * @param ptr 用户空间指针
 * @param size 要访问的大小
 * @return 有效返回true，否则返回false
 */
bool validate_user_ptr(const void *ptr, size_t size) {
    uint64_t addr = (uint64_t)ptr;
    TCB_t *current = get_current_task();

    /* 检查NULL指针 */
    if (ptr == NULL) {
        return false;
    }

    /* 检查地址范围 */
    if (current->isolation_mode == TASK_ISOLATION_SHARED) {
        /*
         * 共享地址空间：
         * - 允许访问所有用户空间地址
         * - 内核数据段受保护（编译时检查）
         */
        return (addr < KERNEL_BASE_ADDR);
    } else {
        /*
         * 独立地址空间：
         * - 严格检查地址范围
         * - 仅允许访问任务自己的地址空间
         */
        return (addr >= USER_SPACE_START) &&
               (addr + size <= USER_SPACE_END) &&
               (addr + size > addr);  /* 防止溢出 */
    }
}
```

### 6.2 能力检查（Capability-based Security）

```c
/**
 * @brief 任务能力定义
 */
typedef uint64_t Capability_t;

#define CAP_PTHREAD_CREATE   (1ULL << 0)  /* 创建线程 */
#define CAP_MQ_OPEN          (1ULL << 1)  /* 打开消息队列 */
#define CAP_SHM_CREATE       (1ULL << 2)  /* 创建共享内存 */
#define CAP_TIMER_CREATE     (1ULL << 3)  /* 创建定时器 */

/**
 * @brief 检查任务能力
 */
bool has_capability(Capability_t cap) {
    TCB_t *current = get_current_task();
    return (current->capabilities & cap) != 0;
}

/**
 * @brief 系统调用能力检查
 */
long sys_pthread_create(uint64_t *params) {
    /* 能力检查 */
    if (!has_capability(CAP_PTHREAD_CREATE)) {
        return -EPERM;
    }

    /* 参数验证 */
    if (!validate_user_ptr((void *)params[0], sizeof(pthread_t))) {
        return -EFAULT;
    }

    /* 执行系统调用 */
    return do_pthread_create(params);
}
```

### 6.3 拒绝服务（DoS）防护

```c
/**
 * @brief 系统调用速率限制
 */
typedef struct {
    uint32_t count;
    uint64_t last_reset;
} RateLimiter_t;

static RateLimiter_t syscall_rate_limits[SYSCALL_COUNT];

#define MAX_SYSCALLS_PER_SEC  1000U

/**
 * @brief 检查系统调用速率限制
 */
bool check_syscall_rate_limit(long syscall_nr) {
    RateLimiter_t *limiter = &syscall_rate_limits[syscall_nr];
    uint64_t now = get_system_time_ms();

    /* 每秒重置计数 */
    if (now - limiter->last_reset >= 1000U) {
        limiter->count = 0;
        limiter->last_reset = now;
    }

    /* 检查是否超过限制 */
    if (limiter->count >= MAX_SYSCALLS_PER_SEC) {
        return false;
    }

    limiter->count++;
    return true;
}
```

---

## 7. 调试支持

### 7.1 系统调用跟踪

```c
/**
 * @brief 系统调用跟踪
 */
#ifdef CONFIG_SYSCALL_TRACE

typedef struct {
    long syscall_nr;
    uint64_t params[6];
    long ret;
    uint64_t timestamp;
    uint32_t task_id;
} SyscallTraceEntry_t;

#define SYSCALL_TRACE_BUF_SIZE  1024

static SyscallTraceEntry_t syscall_trace_buffer[SYSCALL_TRACE_BUF_SIZE];
static uint32_t syscall_trace_index = 0;

/**
 * @brief 记录系统调用
 */
void trace_syscall(long syscall_nr, uint64_t *params, long ret) {
    SyscallTraceEntry_t *entry;
    uint32_t index;

    index = atomic_fetch_add(&syscall_trace_index, 1U) % SYSCALL_TRACE_BUF_SIZE;
    entry = &syscall_trace_buffer[index];

    entry->syscall_nr = syscall_nr;
    entry->ret = ret;
    entry->timestamp = get_system_time_ns();
    entry->task_id = get_current_task_id();

    /* 复制参数 */
    for (uint32_t i = 0; i < 6; i++) {
        entry->params[i] = params[i];
    }
}

#endif /* CONFIG_SYSCALL_TRACE */
```

### 7.2 系统调用统计

```c
/**
 * @brief 系统调用性能统计
 */
typedef struct {
    uint64_t count;        /* 调用次数 */
    uint64_t total_cycles;  /* 总周期数 */
    uint64_t max_cycles;    /* 最大周期数 */
    uint64_t min_cycles;    /* 最小周期数 */
} SyscallStats_t;

static SyscallStats_t syscall_stats[SYSCALL_COUNT];

/**
 * @brief 更新系统调用统计
 */
void update_syscall_stats(long syscall_nr, uint64_t cycles) {
    SyscallStats_t *stats = &syscall_stats[syscall_nr];

    stats->count++;
    stats->total_cycles += cycles;

    if (cycles > stats->max_cycles) {
        stats->max_cycles = cycles;
    }

    if (cycles < stats->min_cycles) {
        stats->min_cycles = cycles;
    }
}
```

---

## 8. 测试策略

### 8.1 单元测试

```c
/**
 * @brief 系统调用单元测试
 */
void test_syscall_basic(void) {
    /* 测试1：基本系统调用 */
    int ret = syscall(SYS_GETPID);
    TEST_ASSERT(ret > 0);

    /* 测试2：无效系统调用号 */
    ret = syscall(9999);
    TEST_ASSERT(ret == -ENOSYS);

    /* 测试3：参数验证 */
    ret = syscall(SYS_PTHREAD_CREATE, NULL, NULL, NULL);
    TEST_ASSERT(ret == -EINVAL);
}

void test_syscall_performance(void) {
    uint64_t start, end;
    const int iterations = 10000;
    uint64_t total = 0;

    for (int i = 0; i < iterations; i++) {
        start = get_cycles();
        syscall(SYS_YIELD);
        end = get_cycles();

        total += (end - start);
    }

    uint64_t avg = total / iterations;
    printf("Average syscall cycles: %llu\n", avg);

    /* 断言：平均系统调用周期 < 200 */
    TEST_ASSERT(avg < 200);
}
```

### 8.2 集成测试

```c
/**
 * @brief 多任务隔离模式测试
 */
void test_mixed_isolation_modes(void) {
    pthread_t task_shared, task_private;

    /* 创建共享地址空间任务 */
    pthread_create_shared(&task_shared, NULL, task_func, NULL);

    /* 创建独立地址空间任务 */
    pthread_create_private(&task_private, NULL, task_func, NULL);

    /* 验证隔离 */
    TEST_ASSERT(task_shared->isolation_mode == TASK_ISOLATION_SHARED);
    TEST_ASSERT(task_private->isolation_mode == TASK_ISOLATION_PRIVATE);

    /* 验证通信机制 */
    test_ipc_between_isolated_tasks();
}
```

---

## 9. 总结

### 9.1 方案选择矩阵

| 需求 | 方案A | 方案B | **方案C (推荐)** |
|------|-------|-------|-----------------|
| 性能要求高 | ✅ | ❌ | ✅ |
| 安全隔离 | ❌ | ✅ | ✅ |
| ASIL-D合规 | ❌ | ✅ | ✅ |
| 灵活性 | ❌ | ⭐⭐⭐ | ✅ |
| 复杂度 | ⭐⭐⭐⭐⭐ | ⭐⭐ | ⭐⭐⭐⭐ |

### 9.2 推荐配置

**典型配置1：高性能实时系统**
```kconfig
CONFIG_SYSCALL=SYSCALL_ADAPTIVE
CONFIG_TASK_ISOLATION_DEFAULT=SHARED
# 大多数任务共享地址空间，零系统调用开销
```

**典型配置2：安全关键系统（ASIL-D）**
```kconfig
CONFIG_SYSCALL=SYSCALL_ALWAYS
CONFIG_TASK_ISOLATION_DEFAULT=PRIVATE
# 所有任务独立地址空间，完全隔离
```

**典型配置3：混合系统（推荐）**
```kconfig
CONFIG_SYSCALL=SYSCALL_ADAPTIVE
CONFIG_TASK_ISOLATION_DEFAULT=HYBRID
# 高优先级任务独立，低优先级任务共享
CONFIG_TASK_ISOLATION_HIGH_PRIO_THRESHOLD=200
```

### 9.3 实现路线图

**阶段1：基础设施（4周）**
- 系统调用框架
- 异常向量表
- 系统调用表
- 基本系统调用（10个）

**阶段2：完整POSIX支持（8周）**
- 线程相关系统调用
- 同步原语系统调用
- 消息队列系统调用
- 共享内存系统调用

**阶段3：优化和测试（4周）**
- 自适应系统调用
- 性能优化
- 安全性测试
- MISRA-C合规验证

**总计：16周（4个月）**

---

## 附录A：系统调用号分配

```c
/* 系统调用号分配 */
#define SYSCALL_BASE              0

/* 线程管理 (1-10) */
#define SYS_PTHREAD_CREATE        1
#define SYS_PTHREAD_JOIN          2
#define SYS_PTHREAD_EXIT          3
#define SYS_PTHREAD_DETACH        4
#define SYS_PTHREAD_CANCEL        5

/* 互斥锁 (10-20) */
#define SYS_MUTEX_INIT            10
#define SYS_MUTEX_LOCK            11
#define SYS_MUTEX_UNLOCK          12
#define SYS_MUTEX_DESTROY         13
#define SYS_MUTEX_TRYLOCK         14

/* 信号量 (20-30) */
#define SYS_SEM_INIT              20
#define SYS_SEM_WAIT              21
#define SYS_SEM_POST              22
#define SYS_SEM_DESTROY           23

/* 条件变量 (30-40) */
#define SYS_COND_INIT             30
#define SYS_COND_WAIT             31
#define SYS_COND_SIGNAL           32
#define SYS_COND_BROADCAST        33
#define SYS_COND_DESTROY          34

/* 消息队列 (64-70) */
#define SYS_MQ_OPEN               64
#define SYS_MQ_CLOSE              65
#define SYS_MQ_SEND               66
#define SYS_MQ_RECEIVE            67
#define SYS_MQ_UNLINK             68
#define SYS_MQ_GETATTR            69
#define SYS_MQ_SETATTR            70

/* 共享内存 (80-90) */
#define SYS_SHM_CREATE            80
#define SYS_SHM_ATTACH            81
#define SYS_SHM_DETACH            82
#define SYS_SHM_DESTROY           83

/* 定时器 (90-100) */
#define SYS_TIMER_CREATE          90
#define SYS_TIMER_SETTIME         91
#define SYS_TIMER_GETOVERRUN      92
#define SYS_TIMER_DELETE          93

/* 调度控制 (100-110) */
#define SYS_SCHED_SETSCHEDULER    100
#define SYS_SCHED_GETSCHEDULER    101
#define SYS_SCHED_YIELD           102
#define SYS_TASK_SLEEP            103
```

---

**文档结束**
