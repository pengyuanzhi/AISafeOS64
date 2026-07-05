# AISafeOS64 内核架构重构设计文档

**版本**: 1.0
**日期**: 2026-07-05
**状态**: 评审中
**作者**: AISafe64 Team

---

## 1. 现状评估

### 1.1 微内核架构原则符合度

| 原则 | 评分 | 问题 |
|------|------|------|
| 最小化内核功能 | 4/10 | 驱动/ELF加载/验证/堆策略全在内核（52KB text） |
| 机制与策略分离 | 5/10 | EDF/ARINC653/驱动匹配/堆策略混入内核 |
| 消息传递通信 | 7/10 | IPC框架正确，但驱动I/O走直接函数调用而非IPC |
| 地址空间隔离 | 6/10 | 真实服务隔离OK，entry.c假EL0服务共享内核全局 |
| 最小TCB | 4/10 | ~30%代码（16KB）可移出TCB |

### 1.2 QNX 核心机制覆盖

| 机制 | 状态 | 说明 |
|------|------|------|
| 线程原语 | ✅ 完整 | CREATE/EXIT/SUSPEND/RESUME/SET_PRIORITY/YIELD |
| 同步IPC | ✅ 基本完整 | SEND/RECV/REPLY（缺 MsgError） |
| 异步通知 | ✅ 完整 | Pulse + Notification |
| 中断绑定 | ✅ 有 | InterruptAttach/Detach |
| 内存映射 | ✅ 有 | VM_MAP/UNMAP/VIRT_TO_PHYS |
| 能力系统 | ✅ 有 | CSPACE/CAP_COPY/MOVE/REVOKE/DELETE |
| **进程管理** | ❌ 缺失 | 无 fork/exec/waitpid/getpid |
| **POSIX信号** | ❌ 缺失 | 无 signal_action/kill/procmask |
| **用户定时器** | ❌ 缺失 | 无 timer_create/nanosleep/clock_gettime |
| **init启动链** | ❌ 缺失 | 无内核→init.elf启动路径 |
| 名称服务 | ⚠️ 部分 | 用户态有PathManager但内核不引导 |

---

## 2. 目标架构

### 2.1 内核最小集定义

重构后内核只保留：

```
┌─────────────────────────────────────────────────┐
│                  内核（TCB）                      │
│                                                   │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐         │
│  │  调度器   │ │   IPC    │ │ 内存管理  │         │
│  │ sched_class│ │ endpoint │ │ page_table│         │
│  │ RR/FIFO  │ │ channel  │ │ vmspace  │         │
│  │ ARINC653 │ │ notify   │ │ phys_mem │         │
│  └──────────┘ └──────────┘ └──────────┘         │
│                                                   │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐         │
│  │ 中断/异常 │ │ 能力系统  │ │  进程管理 │         │
│  │ hal_intc │ │ cspace   │ │ process  │         │
│  │ syscall  │ │ cap      │ │ signal   │         │
│  └──────────┘ └──────────┘ └──────────┘         │
│                                                   │
│  ┌──────────┐ ┌──────────┐                       │
│  │  定时器   │ │  klog    │                       │
│  │ hrtimer  │ │ 日志接口  │                       │
│  └──────────┘ └──────────┘                       │
└─────────────────────────────────────────────────┘
          target text < 50KB

┌─────────────────────────────────────────────────┐
│              用户态服务                            │
│                                                   │
│  init │ proc │ fs │ net │ driver │ security │ ...│
│  (启动) (进程) (文件) (网络) (设备)   (安全)      │
└─────────────────────────────────────────────────┘
```

### 2.2 调度类框架设计（sched_class）

```c
/**
 * @brief 调度类（策略可插拔）
 * 每种调度策略实现一个 sched_class 实例，
 * schedule() 按优先级遍历调度类链表调用 pick_next。
 */
struct sched_class
{
    const char *name;                                    /* 策略名称 */
    int priority;                                        /* 类优先级（高优先） */
    void (*enqueue)(struct KThread *thread);             /* 线程就绪 */
    void (*dequeue)(struct KThread *thread);             /* 线程离开就绪 */
    struct KThread *(*pick_next)(void);                  /* 选择下一个 */
    void (*tick)(struct KThread *current);               /* 时钟tick */
    struct sched_class *next;                            /* 链表 */
};

/* 策略实例 */
extern struct sched_class sched_rr;       /* 优先级+RR */
extern struct sched_class sched_fifo;     /* 严格优先级 */
extern struct sched_class sched_arinc653; /* ARINC653分区 */

/* KThread_t 中添加 */
struct KThread
{
    ...
    struct sched_class *sched_class;  /* 所属调度类 */
    ...
};
```

