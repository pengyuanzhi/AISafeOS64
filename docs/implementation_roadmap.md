# AISafe64 优先级 P0 项目详细实施方案

## 文档信息
- **版本**: 1.0
- **日期**: 2025-01-08
- **作者**: AISafe64 Team

---

## 目录
1. [栈溢出保护](#1-栈溢出保护)
2. [MPU/MMU 抽象层](#2-mpummu-抽象层)
3. [安全钩子框架](#3-安全钩子框架)
4. [Capability 系统](#4-capability-系统)
5. [Fast IPC](#5-fast-ipc)

---

<a name="1"></a>
## 1. 栈溢出保护

### 1.1 项目概述

| 属性 | 值 |
|------|-----|
| **优先级** | P0 |
| **工期** | 2周 |
| **价值** | 高 |
| **成本** | 低 |
| **风险** | 低 |
| **参考** | Zephyr RTOS, NuttX |
| **MISRA** | 完全合规 |

### 1.2 技术设计

#### 栈布局
```
高地址 (0xFFFFFFFF...)
+------------------+
|  Guard Pattern   | 16 bytes (4 x 32-bit)
|  0xFEE1DEAD x 4  |
+------------------+
|  Available Stack | N bytes
|  (grows down)    |
+------------------+
|  Canary Value    | 4 bytes
|  0xDEADBEEF      |
+------------------+
低地址 (0x00000000...)
```

#### 保护机制（3层）
1. **编译时**: 金丝雀值插入
2. **运行时**: 上下文切换检查
3. **硬件**: MPU/MMU 保护页

### 1.3 实施步骤

#### Week 1: 设计与实现

**Day 1-2: 数据结构**
```c
/* src/include/stack_protection.h */
#define STACK_CANARY_VALUE     0xDEADBEEFU
#define STACK_GUARD_PATTERN    0xFEE1DEADU
#define STACK_GUARD_COUNT      4U

typedef struct StackProtectionConfig {
    uint32_t canary;
    uint32_t guard_pattern[STACK_GUARD_COUNT];
    bool use_mpu;
    uint32_t guard_page_size;
    uint64_t max_usage;
    uint64_t high_watermark;
    bool enable_canary_check;
    bool enable_guard_check;
    bool enable_mpu_check;
} StackProtectionConfig_t;

typedef struct StackFrame {
    uint32_t canary;
    uint8_t  stack[];
} StackFrame_t;

/* API */
int stack_protection_init(TCB_t *task, uint32_t size);
bool stack_protection_check(const TCB_t *task);
uint32_t stack_usage_percent(const TCB_t *task);
void stack_protection_update_stats(TCB_t *task);
int stack_protection_configure_mpu(const TCB_t *task);
```

**Day 3-7: 核心实现**
```c
/* src/kernel/stack_protection.c */

static StackProtectionConfig_t global_config = {
    .canary = STACK_CANARY_VALUE,
    .guard_pattern = {
        STACK_GUARD_PATTERN,
        STACK_GUARD_PATTERN,
        STACK_GUARD_PATTERN,
        STACK_GUARD_PATTERN
    },
    .use_mpu = false,
    .guard_page_size = 4096U,
    .enable_canary_check = true,
    .enable_guard_check = true,
    .enable_mpu_check = false
};

int stack_protection_init(TCB_t *task, uint32_t size) {
    /* 1. 参数验证 */
    if (task == NULL || size == 0U) {
        return -EINVAL;
    }

    /* 2. 分配栈 */
    task->stack_base = (uint64_t)malloc(size);
    if (task->stack_base == 0ULL) {
        return -ENOMEM;
    }
    task->stack_size = size;
    task->stack_ptr = task->stack_base + size;

    /* 3. 设置金丝雀 */
    if (global_config.enable_canary_check) {
        uint32_t *canary_ptr = (uint32_t *)task->stack_base;
        *canary_ptr = global_config.canary;
    }

    /* 4. 设置边界模式 */
    if (global_config.enable_guard_check) {
        uint32_t *guard_ptr = (uint32_t *)(task->stack_base + size - 16U);
        for (uint32_t i = 0U; i < 4U; i++) {
            guard_ptr[i] = global_config.guard_pattern[i];
        }
    }

    /* 5. 配置 MPU（可选）*/
    if (global_config.use_mpu) {
        stack_protection_configure_mpu(task);
    }

    /* 6. 初始化统计 */
    task->stack_max_usage = 0ULL;
    task->stack_high_watermark = 0ULL;

    return 0;
}

bool stack_protection_check(const TCB_t *task) {
    if (task == NULL) {
        return false;
    }

    /* 检查金丝雀 */
    if (global_config.enable_canary_check) {
        const uint32_t *canary_ptr = (const uint32_t *)task->stack_base;
        if (*canary_ptr != global_config.canary) {
            printk("Stack overflow: Task %u (%s)\n",
                   task->tid, task->name);
            return false;
        }
    }

    /* 检查边界模式 */
    if (global_config.enable_guard_check) {
        const uint32_t *guard_ptr = (const uint32_t *)
            (task->stack_base + task->stack_size - 16U);
        for (uint32_t i = 0U; i < 4U; i++) {
            if (guard_ptr[i] != global_config.guard_pattern[i]) {
                printk("Stack overflow: Task %u guard corrupted\n",
                       task->tid);
                return false;
            }
        }
    }

    /* 检查栈指针范围 */
    if (task->stack_ptr < task->stack_base ||
        task->stack_ptr > (task->stack_base + task->stack_size)) {
        printk("Stack underflow: Task %u\n", task->tid);
        return false;
    }

    return true;
}

uint32_t stack_usage_percent(const TCB_t *task) {
    if (task == NULL) {
        return 0U;
    }

    const uint8_t *ptr = (const uint8_t *)task->stack_base + 4U;
    const uint8_t *stack_ptr = (const uint8_t *)task->stack_ptr;
    uint32_t unused = 0U;

    while (ptr < stack_ptr && *ptr == 0x00) {
        unused++;
        ptr++;
    }

    uint64_t used = task->stack_size - unused;
    return (uint32_t)((used * 100ULL) / task->stack_size);
}

void stack_protection_update_stats(TCB_t *task) {
    if (task == NULL) {
        return;
    }

    uint64_t current_usage = task->stack_ptr - task->stack_base;

    if (current_usage > task->stack_max_usage) {
        task->stack_max_usage = current_usage;
    }

    uint32_t percent = stack_usage_percent(task);
    if (percent > task->stack_high_watermark) {
        task->stack_high_watermark = percent;
    }

    if (percent > 80U) {
        printk("Warning: Task %u stack at %u%%\n",
               task->tid, percent);
    }
}

int stack_protection_configure_mpu(const TCB_t *task) {
#ifdef CONFIG_ARM_MPU
    const struct mpu_ops *ops = mpu_get_ops();

    /* 栈区域（RW）*/
    ops->configure_region(task->mpu_region_stack,
                         task->stack_base,
                         task->stack_size,
                         MPU_AP_RW);

    /* 保护页（无访问）*/
    ops->configure_region(task->mpu_region_guard,
                         task->stack_base + task->stack_size,
                         4096U,
                         MPU_AP_NONE);

    return 0;
#else
    (void)task;
    return -ENOTSUP;
#endif
}
```

**Day 8-10: 集成到调度器**
```c
/* src/kernel/scheduler.c - 修改 */

void context_switch(TCB_t *prev, TCB_t *next) {
    /* 栈检查 */
    if (!stack_protection_check(prev)) {
        system_panic("Stack overflow detected", prev->tid);
    }

    /* 更新统计 */
    stack_protection_update_stats(prev);

    /* 保存/恢复上下文 */
    save_context(prev);
    restore_context(next);
}
```

#### Week 2: 测试与文档

**Day 11-14: 单元测试**
```c
/* tests/test_stack_protection.c */

void test_stack_init_normal(void) {
    TCB_t task;
    int ret = stack_protection_init(&task, 8192U);

    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NOT_EQUAL(0ULL, task.stack_base);
    TEST_ASSERT_EQUAL(8192U, task.stack_size);
}

void test_canary_detection(void) {
    TCB_t task;
    stack_protection_init(&task, 4096U);

    /* 篡改金丝雀 */
    uint32_t *canary = (uint32_t *)task.stack_base;
    *canary = 0xBADBADBA;

    /* 应该检测到 */
    bool result = stack_protection_check(&task);
    TEST_ASSERT_FALSE(result);
}

void test_guard_detection(void) {
    TCB_t task;
    stack_protection_init(&task, 4096U);

    /* 篡改边界 */
    uint32_t *guard = (uint32_t *)(task.stack_base + 4096U - 16U);
    guard[0] = 0xBADBADBA;

    /* 应该检测到 */
    bool result = stack_protection_check(&task);
    TEST_ASSERT_FALSE(result);
}

void test_overflow_simulation(void) {
    TCB_t task;
    stack_protection_init(&task, 4096U);

    /* 模拟溢出 */
    volatile uint8_t *ptr =
        (volatile uint8_t *)(task.stack_base + 4200U);
    *ptr = 0xAA;

    /* 应该检测到 */
    bool result = stack_protection_check(&task);
    TEST_ASSERT_FALSE(result);
}

void test_usage_calculation(void) {
    TCB_t task;
    stack_protection_init(&task, 4096U);

    /* 使用 1024 字节 */
    task.stack_ptr = task.stack_base + 1024U;

    uint32_t usage = stack_usage_percent(&task);
    TEST_ASSERT_EQUAL(25U, usage);  /* 1024/4096 = 25% */
}

void test_statistics(void) {
    TCB_t task;
    stack_protection_init(&task, 4096U);

    task.stack_ptr = task.stack_base + 2048U;
    stack_protection_update_stats(&task);

    TEST_ASSERT_EQUAL(2048ULL, task.stack_max_usage);
    TEST_ASSERT_EQUAL(50U, task.stack_high_watermark);
}
```

**Day 15: MISRA 检查与文档**
```bash
# PC-lint 检查
pc-lint -os("lint_stack.txt") src/kernel/stack_protection.c

# 零警告要求
grep "Warning" lint_stack.txt | wc -l  # 应该是 0
```

### 1.4 验收标准

- [ ] 10/10 单元测试通过
- [ ] 栈溢出检测率 100%
- [ ] 误报率 0%（单元测试）
- [ ] 性能开销 < 5%
- [ ] MISRA-C:2012 零警告
- [ ] 代码覆盖率 > 95%

### 1.5 性能基准

| 操作 | 开销 | 目标 |
|------|------|------|
| 初始化 | ~1μs | <2μs |
| 检查 | ~50ns | <100ns |
| 统计更新 | ~30ns | <50ns |
| 上下文切换增加 | ~50ns | <100ns |

### 1.6 风险与缓解

| 风险 | 缓解措施 |
|------|----------|
| 性能下降 | 内联关键路径函数 |
| 误报 | 可调整敏感度配置 |
| MPU 不支持 | 回退到软件保护 |

---

<a name="2"></a>
## 2. MPU/MMU 抽象层

### 2.1 项目概述

| 属性 | 值 |
|------|-----|
| **优先级** | P0 |
| **工期** | 3周 |
| **价值** | 高 |
| **成本** | 低 |
| **风险** | 低 |
| **参考** | Zephyr, NuttX |
| **MISRA** | 完全合规 |

### 2.2 架构设计

```
+---------------------------+
|  应用层（任务、驱动）      |
|  - 使用抽象 API            |
+---------------------------+
           |
           v
+---------------------------+
|  MPU/MMU 抽象层           |
|  - 统一接口                |
|  - 运行时选择              |
+---------------------------+
      |             |
      v             v
+----------+   +----------+
| ARMv8-M  |   | ARMv8-A  |
| MPU 实现  |   | MMU 实现  |
+----------+   +----------+
```

### 2.3 接口定义

```c
/* src/include/mem_protection.h */

/* 权限标志 */
#define MEM_PERM_READ    (1U << 0)
#define MEM_PERM_WRITE   (1U << 1)
#define MEM_PERM_EXEC    (1U << 2)
#define MEM_PERM_PRIV    (1U << 3)

/* 内存属性 */
#define MEM_ATTR_DEVICE  (0x0U << 2)
#define MEM_ATTR_NORMAL  (0x1U << 2)

/* 内存区域 */
typedef struct {
    uint64_t base;
    uint64_t size;
    uint32_t flags;
    uint32_t attributes;
    uint32_t region_id;
    bool active;
} MemRegion_t;

/* 保护域 */
typedef struct {
    uint32_t pd_id;
    char name[32];
    MemRegion_t regions[8];
    uint32_t region_count;
    uint64_t *page_table;
} ProtectionDomain_t;

/* 操作接口 */
typedef struct MemProtectionOps {
    int (*configure_region)(const MemRegion_t *region);
    int (*remove_region)(uint32_t region_id);
    int (*context_switch)(const TCB_t *next);
    int (*enable)(void);
    int (*disable)(void);
    int (*get_region_info)(uint32_t region_id, MemRegion_t *info);
} MemProtectionOps_t;

/* 全局 API */
int mem_protection_init(void);
const MemProtectionOps_t *mem_protection_get_ops(void);

static inline int mp_configure_region(const MemRegion_t *region) {
    const MemProtectionOps_t *ops = mem_protection_get_ops();
    if (ops == NULL) return -ENOTSUP;
    return ops->configure_region(region);
}

static inline int mp_context_switch(const TCB_t *next) {
    const MemProtectionOps_t *ops = mem_protection_get_ops();
    if (ops == NULL || ops->context_switch == NULL) return 0;
    return ops->context_switch(next);
}
```

### 2.4 MPU 实现（ARMv8-M）

```c
/* src/hal/mpu_v8m.c */

/* ARMv8-M MPU 寄存器 */
#define MPU_TYPE    (*(volatile uint32_t *)(0xE000ED90))
#define MPU_CTRL    (*(volatile uint32_t *)(0xE000ED94))
#define MPU_RNR     (*(volatile uint32_t *)(0xE000ED98))
#define MPU_RBAR    (*(volatile uint32_t *)(0xE000ED9C))
#define MPU_RLAR    (*(volatile uint32_t *)(0xE000EDA0))

static int mpu_configure_region(const MemRegion_t *region) {
    /* 验证 */
    if (region == NULL) return -EINVAL;

    /* 检查对齐 */
    if ((region->base & 0x1FUL) != 0U) return -EINVAL;

    /* 检查大小（2的幂）*/
    uint64_t size = region->size;
    if ((size & (size - 1ULL)) != 0ULL) return -EINVAL;

    /* 计算大小编码 */
    uint32_t size_code = 0U;
    uint64_t temp = size >> 4U;
    while (temp != 0ULL) {
        temp >>= 1U;
        size_code++;
    }

    /* 权限转换 */
    uint32_t ap = 0U;
    if (region->flags & MEM_PERM_WRITE) {
        ap = (region->flags & MEM_PERM_PRIV) ? 0x0U : 0x3U;
    } else if (region->flags & MEM_PERM_READ) {
        ap = (region->flags & MEM_PERM_PRIV) ? 0x1U : 0x2U;
    }

    /* 执行位 */
    uint32_t xn = (region->flags & MEM_PERM_EXEC) ? 0U : 1U;

    /* 配置 */
    MPU_RNR = region->region_id;
    MPU_RBAR = (region->base & 0xFFFFFF00UL) | (1U << 0);
    MPU_RLAR = ((region->base + size - 1ULL) & 0xFFFFFF00UL) |
               (xn << 1U) | (ap << 3U) | (3U << 5U) | (1U << 0);

    return 0;
}

static int mpu_context_switch(const TCB_t *next) {
    for (uint32_t i = 0U; i < next->region_count; i++) {
        int ret = mpu_configure_region(&next->regions[i]);
        if (ret != 0) return ret;
    }
    return 0;
}

static int mpu_enable(void) {
    MPU_CTRL |= (1U << 0);
    __asm__ volatile("dsb");
    __asm__ volatile("isb");
    return 0;
}

static const MemProtectionOps_t mpu_ops = {
    .configure_region = mpu_configure_region,
    .context_switch = mpu_context_switch,
    .enable = mpu_enable
};
```

### 2.5 MMU 实现（ARMv8-A）

```c
/* src/hal/mmu_v8a.c */

/* 页表项标志 */
#define PTE_AF        (1UL << 10)
#define PTE_SH_INNER  (3UL << 8)
#define PTE_AP_RW     (0UL << 6)
#define PTE_AP_RO     (2UL << 6)
#define PTE_UXN       (1UL << 54)
#define PTE_PXN       (1UL << 53)
#define PTE_TYPE_PAGE (3UL)
#define PTE_TYPE_TABLE (3UL)

static uint64_t create_pte(uint32_t flags, uint32_t attrs) {
    uint64_t pte = PTE_AF | PTE_SH_INNER;

    /* AP 位 */
    if (flags & MEM_PERM_WRITE) {
        pte |= PTE_AP_RW;
    } else {
        pte |= PTE_AP_RO;
    }

    /* 执行位 */
    if (!(flags & MEM_PERM_EXEC)) {
        pte |= PTE_UXN | PTE_PXN;
    }

    return pte;
}

static int map_page(uint64_t *pgd, uint64_t virt, uint64_t phys,
                   uint64_t size, uint32_t flags, uint32_t attrs) {
    uint64_t pte_attr = create_pte(flags, attrs);

    for (uint64_t offset = 0ULL; offset < size; offset += 4096ULL) {
        uint64_t idx0 = (virt >> 39) & 0x1FF;
        uint64_t idx1 = (virt >> 30) & 0x1FF;
        uint64_t idx2 = (virt >> 21) & 0x1FF;
        uint64_t idx3 = (virt >> 12) & 0x1FF;

        /* 4 级页表遍历和创建 */
        /* ... (详细实现见上文）... */

        virt += 4096ULL;
    }

    return 0;
}

static int mmu_context_switch(const TCB_t *next) {
    uint64_t ttbr0 = next->page_table;

    __asm__ volatile("msr TTBR0_EL1, %0" :: "r"(ttbr0));
    __asm__ volatile("tlbi aside1is, %0" :: "r"(next->tid));
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");

    return 0;
}

static const MemProtectionOps_t mmu_ops = {
    .context_switch = mmu_context_switch,
    /* ... */
};
```

### 2.6 集成到 TCB

```c
/* src/include/task.h - 修改 */

typedef struct TCB_t {
    /* ... 现有字段 ... */

    /* 内存保护 */
    struct {
        MemRegion_t regions[8];
        uint32_t region_count;
        uint64_t page_table;
    } mem_protection;

} TCB_t;
```

### 2.7 集成到调度器

```c
/* src/kernel/scheduler.c - 修改 */

void context_switch(TCB_t *prev, TCB_t *next) {
    /* 切换内存保护 */
    int ret = mp_context_switch(next);
    if (ret != 0) {
        printk("Warning: MP switch failed: %d\n", ret);
    }

    /* 上下文切换 */
    save_context(prev);
    restore_context(next);
}
```

### 2.8 测试

```c
/* tests/test_mem_protection.c */

void test_mpu_configure(void) {
    MemRegion_t region = {
        .base = 0x20000000,
        .size = 0x1000,
        .flags = MEM_PERM_READ | MEM_PERM_WRITE,
        .region_id = 0
    };

    int ret = mp_configure_region(&region);
    TEST_ASSERT_EQUAL(0, ret);
}

void test_mpu_violation(void) {
    MemRegion_t region = {
        .base = 0x30000000,
        .size = 0x1000,
        .flags = MEM_PERM_READ,  /* 只读 */
        .region_id = 1
    };

    mp_configure_region(&region);

    volatile uint32_t *ptr = (volatile uint32_t *)region.base;
    TEST_ASSERT_EXCEPTION(*ptr = 0xDEADBEEF);  /* 应该触发异常 */
}
```

### 2.9 验收标准

- [ ] MPU 实现通过测试
- [ ] MMU 实现通过测试
- [ ] 上下文切换开销增加 < 10%
- [ ] 内存隔离有效性 100%
- [ ] MISRA-C:2012 零警告
- [ ] 支持 ARMv8-M 和 ARMv8-A

---

<a name="3"></a>
## 3. 安全钩子框架

### 3.1 项目概述

| 属性 | 值 |
|------|-----|
| **优先级** | P0 |
| **工期** | 2周 |
| **价值** | 高 |
| **成本** | 低 |
| **风险** | 低 |
| **参考** | Linux LSM |
| **MISRA** | 完全合规 |

### 3.2 钩子类型定义

```c
/* src/include/security_hooks.h */

typedef enum {
    /* 任务管理 */
    HOOK_TASK_CREATE = 0,
    HOOK_TASK_EXIT,
    HOOK_TASK_YIELD,

    /* 内存管理 */
    HOOK_MEM_ALLOC,
    HOOK_MEM_FREE,

    /* IPC */
    HOOK_IPC_SEND,
    HOOK_IPC_RECV,

    /* 设备访问 */
    HOOK_DEV_OPEN,
    HOOK_DEV_CLOSE,
    HOOK_DEV_IOCTL,

    /* 系统调用 */
    HOOK_SYSCALL_ENTRY,
    HOOK_SYSCALL_EXIT,

    HOOK_MAX
} SecurityHookType_t;
```

### 3.3 钩子函数签名

```c
/* 上下文结构 */
typedef struct TaskCreateCtx {
    uint32_t tid;
    uint8_t prio;
    uint32_t stack_size;
    void (*entry)(void);
} TaskCreateCtx_t;

typedef struct MemAllocCtx {
    uint64_t size;
    uint32_t flags;
    void *ptr;
} MemAllocCtx_t;

typedef struct IPCTx {
    uint32_t src_tid;
    uint32_t dst_tid;
    void *data;
    size_t len;
} IPCTx_t;

/* 钩子函数类型 */
typedef int (*security_hook_fn)(void *ctx);
```

### 3.4 框架实现

```c
/* src/kernel/security_hooks.c */

#define MAX_HOOKS_PER_TYPE  8

typedef struct {
    security_hook_fn hooks[MAX_HOOKS_PER_TYPE];
    uint32_t count;
} SecurityHookList_t;

static SecurityHookList_t hook_lists[HOOK_MAX];

/* 注册钩子 */
int security_hook_register(SecurityHookType_t type,
                          security_hook_fn fn,
                          const char *name) {
    if (type >= HOOK_MAX) {
        return -EINVAL;
    }

    if (fn == NULL) {
        return -EINVAL;
    }

    SecurityHookList_t *list = &hook_lists[type];

    if (list->count >= MAX_HOOKS_PER_TYPE) {
        return -ENOSPC;
    }

    list->hooks[list->count++] = fn;

    printk("Registered hook '%s' for type %u\n", name, type);

    return 0;
}

/* 注销钩子 */
int security_hook_unregister(SecurityHookType_t type,
                            security_hook_fn fn) {
    if (type >= HOOK_MAX) {
        return -EINVAL;
    }

    SecurityHookList_t *list = &hook_lists[type];

    for (uint32_t i = 0U; i < list->count; i++) {
        if (list->hooks[i] == fn) {
            /* 移动后续钩子 */
            for (uint32_t j = i; j < list->count - 1U; j++) {
                list->hooks[j] = list->hooks[j + 1U];
            }
            list->count--;
            return 0;
        }
    }

    return -ENOENT;
}

/* 调用钩子（内联优化）*/
static inline int call_security_hooks(SecurityHookType_t type,
                                      void *ctx) {
    SecurityHookList_t *list = &hook_lists[type];
    int ret = 0;

    for (uint32_t i = 0U; i < list->count; i++) {
        ret = list->hooks[i](ctx);
        if (ret != 0) {
            /* 任何钩子拒绝则停止 */
            return ret;
        }
    }

    return ret;
}
```

### 3.5 内置安全模块

```c
/* src/kernel/security/capability_check.c */

static int task_create_hook(void *ctx) {
    TaskCreateCtx_t *tcc = (TaskCreateCtx_t *)ctx;

    /* 检查创建权限 */
    if (!cap_has_capability(current, CAP_CREATE_TASK)) {
        printk("Permission denied: Create task\n");
        return -EPERM;
    }

    /* 检查资源限制 */
    if (current->protection_domain->task_count >=
        current->protection_domain->max_tasks) {
        printk("Resource limit exceeded\n");
        return -EAGAIN;
    }

    return 0;
}

static int mem_alloc_hook(void *ctx) {
    MemAllocCtx_t *mac = (MemAllocCtx_t *)ctx;

    /* 检查配额 */
    if (current->memory_used + mac->size >
        current->protection_domain->max_memory) {
        return -ENOMEM;
    }

    return 0;
}

/* 模块初始化 */
static int __init capability_check_init(void) {
    security_hook_register(HOOK_TASK_CREATE, task_create_hook,
                          "capability_check");
    security_hook_register(HOOK_TASK_EXIT, task_exit_hook,
                          "capability_check");
    security_hook_register(HOOK_MEM_ALLOC, mem_alloc_hook,
                          "capability_check");
    security_hook_register(HOOK_IPC_SEND, ipc_send_hook,
                          "capability_check");

    return 0;
}

module_init(capability_check_init);
```

### 3.6 使用示例

```c
/* 在 task_create() 中 */
uint32_t task_create(const char *name, uint8_t prio,
                    uint32_t stack_size, void (*entry)(void)) {
    /* 准备上下文 */
    TaskCreateCtx_t ctx = {
        .tid = 0,
        .prio = prio,
        .stack_size = stack_size,
        .entry = entry
    };

    /* 调用安全钩子 */
    int ret = call_security_hooks(HOOK_TASK_CREATE, &ctx);
    if (ret != 0) {
        return ret;  /* 安全检查失败 */
    }

    /* 继续创建任务... */
}

/* 在 kmalloc() 中 */
void *kmalloc(uint64_t size, uint32_t flags) {
    /* 准备上下文 */
    MemAllocCtx_t ctx = {
        .size = size,
        .flags = flags,
        .ptr = NULL
    };

    /* 调用安全钩子 */
    int ret = call_security_hooks(HOOK_MEM_ALLOC, &ctx);
    if (ret != 0) {
        return NULL;  /* 安全检查失败 */
    }

    /* 继续分配... */
}
```

### 3.7 验收标准

- [ ] 框架实现完成
- [ ] 2 个内置安全模块
- [ ] 性能开销 < 5%
- [ ] MISRA-C:2012 零警告
- [ ] 代码覆盖率 > 90%

---

由于文档长度限制，我将继续在下一个文件中创建 Capability 系统和 Fast IPC 的详细实施方案。

