# AISafe64 优先级 P1 项目详细实施方案

## 文档信息
- **版本**: 1.0
- **日期**: 2025-01-08
- **作者**: AISafe64 Team

---

## 目录
1. [保护域简化版](#1-保护域简化版)
2. [自适应分区](#2-自适应分区)
3. [AISafe-eBPF](#3-aiebpf)
4. [模块化驱动框架](#4-模块化驱动框架)
5. [形式化验证](#5-形式化验证)

---

<a name="1"></a>
## 1. 保护域简化版

### 1.1 项目概述

| 属性 | 值 |
|------|-----|
| **优先级** | P1 |
| **工期** | 4周 |
| **价值** | 高 |
| **成本** | 中 |
| **风险** | 低 |
| **参考** | Zephyr, NuttX |
| **MISRA** | 完全合规 |

### 1.2 设计原则

**简化策略**:
- **静态配置**: 启动时定义，运行时不修改
- **最小数量**: 4-8 个保护域
- **固定分区**: 每域固定资源预算
- **透明集成**: 对现有调度器影响最小

### 1.3 保护域定义

```c
/* src/include/protection_domain.h */

/* 预定义保护域 */
typedef enum {
    PD_KERNEL = 0,        /* 内核域 */
    PD_DRIVER,            /* 驱动域 */
    PD_APP_CRITICAL,      /* 关键应用域 */
    PD_APP_NORMAL,        /* 普通应用域 */
    PD_APP_UNTRUSTED,     /* 非可信应用域 */
    PD_COUNT
} ProtectionDomainID_t;

/* 内存分区 */
typedef struct {
    uint64_t base;
    uint64_t size;
    uint32_t flags;       /* MPU/MMU 权限 */
    uint32_t region_id;   /* MPU 区域号或 MMU 页表索引 */
} MemPartition_t;

/* 保护域配置（静态）*/
typedef struct {
    uint32_t pd_id;
    const char *name;
    ProtectionDomainID_t type;

    /* 内存分区 */
    MemPartition_t partitions[4];
    uint32_t partition_count;

    /* 资源限制 */
    uint32_t max_tasks;
    uint32_t max_memory;
    uint32_t max_cpu_percent;

    /* 任务列表 */
    struct list_head tasks;
    uint32_t task_count;

    /* 页表（MMU）*/
    uint64_t *page_table;

} ProtectionDomain_t;
```

### 1.4 启动时配置

```c
/* src/kernel/protection_domain.c */

static ProtectionDomain_t pd_table[PD_COUNT];

void init_protection_domains(void) {
    /* 1. 内核域 */
    pd_table[PD_KERNEL].pd_id = PD_KERNEL;
    pd_table[PD_KERNEL].name = "kernel";
    pd_table[PD_KERNEL].type = PD_KERNEL;
    pd_table[PD_KERNEL].max_tasks = 64;
    pd_table[PD_KERNEL].max_memory = 16 * 1024 * 1024;  /* 16MB */

    /* 内存分区（内核空间）*/
    pd_table[PD_KERNEL].partitions[0] = (MemPartition_t){
        .base = 0xFFFF00000000ULL,
        .size = 0x100000000ULL,  /* 4GB */
        .flags = MEM_PERM_READ | MEM_PERM_WRITE | MEM_PERM_EXEC | MEM_PERM_PRIV,
        .region_id = 0
    };
    pd_table[PD_KERNEL].partition_count = 1;

    INIT_LIST_HEAD(&pd_table[PD_KERNEL].tasks);

    /* 2. 驱动域 */
    pd_table[PD_DRIVER].pd_id = PD_DRIVER;
    pd_table[PD_DRIVER].name = "drivers";
    pd_table[PD_DRIVER].type = PD_DRIVER;
    pd_table[PD_DRIVER].max_tasks = 32;
    pd_table[PD_DRIVER].max_memory = 8 * 1024 * 1024;  /* 8MB */
    pd_table[PD_DRIVER].max_cpu_percent = 30;

    /* 3. 关键应用域 */
    pd_table[PD_APP_CRITICAL].pd_id = PD_APP_CRITICAL;
    pd_table[PD_APP_CRITICAL].name = "apps_critical";
    pd_table[PD_APP_CRITICAL].type = PD_APP_CRITICAL;
    pd_table[PD_APP_CRITICAL].max_tasks = 16;
    pd_table[PD_APP_CRITICAL].max_memory = 32 * 1024 * 1024;  /* 32MB */
    pd_table[PD_APP_CRITICAL].max_cpu_percent = 40;

    /* 4. 普通应用域 */
    pd_table[PD_APP_NORMAL].pd_id = PD_APP_NORMAL;
    pd_table[PD_APP_NORMAL].name = "apps_normal";
    pd_table[PD_APP_NORMAL].type = PD_APP_NORMAL;
    pd_table[PD_APP_NORMAL].max_tasks = 64;
    pd_table[PD_APP_NORMAL].max_memory = 64 * 1024 * 1024;  /* 64MB */
    pd_table[PD_APP_NORMAL].max_cpu_percent = 20;

    /* 5. 非可信应用域 */
    pd_table[PD_APP_UNTRUSTED].pd_id = PD_APP_UNTRUSTED;
    pd_table[PD_APP_UNTRUSTED].name = "apps_untrusted";
    pd_table[PD_APP_UNTRUSTED].type = PD_APP_UNTRUSTED;
    pd_table[PD_APP_UNTRUSTED].max_tasks = 128;
    pd_table[PD_APP_UNTRUSTED].max_memory = 128 * 1024 * 1024;  /* 128MB */
    pd_table[PD_APP_UNTRUSTED].max_cpu_percent = 10;
}
```

### 1.5 任务管理

```c
/* 添加任务到保护域 */
int pd_add_task(uint32_t pd_id, TCB_t *task) {
    if (pd_id >= PD_COUNT || task == NULL) {
        return -EINVAL;
    }

    ProtectionDomain_t *pd = &pd_table[pd_id];

    /* 检查资源限制 */
    if (pd->task_count >= pd->max_tasks) {
        printk("PD %u: task limit exceeded\n", pd_id);
        return -EAGAIN;
    }

    /* 设置任务的保护域 */
    task->protection_domain = pd;
    task->pd_id = pd_id;

    /* 添加到任务列表 */
    list_add_tail(&task->pd_list, &pd->tasks);
    pd->task_count++;

    printk("Task %u added to PD %u (%s)\n",
           task->tid, pd_id, pd->name);

    return 0;
}

/* 从保护域移除任务 */
int pd_remove_task(TCB_t *task) {
    if (task == NULL) {
        return -EINVAL;
    }

    ProtectionDomain_t *pd = task->protection_domain;
    if (pd == NULL) {
        return -EINVAL;
    }

    /* 从列表移除 */
    list_del_init(&task->pd_list);
    pd->task_count--;

    /* 清除引用 */
    task->protection_domain = NULL;

    return 0;
}
```

### 1.6 上下文切换集成

```c
/* 在 context_switch() 中调用 */
void pd_context_switch(TCB_t *next) {
    if (next == NULL || next->protection_domain == NULL) {
        return;
    }

    ProtectionDomain_t *pd = next->protection_domain;

    /* 配置 MPU/MMU */
    for (uint32_t i = 0U; i < pd->partition_count; i++) {
        mp_configure_region(&pd->partitions[i]);
    }

    /* 如果是 MMU，切换页表 */
    if (pd->page_table != NULL) {
        write_ttbr0_el1((uint64_t)pd->page_table);
        __asm__ volatile("tlbi aside1is, %0" :: "r"(next->tid));
        __asm__ volatile("dsb ish");
    }
}

/* 修改 scheduler.c */
void context_switch(TCB_t *prev, TCB_t *next) {
    /* ... 现有代码 ... */

    /* 保护域切换 */
    pd_context_switch(next);

    /* 上下文切换 */
    save_context(prev);
    restore_context(next);
}
```

### 1.7 资源统计

```c
/* 统计信息 */
typedef struct {
    uint32_t pd_id;
    const char *name;
    uint32_t task_count;
    uint32_t max_tasks;
    uint64_t memory_used;
    uint64_t max_memory;
    uint32_t cpu_usage_percent;
} ProtectionDomainStats_t;

int pd_get_stats(uint32_t pd_id, ProtectionDomainStats_t *stats) {
    if (pd_id >= PD_COUNT || stats == NULL) {
        return -EINVAL;
    }

    ProtectionDomain_t *pd = &pd_table[pd_id];

    stats->pd_id = pd->pd_id;
    stats->name = pd->name;
    stats->task_count = pd->task_count;
    stats->max_tasks = pd->max_tasks;
    stats->memory_used = calculate_memory_usage(pd);
    stats->max_memory = pd->max_memory;
    stats->cpu_usage_percent = calculate_cpu_usage(pd);

    return 0;
}
```

### 1.8 使用示例

```c
/* 应用加载器中使用 */
int app_loader_load_all(const char *config_path) {
    /* 解析配置 */
    AppConfig config;

    while (read_next_config(&config)) {
        /* 创建任务 */
        TCB_t *task = task_create(
            config.name,
            config.priority,
            config.stack_size,
            config.entry
        );

        /* 添加到保护域 */
        uint32_t pd_id = PD_APP_NORMAL;
        if (config.is_critical) {
            pd_id = PD_APP_CRITICAL;
        } else if (config.is_untrusted) {
            pd_id = PD_APP_UNTRUSTED;
        }

        pd_add_task(pd_id, task);
    }

    return 0;
}
```

### 1.9 验收标准

- [ ] 6/6 保护域正确配置
- [ ] 内存隔离 100% 有效
- [ ] 任务隔离 100% 有效
- [ ] 资源限制强制执行
- [ ] MISRA-C:2012 零警告

---

<a name="2"></a>
## 2. 自适应分区

### 2.1 项目概述

| 属性 | 值 |
|------|-----|
| **优先级** | P1 |
| **工期** | 6周 |
| **价值** | 中 |
| **成本** | 中 |
| **风险** | 低 |
| **参考** | QNX Adaptive Partitioning |
| **MISRA** | 完全合规 |

### 2.2 核心概念

**时间窗口**: 100ms
```
窗口 0: [0ms - 100ms]
  - 分区 A: 30% CPU (30ms)
  - 分区 B: 50% CPU (50ms)
  - 分区 C: 20% CPU (20ms)

窗口 1: [100ms - 200ms]
  - 重置预算
  - 重新分配
```

### 2.3 数据结构

```c
/* src/include/partition.h */

#define MAX_PARTITIONS   8
#define PARTITION_WINDOW_MS  100U

typedef struct {
    uint32_t part_id;
    char name[32];

    /* CPU 预算 */
    uint32_t cpu_budget_percent;  /* 百分比 (0-100) */
    uint32_t cpu_budget_us;       /* 微秒 */
    uint32_t cpu_used_us;         /* 已使用 */
    bool exhausted;               /* 是否耗尽 */

    /* 内存预算 */
    uint64_t memory_budget_bytes;
    uint64_t memory_used_bytes;

    /* 任务列表 */
    struct list_head tasks;
    uint32_t task_count;

    /* 调度类 */
    const SchedClass_t *sched_class;

} Partition_t;

/* 分区调度器 */
typedef struct {
    Partition_t *partitions[MAX_PARTITIONS];
    uint32_t partition_count;

    /* 时间窗口 */
    uint32_t window_duration_ms;
    uint32_t window_elapsed_ms;
    uint64_t window_start_time;

    /* 当前活动分区 */
    Partition_t *current;

    spinlock_t lock;

} PartitionScheduler_t;
```

### 2.4 分区管理

```c
/* 创建分区 */
int partition_create(const char *name,
                    uint32_t cpu_budget_percent,
                    uint64_t memory_budget,
                    Partition_t **part_out) {
    Partition_t *part = (Partition_t *)kmalloc(sizeof(Partition_t));
    if (part == NULL) {
        return -ENOMEM;
    }

    /* 初始化 */
    strncpy(part->name, name, 32);
    part->cpu_budget_percent = cpu_budget_percent;
    part->cpu_budget_us = (cpu_budget_percent * PARTITION_WINDOW_MS) * 10;
    part->cpu_used_us = 0U;
    part->exhausted = false;
    part->memory_budget_bytes = memory_budget;
    part->memory_used_bytes = 0ULL;
    part->task_count = 0U;

    INIT_LIST_HEAD(&part->tasks);

    /* 默认调度类 */
    part->sched_class = &sched_class_cfs;

    *part_out = part;

    return 0;
}

/* 添加任务到分区 */
int partition_add_task(Partition_t *part, TCB_t *task) {
    if (part == NULL || task == NULL) {
        return -EINVAL;
    }

    /* 检查任务数限制 */
    if (part->task_count >= 64U) {
        return -EAGAIN;
    }

    /* 设置分区 */
    task->partition = part;

    /* 添加到列表 */
    list_add_tail(&task->part_list, &part->tasks);
    part->task_count++;

    return 0;
}

/* 重置预算（每个窗口调用）*/
void partition_reset_budgets(PartitionScheduler_t *ps) {
    for (uint32_t i = 0U; i < ps->partition_count; i++) {
        Partition_t *part = ps->partitions[i];

        part->cpu_used_us = 0U;
        part->exhausted = false;
    }

    /* 重置窗口计时器 */
    ps->window_elapsed_ms = 0U;
    ps->window_start_time = sched_clock();
}
```

### 2.5 与调度器集成

```c
/* 修改 pick_next_task() */
TCB_t *pick_next_task(struct rq *rq) {
    PartitionScheduler_t *ps = rq->partition_sched;
    Partition_t *best_part = NULL;
    TCB_t *best_task = NULL;

    spin_lock(&ps->lock);

    /* 遍历分区 */
    for (uint32_t i = 0U; i < ps->partition_count; i++) {
        Partition_t *part = ps->partitions[i];

        /* 检查预算 */
        if (part->exhausted) {
            continue;
        }

        if (part->cpu_used_us >= part->cpu_budget_us) {
            part->exhausted = true;
            continue;
        }

        /* 在分区内选择任务 */
        best_task = part->sched_class->pick_next(rq);
        if (best_task != NULL) {
            best_part = part;
            break;
        }
    }

    spin_unlock(&ps->lock);

    if (best_task == NULL) {
        /* 所有分区超预算，返回 idle */
        return rq->idle;
    }

    /* 更新分区统计 */
    spin_lock(&ps->lock);
    ps->current = best_part;
    spin_unlock(&ps->lock);

    return best_task;
}

/* 在 task_tick() 中更新使用量 */
void scheduler_tick(uint32_t cpu) {
    /* ... 现有代码 ... */

    PartitionScheduler_t *ps = cpu_rq(cpu)->partition_sched;
    if (ps != NULL && ps->current != NULL) {
        /* 增加 1ms */
        ps->current->cpu_used_us += 1000;

        /* 检查是否超预算 */
        if (ps->current->cpu_used_us >= ps->current->cpu_budget_us) {
            ps->current->exhausted = true;
        }

        /* 增加窗口计时器 */
        ps->window_elapsed_ms++;

        /* 检查窗口是否结束 */
        if (ps->window_elapsed_ms >= ps->window_duration_ms) {
            partition_reset_budgets(ps);
        }
    }
}
```

### 2.6 定时器配置

```c
/* 初始化分区调度器 */
int partition_scheduler_init(struct rq *rq) {
    PartitionScheduler_t *ps = (PartitionScheduler_t *)
        kmalloc(sizeof(PartitionScheduler_t));

    ps->partition_count = 0U;
    ps->window_duration_ms = PARTITION_WINDOW_MS;
    ps->window_elapsed_ms = 0U;
    ps->window_start_time = sched_clock();
    ps->current = NULL;

    spin_lock_init(&ps->lock);

    rq->partition_sched = ps;

    /* 注册窗口定时器 */
    timer_register(PARTITION_WINDOW_MS, partition_timer_callback, rq);

    return 0;
}

/* 定时器回调 */
void partition_timer_callback(void *data) {
    struct rq *rq = (struct rq *)data;
    PartitionScheduler_t *ps = rq->partition_sched;

    if (ps != NULL) {
        partition_reset_budgets(ps);
    }
}
```

### 2.7 使用示例

```c
/* 初始化系统分区 */
void init_partitions(void) {
    PartitionScheduler_t *ps = &global_partition_sched;

    /* 控制分区（30% CPU）*/
    Partition_t *control;
    partition_create("Control", 30, 4 * 1024 * 1024, &control);

    /* 显示分区（20% CPU）*/
    Partition_t *display;
    partition_create("Display", 20, 8 * 1024 * 1024, &display);

    /* 后台分区（50% CPU）*/
    Partition_t *background;
    partition_create("Background", 50, 16 * 1024 * 1024, &background);

    /* 注册到调度器 */
    ps->partitions[0] = control;
    ps->partitions[1] = display;
    ps->partitions[2] = background;
    ps->partition_count = 3;
}

/* 添加任务到分区 */
void add_control_task(TCB_t *task) {
    partition_add_task(ps->partitions[0], task);
}
```

### 2.8 验收标准

- [ ] 8 个分区支持
- [ ] CPU 预算强制执行
- [ ] 时间窗口准确度 ±1ms
- [ ] 内存预算强制执行
- [ ] MISRA-C:2012 零警告

---

<a name="3"></a>
## 3. AISafe-eBPF

### 3.1 项目概述

| 属性 | 值 |
|------|-----|
| **优先级** | P1 |
| **工期** | 10周 |
| **价值** | 中 |
| **成本** | 高 |
| **风险** | 中 |
| **参考** | Linux eBPF |
| **MISRA** | 完全合规 |

### 3.2 设计原则

**简化策略**:
- **指令子集**: 64 条指令（Linux eBPF 的 50%）
- **限制功能**: 仅支持必要操作
- **静态验证**: 严格验证器
- **JIT 可选**: 解释器优先，JIT 后续

### 3.3 指令集定义

```c
/* src/kernel/asebpf.h */

/* 操作码 */
#define AEBPF_OP_ADD    0x00
#define AEBPF_OP_SUB    0x10
#define AEBPF_OP_MUL    0x20
#define AEBPF_OP_DIV    0x30
#define AEBPF_OP_AND    0x40
#define AEBPF_OP_OR     0x50
#define AEBPF_OP_LSH    0x60
#define AEBPF_OP_RSH    0x70
#define AEBPF_OP_NEG    0x80
#define AEBPF_OP_MOD    0x90
#define AEBPF_OP_XOR    0xA0
#define AEBPF_OP_MOV    0xB0
#define AEBPF_OP_ARSH   0xC0

#define AEBPF_OP_LE     0x0D
#define AEBPF_OP_BE     0x0E

#define AEBPF_OP_JA     0x00
#define AEBPF_OP_JEQ    0x10
#define AEBPF_OP_JGT    0x20
#define AEBPF_OP_JGE    0x30
#define AEBPF_OP_JSET   0x40
#define AEBPF_OP_JNE    0x50
#define AEBPF_OP_JSGT   0x60
#define AEBPF_OP_JSGE   0x70
#define AEBPF_OP_CALL   0x80
#define AEBPF_OP_EXIT   0x90

#define AEBPF_OP_LDAB   0x10
#define AEBPF_OP_LDAH   0x20
#define AEBPF_OP_LDAW   0x40
#define AEBPF_OP_LDD    0x60
#define AEBPF_OP_LDXB   0x70
#define AEBPF_OP_LDXH   0x80
#define AEBPF_OP_LDXW   0xA0
#define AEBPF_OP_STB    0x30
#define AEBPF_OP_STH    0x40
#define AEBPF_OP_STW    0x60
#define AEBPF_OP_STXW   0xA0

/* 指令结构（64位）*/
typedef struct {
    uint8_t  opcode;   /* 操作码 */
    uint8_t  dst_reg;  /* 目标寄存器 (0-15) */
    uint8_t  src_reg;  /* 源寄存器 (0-15) */
    uint8_t  offset;   /* 偏移（字节）*/
    int32_t  imm;      /* 立即数 */
} __attribute__((packed)) AEBPF_Inst_t;
```

### 3.4 虚拟机状态

```c
/* 虚拟机 */
typedef struct {
    /* 寄存器（R0-R10）*/
    uint64_t regs[11];

    /* 栈 */
    uint64_t *stack;
    uint32_t stack_size;

    /* 程序计数器 */
    uint32_t pc;

    /* 上下文 */
    const void *ctx;
    uint64_t ctx_size;

} AEBPF_VM_t;
```

### 3.5 解释器实现

```c
/* src/kernel/asebpf/interpreter.c */

int aebpf_execute(const AEBPF_Inst_t *prog,
                  uint32_t prog_len,
                  const void *ctx,
                  uint64_t ctx_size,
                  uint64_t *result) {
    AEBPF_VM_t vm;
    int ret;

    /* 1. 初始化虚拟机 */
    (void)memset(&vm, 0, sizeof(vm));
    vm.pc = 0U;
    vm.ctx = ctx;
    vm.ctx_size = ctx_size;

    /* R10 = 栈底 */
    vm.regs[10] = (uint64_t)vm.stack + 512U;

    /* 2. 执行循环 */
    while (vm.pc < prog_len) {
        const AEBPF_Inst_t *inst = &prog[vm.pc];

        ret = execute_instruction(&vm, inst);
        if (ret != 0) {
            return ret;
        }

        vm.pc++;

        /* 检查退出 */
        if (inst->opcode == AEBPF_OP_EXIT) {
            break;
        }
    }

    /* 3. 返回结果 */
    if (result != NULL) {
        *result = vm.regs[0];
    }

    return 0;
}

/* 执行单条指令 */
static int execute_instruction(AEBPF_VM_t *vm,
                               const AEBPF_Inst_t *inst) {
    uint8_t opcode = inst->opcode;
    uint8_t dst = inst->dst_reg;
    uint8_t src = inst->src_reg;
    int32_t imm = inst->imm;
    uint64_t *regs = vm->regs;

    /* ALU 操作 */
    if ((opcode & 0xF0) == 0x00) {
        uint64_t *dst_ptr = &regs[dst];
        uint64_t src_val = (opcode & 0x08) ? regs[src] : (uint64_t)imm;

        switch (opcode & 0xF0) {
            case AEBPF_OP_ADD:
                *dst_ptr += src_val;
                break;
            case AEBPF_OP_SUB:
                *dst_ptr -= src_val;
                break;
            case AEBPF_OP_MUL:
                *dst_ptr *= src_val;
                break;
            case AEBPF_OP_DIV:
                if (src_val == 0ULL) return -EINVAL;
                *dst_ptr /= src_val;
                break;
            case AEBPF_OP_AND:
                *dst_ptr &= src_val;
                break;
            case AEBPF_OP_OR:
                *dst_ptr |= src_val;
                break;
            case AEBPF_OP_LSH:
                *dst_ptr <<= src_val;
                break;
            case AEBPF_OP_RSH:
                *dst_ptr >>= src_val;
                break;
            /* ... */
        }
    }
    /* 分支操作 */
    else if ((opcode & 0xF0) == 0x00) {
        uint64_t dst_val = regs[dst];
        uint64_t src_val = (opcode & 0x08) ? regs[src] : (uint64_t)imm;
        int32_t offset = inst->offset;

        bool condition = false;
        switch (opcode & 0xF0) {
            case AEBPF_OP_JA:
                condition = true;
                break;
            case AEBPF_OP_JEQ:
                condition = (dst_val == src_val);
                break;
            case AEBPF_OP_JGT:
                condition = (dst_val > src_val);
                break;
            case AEBPF_OP_JGE:
                condition = (dst_val >= src_val);
                break;
            /* ... */
        }

        if (condition) {
            vm->pc += offset;
        }
    }
    /* 内存操作 */
    else if ((opcode & 0xF0) == 0x00) {
        /* 加载/存储 */
        /* ... */
    }

    return 0;
}
```

### 3.6 验证器

```c
/* src/kernel/asebpf/verifier.c */

/* 寄存器状态 */
typedef struct {
    bool is_pointer;      /* 是否指针 */
    uint64_t ptr_base;    /* 指针基址 */
    uint64_t ptr_size;    /* 指针大小 */
    bool has_terminator;  /* 是否有终止符 */
} RegState_t;

/* 验证程序 */
int aebpf_verify_program(const AEBPF_Inst_t *prog,
                         uint32_t prog_len) {
    RegState_t reg_states[prog_len][16];
    uint32_t pc;

    /* 初始化 */
    for (pc = 0U; pc < prog_len; pc++) {
        for (uint32_t i = 0U; i < 16U; i++) {
            reg_states[pc][i].is_pointer = false;
            reg_states[pc][i].has_terminator = false;
        }
    }

    /* 前向数据流分析 */
    for (pc = 0U; pc < prog_len; pc++) {
        const AEBPF_Inst_t *inst = &prog[pc];
        int ret;

        /* 1. 操作码检查 */
        if (inst->opcode >= 0xD0) {
            return -EINVAL;  /* 无效操作码 */
        }

        /* 2. 寄存器范围检查 */
        if (inst->dst_reg >= 16U || inst->src_reg >= 16U) {
            return -EINVAL;
        }

        /* 3. 内存访问检查 */
        if (is_memory_op(inst->opcode)) {
            RegState_t *state = &reg_states[pc][inst->dst_reg];

            if (!state->is_pointer) {
                return -EINVAL;  /* 非指针不能解引用 */
            }

            if (inst->offset >= state->ptr_size) {
                return -EINVAL;  /* 越界访问 */
            }
        }

        /* 4. 终止性检查 */
        if (is_terminator(inst->opcode)) {
            reg_states[pc][inst->dst_reg].has_terminator = true;
        }

        /* 5. 传播寄存器状态 */
        propagate_state(reg_states[pc], reg_states[pc + 1U]);
    }

    /* 检查终止符 */
    if (!reg_states[prog_len - 1U][0U].has_terminator) {
        return -EINVAL;  /* 程序必须以 EXIT 结尾 */
    }

    return 0;
}
```

### 3.7 钩子集成

```c
/* 钩子点 */
typedef enum {
    AEBPF_HOOK_NET_RECV,
    AEBPF_HOOK_NET_SEND,
    AEBPF_HOOK_SYSCALL_ENTRY,
    AEBPF_HOOK_SYSCALL_EXIT,
    AEBPF_HOOK_MAX
} AEBPFHookType_t;

/* 附加程序 */
int aebpf_attach_hook(AEBPFHookType_t type,
                     const AEBPF_Inst_t *prog,
                     uint32_t prog_len) {
    /* 1. 验证程序 */
    int ret = aebpf_verify_program(prog, prog_len);
    if (ret != 0) {
        return ret;
    }

    /* 2. 存储程序 */
    AEBPF_Program_t *bp = (AEBPF_Program_t *)
        kmalloc(sizeof(AEBPF_Program_t));

    bp->insts = prog;
    bp->inst_count = prog_len;
    bp->verified = true;

    /* 3. 附加到钩子 */
    list_add_tail(&bp->list, &aebpf_hooks[type]);

    return 0;
}

/* 调用钩子 */
int aebpf_call_hooks(AEBPFHookType_t type,
                    const void *ctx,
                    uint64_t ctx_size) {
    AEBPF_Program_t *prog;
    uint64_t result;

    list_for_each_entry(prog, &aebpf_hooks[type], list) {
        int ret = aebpf_execute(prog->insts, prog->inst_count,
                               ctx, ctx_size, &result);

        if (ret != 0) {
            return ret;
        }

        if (result == 0ULL) {
            /* 拒绝 */
            return -EPERM;
        }
    }

    return 0;
}
```

### 3.8 使用示例

```c
/* 网络包过滤器 */
static const AEBPF_Inst_t filter_prog[] = {
    /* 加载包长度 */
    { .opcode = AEBPF_OP_LDXH | 0x08, .dst_reg = 1,
      .src_reg = 1, .offset = 0, .imm = 0 },

    /* 如果长度 < 64，拒绝 */
    { .opcode = AEBPF_OP_JLT, .dst_reg = 1, .src_reg = 0,
      .offset = 2, .imm = 64 },

    /* 接受 */
    { .opcode = AEBPF_OP_MOV | 0x08, .dst_reg = 0,
      .src_reg = 0, .offset = 0, .imm = 1 },
    { .opcode = AEBPF_OP_EXIT },

    /* 拒绝 */
    { .opcode = AEBPF_OP_MOV | 0x08, .dst_reg = 0,
      .src_reg = 0, .offset = 0, .imm = 0 },
    { .opcode = AEBPF_OP_EXIT },
};

/* 加载并附加 */
void init_packet_filter(void) {
    int ret = aebpf_attach_hook(AEBPF_HOOK_NET_RECV,
                               filter_prog,
                               7);
    if (ret != 0) {
        printk("Failed to load filter: %d\n", ret);
    }
}
```

### 3.9 验收标准

- [ ] 64 条指令支持
- [ ] 验证器覆盖率 100%
- [ ] 性能开销 < 5%
- [ ] MISRA-C:2012 零警告
- [ ] 代码覆盖率 > 95%

---

由于文档长度限制，我将继续在下一个文件中完成剩余项目（驱动框架和形式化验证）的详细实施方案。

---

**文档版本**: 1.0
**最后更新**: 2025-01-08
**作者**: AISafe64 Team