**移除 EDF**：edf.c 删除，EDF 不适合安全关键 RTOS（WCET 分析复杂）。

### 2.3 中断控制器 HAL 抽象设计

```c
/* hal_intc.h — 中断控制器抽象（GIC不泄漏到内核核心） */

/* 初始化 */
void hal_intc_init(void);
void hal_intc_init_secondary(void);

/* 单线控制 */
void hal_intc_enable(uint32_t irq);
void hal_intc_disable(uint32_t irq);

/* 配置 */
void hal_intc_set_priority(uint32_t irq, uint8_t prio);
void hal_intc_set_affinity(uint32_t irq, uint32_t cpu_mask);
void hal_intc_set_trigger(uint32_t irq, irq_trigger_t trigger);

/* 中断处理 */
uint32_t hal_intc_acknowledge(void);   /* 取中断号 */
void hal_intc_eoi(uint32_t irq);       /* 结束中断 */
bool hal_intc_is_spurious(uint32_t irq);

/* 中断类型判定 */
bool hal_intc_is_sgi(uint32_t irq);    /* 软件中断 */
bool hal_intc_is_ppi(uint32_t irq);    /* 私有外设 */
bool hal_intc_is_spi(uint32_t irq);    /* 共享外设 */

/* IPI */
void hal_intc_send_ipi(uint32_t cpu_mask, uint32_t ipi_type);

/* 类型定义 */
typedef enum
{
    IRQ_TRIGGER_EDGE_RISING = 0U,
    IRQ_TRIGGER_EDGE_FALLING = 1U,
    IRQ_TRIGGER_LEVEL_HIGH = 2U,
    IRQ_TRIGGER_LEVEL_LOW = 3U
} irq_trigger_t;
```

**中断接口命名统一**：
- `irq_attach(irq, handler, arg)` — 用户态中断绑定（通过 notification）
- `irq_detach(irq)` — 解绑
- `irq_register_handler(irq, handler, arg)` — 内核回调注册
- `irq_unregister_handler(irq)` — 注销
- 删除 `interrupt_` 前缀，统一 `irq_`
- 删除 kernel/irq/interrupt.c 中所有 `gic_*` 直接调用

### 2.4 kobject 统一对象管理

```c
/* kobject.h — 重命名 KObjHeader_t → kobj_header_t */

typedef struct kobj_header
{
    kobj_type_t     type;           /* 对象类型 */
    kobj_id_t       id;            /* 对象ID */
    volatile int32_t ref_count;     /* 引用计数 */
    struct list_head children;      /* 子对象链表 */
    struct list_head sibling;       /* 兄弟节点 */
    kobj_id_t       parent_id;      /* 父对象ID */
} kobj_header_t;

/* 所有内核对象首成员为 kobj_header_t */
typedef struct
{
    kobj_header_t header;           /* 必须是首成员 */
    ipc_ep_state_t state;
    thread_id_t owner_tid;
    ...
} ipc_endpoint_t;

/* 引用计数生命周期 */
void *kobj_alloc(kobj_type_t type, size_t size);
void kobj_ref_inc(kobj_header_t *hdr);
void kobj_ref_dec(kobj_header_t *hdr);  /* 归零→kobj_destroy */
void kobj_destroy(kobj_header_t *hdr);  /* 迭代式级联销毁 */

/* 能力与引用计数挂钩 */
/* cap_copy 时: kobj_ref_inc(target) */
/* cap_revoke/delete 时: kobj_ref_dec(target) */
/* cspace_destroy 时: 遍历所有cap, ref_dec */
```

### 2.5 地址空间管理

```c
/* 统一到 vmspace 抽象 */

/* scheduler 调用 */
void vmspace_switch(vm_space_t *space);

/* 内部实现（hal层） */
void hal_write_ttbr0(uint64_t pgd_phys | (asid << 48));
void hal_tlb_flush_asid(uint16_t asid);    /* tlbi asides1,asid */
void hal_write_contextidr(uint64_t value);  /* 写 CONTEXTIDR_EL1 */

/* 删除 mmu.c 的 s_user_pgds 静态池 */
/* 改用 vmspace_create 动态分配 */
/* PGD 位图分配加锁 */
```

### 2.6 日志接口（klog）

```c
/* klog.h */
typedef enum
{
    KLOG_LEVEL_ERROR = 0,
    KLOG_LEVEL_WARN  = 1,
    KLOG_LEVEL_INFO  = 2,
    KLOG_LEVEL_DEBUG = 3
} klog_level_t;

void klog_set_level(klog_level_t level);
void klog_error(const char *fmt, ...);
void klog_warn(const char *fmt, ...);
void klog_info(const char *fmt, ...);
void klog_debug(const char *fmt, ...);

/* 内部固定 UART base，不暴露给调用者 */
/* 生产模式(CONFIG_DEBUG=0): 只输出 ERROR+WARN */
/* 开发模式(CONFIG_DEBUG=1): 全级别输出 */
```

### 2.7 进程抽象

```c
/* process.h */
typedef struct process
{
    kobj_header_t   header;
    vm_space_t     *vmspace;        /* 地址空间 */
    cspace_t       *cspace;         /* 能力空间 */
    list_head_t     thread_list;    /* 线程组 */
    uint32_t        thread_count;
    process_id_t    pid;
    process_id_t    parent_pid;
    int32_t         exit_status;
    uint32_t        flags;
} process_t;

/* Syscall */
SYS_PROCESS_CREATE   /* 创建进程（新地址空间+CSpace） */
SYS_PROCESS_EXIT     /* 进程退出（释放全部资源） */
SYS_PROCESS_WAIT     /* 等待子进程 */
SYS_PROCESS_GETPID   /* 获取当前PID */
```

### 2.8 信号机制

```c
/* signal.h */
#define SIG_MAX  32U

typedef struct
{
    void (*handler)(int sig);  /* 信号处理函数 */
    uint32_t mask;             /* 屏蔽字 */
} signal_state_t;

/* Syscall */
SYS_SIGNAL_ACTION    /* 注册信号处理函数 */
SYS_SIGNAL_KILL      /* 向线程发送信号 */
SYS_SIGNAL_PROCMASK  /* 设置屏蔽字 */
SYS_SIGNAL_RETURN    /* 信号处理返回 */
```

### 2.9 用户定时器

```c
/* timer_service.h */
typedef struct user_timer
{
    kobj_header_t   header;
    tick_t          expire_tick;
    tick_t          interval;
    thread_id_t     owner;
    kobj_id_t       notify_ep;   /* 超时通知端点 */
    bool            active;
} user_timer_t;

/* Syscall */
SYS_TIMER_CREATE    /* 创建定时器 */
SYS_TIMER_SETTIME   /* 设置定时时间 */
SYS_TIMER_DELETE    /* 删除定时器 */
SYS_NANOSLEEP       /* 纳秒级睡眠 */
SYS_CLOCK_GETTIME   /* 获取当前时间 */
```

### 2.10 init 启动链

```
内核启动:
  1. HAL/MMU/GIC/Timer/调度器/IPC/能力 初始化
  2. 从磁盘加载 init.elf（boot_blk 极简读取器）
  3. 创建 init 进程（独立地址空间+CSpace）
  4. eret 到 init 的 EL0 入口
  5. 内核进入 idle 调度

init 服务（用户态）:
  1. 注册名称服务端点
  2. 从磁盘加载 proc/fs/net/driver 等服务
  3. 通过 IPC 协调服务启动
  4. 进入服务调度循环
```

---

## 3. 内核/用户态职责划分

### 3.1 内核保留（TCB）

| 组件 | 说明 |
|------|------|
| 调度器 | sched_class 框架 + RR/FIFO 策略（机制） |
| IPC | endpoint/channel/notification（机制） |
| 页表管理 | page_table map/unmap/lookup（机制） |
| 物理内存 | phys_mem buddy 分配器（机制） |
| vmspace | 地址空间创建/切换/VMA（机制） |
| 中断/异常 | hal_intc + 异常向量 + irq路由 |
| 能力系统 | CSpace + cap验证（机制） |
| 定时器 | hrtimer + 用户定时器分发 |
| 进程管理 | Process_t + fork/exec/waitpid |
| klog | 日志接口 |
| kobject | 统一对象生命周期 |

### 3.2 移到用户态

| 组件 | 移至 | 说明 |
|------|------|------|
| drv_virtio_blk.c | services/dev/ | 块设备驱动 |
| drv_uart.c | services/dev/ | UART驱动（内核仅留HAL最小console） |
| driver_core.c | services/dev/ | 设备管理器 |
| driver_module.c | 删除 | 内核态模块加载不安全 |
| elf_loader.c | kernel/mm/→用户态proc | 进程创建器 |
| verify/ | 删除 | 离线证明不属运行时 |
| EDF策略 | 删除 | 用优先级+RR替代 |
| 测试/bench代码 | tests/ | CONFIG_SELFTEST |

---

## 4. 目录结构设计

```
kernel/
├── arch/arm64/          # ARM64架构实现
│   ├── boot.S           # 启动汇编
│   ├── context.S        # 上下文切换
│   ├── exception.S      # 异常向量
│   ├── hal.c            # HAL实现（CPU状态/缓存/屏障/UART）
│   ├── hal_intc.c       # 中断控制器HAL（GIC后端）
│   ├── hal_mmu.c        # MMU HAL（TTBR/TLB）
│   ├── entry.c          # kernel_main + 异常处理（<300行）
│   └── ipi.c            # IPI实现
├── sched/               # 调度器
│   ├── sched_class.h    # 调度类接口
│   ├── scheduler.c      # 核心调度器（机制）
│   ├── sched_rr.c       # RR策略
│   ├── sched_fifo.c     # FIFO策略
│   ├── thread.c         # 线程管理
│   ├── timer.c          # 定时器基础设施
│   ├── spinlock.c       # 自旋锁
│   └── smp.c            # SMP负载均衡
├── ipc/                 # IPC
│   ├── endpoint.c       # 同步IPC
│   ├── channel.c        # 异步通道
│   ├── notification.c   # 通知
│   └── ic2.c            # SPSC环形缓冲
├── mm/                  # 内存管理
│   ├── page_table.c     # 页表
│   ├── vmspace.c        # 地址空间+VMA
│   ├── phys_mem.c       # 物理内存buddy
│   ├── slab.c           # Slab分配器
│   ├── kmalloc.c        # 内核堆
│   ├── kobject.c        # 统一对象管理
│   ├── object_pool.c    # 通用对象池
│   ├── uaccess.c        # 用户指针验证
│   └── elf_loader.c     # ELF加载器（中期迁用户态）
├── cap/                 # 能力系统
│   ├── cspace.c         # CSpace管理
│   └── capability.c     # 能力操作
├── irq/                 # 中断/系统调用
│   ├── irq.c            # 中断路由（原interrupt.c）
│   └── syscall.c        # 系统调用分发（原syscall_dispatch.c）
├── process/             # 进程管理
│   ├── process.c        # Process_t管理
│   └── signal.c         # POSIX信号
├── klog.c               # 内核日志
└── CMakeLists.txt
```

---

## 5. 锁架构设计

### 5.1 全局锁顺序（从高到低）

```
1. per-CPU就绪队列锁   (cpu_q->lock)      — 最高优先
2. CSpace锁            (cspace_t.lock)
3. 端点锁              (ipc_endpoint_t.lock)
4. 通道锁              (ipc_channel_t.lock)
5. 定时器队列锁        (s_timer_locks[])
6. 睡眠队列锁          (s_sleep_locks[])
7. kmalloc锁           (s_kmalloc_state.lock)
8. 物理内存锁          (s_buddy.lock)
9. 对象池锁            (object_pool.lock)
```

### 5.2 锁使用规则

- 获取多个锁时，必须按上述顺序
- 持锁时禁止：kmalloc/schedule/context_switch/hal_uart
- 自旋锁持锁时间 < 1μs
- 临界区 < 50 行代码
- 中断上下文中获取的锁必须用 irqsave 版本

---

## 6. 系统调用接口设计

### 6.1 统一编号方案

```
0x00xx — 线程管理
0x01xx — IPC
0x02xx — 内存管理
0x03xx — 能力管理
0x04xx — 中断管理
0x05xx — 进程管理（新增）
0x06xx — 信号（新增）
0x07xx — 定时器（新增）
0x08xx — 调试/系统信息
```

### 6.2 新增系统调用

```
/* 进程管理 */
0x0500  SYS_PROCESS_CREATE
0x0501  SYS_PROCESS_EXIT
0x0502  SYS_PROCESS_WAIT
0x0503  SYS_PROCESS_GETPID

/* 信号 */
0x0600  SYS_SIGNAL_ACTION
0x0601  SYS_SIGNAL_KILL
0x0602  SYS_SIGNAL_PROCMASK
0x0603  SYS_SIGNAL_RETURN

/* 定时器 */
0x0700  SYS_TIMER_CREATE
0x0701  SYS_TIMER_SETTIME
0x0702  SYS_TIMER_DELETE
0x0703  SYS_NANOSLEEP
0x0704  SYS_CLOCK_GETTIME
```
