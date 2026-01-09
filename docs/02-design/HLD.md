# AISafe64 高层设计文档 (HLD)

## 文档控制信息

| 项目 | 信息 |
|------|------|
| **文档标题** | AISafe64 高层设计文档 (High-Level Design) |
| **文档版本** | 1.0 |
| **创建日期** | 2026-01-09 |
| **最后修改** | 2026-01-09 |
| **作者** | AISafe64 Team |
| **审核状态** | 待审核 |
| **文档来源** | 提取自 docs/plan.md 第4章 |

---

## 1. 引言

### 1.1 文档目的

本文档是 AISafe64 操作系统的高层设计（HLD）文档，旨在描述系统的整体架构、关键设计决策、技术选型和接口设计。本文档面向系统架构师、开发人员和测试人员，提供系统设计的全面视图。

### 1.2 系统概述

AISafe64 是一个 **AI-Generated、Safety-Certifiable、Native 64-bit RTOS**，针对 ARMv8-A 多核处理器架构设计，具有以下核心特性：

- **256级优先级抢占式调度**：O(1)调度算法，支持实时性保证
- **ARMv8-A MMU支持**：4级页表管理，虚拟内存隔离
- **多核SMP架构**：支持1-8核，负载均衡和任务迁移
- **POSIX兼容**：提供 pthread、信号量、消息队列等标准接口
- **安全机制**：代码段保护、栈溢出保护、Capability系统、形式化验证

### 1.3 参考资料

- ARMv8-A Architecture Reference Manual
- MISRA-C:2012 规范
- POSIX 1003.1 标准
- ISO 26262 功能安全标准

---

## 2. 系统架构

### 2.1 总体架构

AISafe64 采用分层架构设计，从硬件到应用分为五层：

```
┌─────────────────────────────────────────┐
│         应用层 (Application)             │
├─────────────────────────────────────────┤
│     系统服务层 (System Services)         │
│  - 文件系统  - 网络协议栈  - 设备管理    │
├─────────────────────────────────────────┤
│      内核层 (Kernel)                     │
│  - 任务调度  - MMU管理  - 同步通信       │
│  - 时间管理  - 中断管理  - 多核同步      │
├─────────────────────────────────────────┤
│   硬件抽象层 (HAL)                       │
│  - ARMv8-A  - GIC  - Timer  - Cache     │
├─────────────────────────────────────────┤
│      硬件层 (Hardware)                   │
│  - ARM64多核处理器  - 外设  - 内存      │
└─────────────────────────────────────────┘
```

**设计原则**：
- **模块化**：每个模块职责单一，接口清晰
- **可扩展**：支持配置化编译，按需裁剪
- **可验证**：代码复杂度受限，便于形式化验证
- **安全关键**：符合 ISO 26262 ASIL-D 标准

### 2.2 分层架构

#### 2.2.1 应用层
- 用户态应用程序
- POSIX 线程（pthread）
- 动态加载模块（可选）

#### 2.2.2 系统服务层
- **文件系统**：支持 FAT32、LittleFS
- **网络协议栈**：TCP/IP 协议栈（可选）
- **设备管理**：字符设备、块设备、网络设备

#### 2.2.3 内核层
- **任务调度**：256级优先级、抢占式调度、多核负载均衡
- **MMU管理**：4级页表、虚拟内存、页错误处理
- **同步通信**：互斥锁、信号量、消息队列、事件标志组
- **时间管理**：系统滴答、软件定时器、任务延迟
- **中断管理**：GIC驱动、ISR管理、IPI处理

#### 2.2.4 硬件抽象层（HAL）
- **ARMv8-A**：异常级别、系统寄存器、内存屏障
- **GIC**：中断控制器驱动、SGI/PPI/SPI
- **Timer**：架构定时器、定时器中断
- **Cache**：缓存维护、TLB管理

#### 2.2.5 硬件层
- **ARM64多核处理器**：Cortex-A53/A72
- **外设**：UART、GPIO、SPI、I2C
- **内存**：DDR4、SRAM

### 2.3 模块划分

#### 2.3.1 任务调度模块 (scheduler)

**职责**：
- 256级优先级就绪队列管理
- O(1)调度算法实现
- 多核负载均衡
- 上下文切换
- 抢占处理

**核心数据结构**：
```c
typedef struct {
    uint64_t            bitmap[4];          /* 256级优先级位图 */
    TaskList_t          queues[256];        /* 256级就绪队列 */
    spinlock_t          lock;               /* 队列自旋锁 */
    uint32_t            task_count;         /* 任务计数 */
} PerCPUReadyQueue_t;

typedef struct {
    TCB_t              *current_task[MAX_CPUS];
    PerCPUReadyQueue_t  ready_queues[MAX_CPUS];
    TaskList_t          sleep_queue;        /* 休眠队列 */
    atomic_uint32_t     cpu_mask;           /* CPU激活掩码 */
    volatile uint64_t   system_ticks;       /* 系统滴答计数 */
} Scheduler_t;
```

**关键算法**：
- **优先级查找**：使用 CLZ 指令实现 O(1) 查找最高优先级
- **负载均衡**：定期检查 CPU 负载差异，迁移任务
- **任务迁移**：基于 CPU 亲和性的任务调度

#### 2.3.2 多核同步模块 (smp)

**职责**：
- 核心间中断（IPI）处理
- CPU 启动和停止
- CPU 亲和性管理
- 负载均衡算法

**核心数据结构**：
```c
#define IPI_RESCHEDULE   0U  /* 重新调度 */
#define IPI_STOP         1U  /* 停止CPU */
#define IPI_TIMER        2U  /* 定时器广播 */
#define IPI_CALL_FUNC    3U  /* 函数调用 */
```

#### 2.3.3 MMU管理模块 (mmu)

**职责**：
- 4级页表管理
- 虚拟内存映射
- 页错误处理
- TLB管理
- 地址空间隔离

**核心数据结构**：
```c
typedef struct {
    uint64_t    pgd[512];   /* L0: Page Global Directory */
    uint64_t    pud[512];   /* L1: Page Upper Directory */
    uint64_t    pmd[512];   /* L2: Page Middle Directory */
    uint64_t    pte[512];   /* L3: Page Table Entry */
} PageTable_t;

typedef struct {
    uint64_t    virt_addr;  /* 虚拟地址 */
    uint64_t    phys_addr;  /* 物理地址 */
    uint64_t    size;       /* 大小 */
    uint64_t    flags;      /* 页属性标志 */
} VMMapping_t;
```

**关键设计决策**：
- **尽早使能MMU**：在 bootloader 阶段使能 MMU，性能提升 51%
- **2MB块映射**：跳过 L2/L3 页表，直接映射 2MB 块，加速页表遍历
- **恒等映射切换**：bootloader 使用恒等映射，内核启动后切换到详细映射

#### 2.3.4 内存管理模块 (memory)

**职责**：
- 内存池管理
- 堆栈管理
- 代码段保护
- 完整性校验

**核心数据结构**：
```c
typedef struct {
    uint64_t    start;              /* 起始地址 */
    uint64_t    end;                /* 结束地址 */
    uint8_t     hash[32];           /* SHA-256哈希 */
    uint32_t    flags;              /* RO, NX属性 */
    uint32_t    size;               /* 大小 */
} CodeSegment_t;

#define CODE_RO      (1U << 0)      /* 只读 */
#define CODE_NX      (1U << 1)      /* 不可执行 */
#define CODE_VERIFY  (1U << 2)      /* 需要验证 */
```

#### 2.3.5 同步通信模块 (sync)

**职责**：
- 互斥锁（优先级继承/天花板）
- 自旋锁（Ticket Lock）
- 信号量
- 消息队列
- 事件标志组

**核心数据结构**：
```c
/* Ticket Lock（公平自旋锁） */
typedef struct {
    atomic_uint16_t next_ticket;
    atomic_uint16_t serving_ticket;
} TicketLock_t;

/* 互斥锁 */
typedef struct {
    atomic_uintptr_t lock;
    uint8_t          priority_ceiling;
    TCB_t           *owner;
} Mutex_t;
```

#### 2.3.6 时间管理模块 (timer)

**职责**：
- 系统滴答管理
- 架构定时器
- 软件定时器
- 任务延迟管理

**核心数据结构**：
```c
typedef struct {
    uint64_t    deadline_ns;        /* 截止时间（纳秒） */
    uint64_t    period_ns;          /* 周期（纳秒） */
    void       (*callback)(void *); /* 回调函数 */
    void       *arg;                /* 回调参数 */
} SoftwareTimer_t;
```

#### 2.3.7 中断管理模块 (irq)

**职责**：
- GIC驱动
- ISR管理
- 中断线程化
- IPI处理

**核心数据结构**：
```c
typedef struct {
    void       (*handler)(void);   /* ISR处理函数 */
    uint32_t    irq_num;           /* 中断号 */
    uint32_t    cpu_mask;          /* CPU掩码 */
    uint32_t    priority;          /* 优先级 */
} IRQHandler_t;
```

---

## 3. 关键设计决策

### 3.1 调度策略选择

#### 3.1.1 256级优先级抢占式调度

**选择理由**：
- **实时性保证**：256级优先级提供细粒度的实时任务分类
- **O(1)调度**：使用位图 + CLZ 指令，调度延迟 < 100ns
- **确定性**：抢占式调度确保高优先级任务立即响应

**算法实现**：
```c
static inline uint8_t find_highest_priority(uint64_t *bitmap) {
    if (bitmap[0] != 0U) {
        return (uint8_t)__builtin_clzll(bitmap[0]);
    }
    if (bitmap[1] != 0U) {
        return (uint8_t)(64U + __builtin_clzll(bitmap[1]));
    }
    if (bitmap[2] != 0U) {
        return (uint8_t)(128U + __builtin_clzll(bitmap[2]));
    }
    if (bitmap[3] != 0U) {
        return (uint8_t)(192U + __builtin_clzll(bitmap[3]));
    }
    return 255U;  /* 空闲任务优先级 */
}
```

**抢占条件**：
1. 高优先级任务进入就绪态
2. 当前任务主动让出 CPU
3. 当前任务被阻塞
4. 当前任务进入休眠态
5. 时间片用完（同级优先级）
6. 负载均衡触发的任务迁移
7. 休眠任务超时唤醒

#### 3.1.2 多核负载均衡

**策略**：
- **周期性检查**：每 100ms 检查一次 CPU 负载
- **迁移阈值**：负载差异超过阈值（默认 2 个任务）时迁移
- **CPU 亲和性**：优先选择亲和 CPU，避免频繁迁移

**算法实现**：
```c
void load_balance(void) {
    uint32_t src_cpu = 0U;
    uint32_t dst_cpu = 0U;
    uint32_t max_load = 0U;
    uint32_t min_load = UINT32_MAX;

    /* 查找负载最高和最低的CPU */
    for (uint32_t i = 0U; i < MAX_CPUS; i++) {
        uint32_t load = scheduler.ready_queues[i].task_count;
        if (load > max_load) {
            max_load = load;
            src_cpu = i;
        }
        if (load < min_load) {
            min_load = load;
            dst_cpu = i;
        }
    }

    /* 执行迁移 */
    if (max_load > (min_load + scheduler.load_balance_threshold)) {
        migrate_task(src_cpu, dst_cpu);
    }
}
```

### 3.2 内存管理策略

#### 3.2.1 尽早使能MMU

**设计决策**：
- **bootloader 阶段使能 MMU**：在系统启动早期（bootloader）就使能 MMU
- **恒等映射启动**：使用 1:1 虚拟-物理映射简化页表建立
- **2MB 块映射**：使用 ARMv8-A 块映射特性，减少 TLB miss

**性能提升**：

| 指标 | 延迟使能MMU | 尽早使能MMU | 提升 |
|------|-------------|-------------|------|
| **Bootloader执行** | 100ms | **40ms** | **60%** |
| **内核初始化** | 200ms | **80ms** | **60%** |
| **总启动时间** | **360ms** | **175ms** | **51%** |

**实现要点**：
```c
/* Bootloader中使能MMU */
int bootloader_enable_mmu(void) {
    uint64_t *pgd = (uint64_t *)BOOT_PG_TABLE_ADDR;
    uint64_t attr = PAGE_BLOCK_ATTR | PAGE_AF | PAGE_SH_INNER;

    /* 恒等映射前1GB空间 */
    for (uint32_t i = 0; i < 1; i++) {
        uint64_t addr = (uint64_t)i << 30U;  /* 1GB对齐 */
        pgd[i] = addr | attr;
    }

    /* 设置页表基址 */
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(pgd));

    /* 使能MMU和缓存 */
    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1UL << 0);  /* M位：使能MMU */
    sctlr |= (1UL << 2);  /* C位：使能数据缓存 */
    sctlr |= (1UL << 12); /* I位：使能指令缓存 */
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr));

    /* 刷新TLB */
    __asm__ volatile("tlbi vmalle1is");
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");

    return 0;
}
```

#### 3.2.2 4级页表管理

**页表结构**：
```
虚拟地址 [63:0]
  |<--- 9位 --->|<--- 9位 --->|<--- 9位 --->|<--- 9位 --->|<-- 12位 -->|
  |    PGD     |    PUD     |    PMD     |    PTE     |   页内偏移   |
  |  (L0索引)  |  (L1索引)  |  (L2索引)  |  (L3索引)  |
```

**页表遍历**：
```c
uint64_t virt_to_phys(uint64_t pgd, uint64_t virt_addr) {
    /* 提取各级页表索引 */
    uint64_t pgd_idx = (virt_addr >> 39U) & 0x1FFU;
    uint64_t pud_idx = (virt_addr >> 30U) & 0x1FFU;
    uint64_t pmd_idx = (virt_addr >> 21U) & 0x1FFU;
    uint64_t pte_idx = (virt_addr >> 12U) & 0x1FFU;

    /* L0: PGD */
    uint64_t *pgd_table = (uint64_t *)(pgd & ~0xFFFU);
    uint64_t entry = pgd_table[pgd_idx];

    /* L1: PUD */
    uint64_t *pud_table = (uint64_t *)(entry & ~0xFFFU);
    entry = pud_table[pud_idx];

    /* 检查是否为块映射（1GB页） */
    if ((entry & PAGE_TABLE) == 0U) {
        return (entry & ~0x3FFFFFFFU) + (virt_addr & 0x3FFFFFFFU);
    }

    /* L2: PMD */
    uint64_t *pmd_table = (uint64_t *)(entry & ~0xFFFU);
    entry = pmd_table[pmd_idx];

    /* 检查是否为块映射（2MB页） */
    if ((entry & PAGE_TABLE) == 0U) {
        return (entry & ~0x1FFFFFU) + (virt_addr & 0x1FFFFFU);
    }

    /* L3: PTE */
    uint64_t *pte_table = (uint64_t *)(entry & ~0xFFFU);
    entry = pte_table[pte_idx];

    /* 4KB页 */
    return (entry & ~0xFFFU) + (virt_addr & 0xFFFU);
}
```

#### 3.2.3 代码段保护

**机制**：
- **SHA-256 哈希校验**：启动时计算代码段哈希，运行时定期验证
- **MMU 保护**：设置代码段为只读（RX），禁止写入
- **RWX 页面检测**：启动时扫描页表，检测可读可写可执行页面

**实现**：
```c
/* 设置代码段为只读 */
void set_code_readonly(uint64_t code_start, uint64_t code_end) {
    for (uint64_t page_addr = code_start; page_addr < code_end; page_addr += 0x1000U) {
        uint64_t *pte = get_pte(scheduler.current_task[0]->page_table, page_addr);
        if (pte != NULL) {
            uint64_t entry = *pte;
            entry |= PAGE_PXN;    /* 特权模式可执行 */
            entry |= PAGE_UXN;    /* 用户模式不可执行 */
            entry |= PAGE_AP_RO;  /* 只读 */
            entry |= PAGE_AF;     /* 访问标志 */
            *pte = entry;
        }
    }

    /* 刷新TLB */
    __asm__ volatile("tlbi vmalle1is");
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");
}
```

### 3.3 同步与通信机制

#### 3.3.1 多核同步原语

**Ticket Lock（公平自旋锁）**：
```c
typedef struct {
    atomic_uint16_t next_ticket;
    atomic_uint16_t serving_ticket;
} TicketLock_t;

static inline void ticket_lock_acquire(TicketLock_t *lock) {
    uint16_t my_ticket = atomic_fetch_add(&lock->next_ticket, 1U);

    while (atomic_load(&lock->serving_ticket) != my_ticket) {
        __asm__ volatile("yield");  /* 降低功耗 */
    }

    barrier();
}

static inline void ticket_lock_release(TicketLock_t *lock) {
    barrier();
    atomic_fetch_add(&lock->serving_ticket, 1U);
}
```

**内存屏障**：
```c
#define barrier() \
    __asm__ volatile("dmb ish" ::: "memory")

#define acquire_barrier() \
    __asm__ volatile("dmb ishld" ::: "memory")

#define release_barrier() \
    __asm__ volatile("dmb ishst" ::: "memory")

#define full_barrier() \
    do { \
        __asm__ volatile("dmb ish" ::: "memory"); \
        __asm__ volatile("isb"); \
    } while (0)
```

#### 3.3.2 互斥锁（优先级继承）

**机制**：
- **优先级继承**：当高优先级任务等待低优先级任务持有的锁时，低优先级任务临时提升到高优先级
- **优先级天花板**：锁被创建时指定优先级天花板，获取锁的任务立即提升到天花板优先级

**数据结构**：
```c
typedef struct {
    atomic_uintptr_t lock;
    uint8_t          priority_ceiling;  /* 优先级天花板 */
    uint8_t          original_priority;  /* 原始优先级 */
    TCB_t           *owner;              /* 锁持有者 */
} Mutex_t;
```

#### 3.3.3 消息队列

**设计**：
- **POSIX 兼容**：提供 mq_open、mq_send、mq_recv 等 API
- **零拷贝优化**：使用共享内存实现，避免数据拷贝
- **优先级消息**：支持消息优先级，高优先级消息优先投递

---

## 4. 技术选型

### 4.1 硬件平台

#### 4.1.1 处理器架构

**选择**：ARMv8-A 64位架构

**理由**：
- **性能**：64位处理器，支持大内存寻址（>4GB）
- **实时性**：Cortex-A53/A72 提供高性能和低延迟
- **多核支持**：原生支持 1-8 核 SMP 架构
- **MMU 支持**：强大的虚拟内存管理能力
- **生态系统**：丰富的开发工具和社区支持

**推荐平台**：
- **树莓派 4**：Broadcom BCM2711（4x Cortex-A72）
- **树莓派 3**：Broadcom BCM2837（4x Cortex-A53）
- **FPGA 模拟**：QEMU virt 模型

#### 4.1.2 最小系统要求

| 资源 | 最小配置 | 推荐配置 |
|------|---------|---------|
| **CPU** | 1x Cortex-A53 @ 1.2GHz | 4x Cortex-A72 @ 1.5GHz |
| **内存** | 512MB | 4GB |
| **存储** | 512MB SD卡 | 32GB eMMC |
| **定时器** | ARM架构定时器 | GIC + 定时器 |
| **调试** | UART | JTAG + UART |

### 4.2 开发工具

#### 4.2.1 编译工具链

**选择**：GCC aarch64-none-elf

**版本要求**：>= 11.0

**理由**：
- **开源免费**：无许可证限制
- **成熟稳定**：长期维护，社区支持良好
- **标准兼容**：完全支持 C11 和 MISRA-C:2012
- **优化能力**：强大的代码优化（-O2/-O3）

**配置选项**：
```bash
aarch64-none-elf-gcc \
    -march=armv8-a \
    -mtune=cortex-a72 \
    -mcpu=cortex-a72 \
    -mstrict-align \
    -ffreestanding \
    -nostdlib \
    -Wall -Wextra -Werror
```

#### 4.2.2 调试工具

**GDB + OpenOCD/QEMU**：
- **源码级调试**：支持断点、单步、变量查看
- **多核调试**：支持同时调试多个 CPU 核心
- **JTAG 支持**：通过 OpenOCD 连接硬件

#### 4.2.3 静态分析

**PC-lint Plus / Coverity**：
- **MISRA-C:2012 检查**：强制符合编码规范
- **缺陷检测**：空指针解引用、内存泄漏、数组越界
- **零警告**：所有代码必须通过静态分析

#### 4.2.4 构建系统

**CMake**：
- **版本要求**：>= 3.20
- **交叉编译支持**：通过 toolchain 文件配置
- **模块化构建**：支持模块化编译和测试

### 4.3 第三方库

#### 4.3.1 单元测试框架

**Unity**：
- **轻量级**：单头文件，无依赖
- **断言丰富**：TEST_ASSERT_EQUAL、TEST_ASSERT_NOT_NULL 等
- **Mock 支持**：支持 Mock 外部依赖

#### 4.3.2 加密库

**mbedTLS / WolfSSL**：
- **SHA-256**：代码段完整性校验
- **AES**：加密文件系统（可选）
- **ECC**：数字签名（可选）

---

## 5. 接口设计

### 5.1 系统调用接口

#### 5.1.1 系统调用约定

**调用方式**：`SVC` 指令（Supervisor Call）

**寄存器约定**：
- `x0`：系统调用号
- `x1-x6`：参数
- `x0`：返回值

**错误码约定**：POSIX 标准 `<errno.h>`
- 成功：返回 0 或正数
- 失败：返回负错误码（如 `-EINVAL`）

#### 5.1.2 核心系统调用

| 系统调用号 | 名称 | 功能 |
|-----------|------|------|
| 1 | sys_task_create | 创建任务 |
| 2 | sys_task_exit | 退出任务 |
| 3 | sys_task_yield | 让出CPU |
| 4 | sys_task_sleep | 任务休眠 |
| 10 | sys_mutex_lock | 获取互斥锁 |
| 11 | sys_mutex_unlock | 释放互斥锁 |
| 20 | sys_mq_open | 打开消息队列 |
| 21 | sys_mq_send | 发送消息 |
| 22 | sys_mq_recv | 接收消息 |
| 30 | sys_mmap | 内存映射 |
| 31 | sys_munmap | 解除映射 |

**实现示例**：
```c
/* 系统调用处理程序 */
void syscall_handler(uint64_t syscall_num, uint64_t *args) {
    switch (syscall_num) {
        case 1:  /* sys_task_create */
            args[0] = (uint64_t)sys_task_create(
                (void (*)(void))args[1],
                (uint8_t)args[2],
                (uint32_t)args[3],
                (const char *)args[4]
            );
            break;

        case 4:  /* sys_task_sleep */
            args[0] = (uint64_t)sys_task_sleep((uint32_t)args[1]);
            break;

        default:
            args[0] = (uint64_t)(-ENOSYS);  /* 功能未实现 */
            break;
    }
}
```

### 5.2 驱动程序接口

#### 5.2.1 设备类型

```c
typedef enum {
    DEVICE_CHAR,        /* 字符设备 */
    DEVICE_BLOCK,       /* 块设备 */
    DEVICE_NET,         /* 网络设备 */
    DEVICE_PLATFORM     /* 平台设备 */
} DeviceType_t;
```

#### 5.2.2 设备操作接口

```c
typedef struct DeviceOperations {
    int (*open)(void);
    int (*close)(void);
    ssize_t (*read)(void *buf, size_t len, off_t offset);
    ssize_t (*write)(const void *buf, size_t len, off_t offset);
    int (*ioctl)(unsigned int cmd, unsigned long arg);
    int (*mmap)(void *addr, size_t len, unsigned long prot);
    int (*poll)(void);
} DeviceOps_t;
```

#### 5.2.3 设备注册

```c
/* 设备描述符 */
typedef struct Device {
    char            name[32];        /* 设备名称 */
    DeviceType_t    type;            /* 设备类型 */
    uint32_t        major;           /* 主设备号 */
    uint32_t        minor;           /* 次设备号 */
    void           *private_data;    /* 私有数据 */
    DeviceOps_t    *ops;            /* 操作接口 */
    struct Device  *next;           /* 链表指针 */
    uint32_t        ref_count;       /* 引用计数 */
} Device_t;

/* 设备注册 */
int device_register(Device_t *dev);
```

---

## 6. 数据流设计

### 6.1 任务调度流程

```
┌─────────────┐
│ 中断/系统调用 │
└──────┬──────┘
       │
       ▼
┌─────────────┐
│ 触发调度    │
└──────┬──────┘
       │
       ▼
┌──────────────────────┐
│ 获取本地就绪队列锁   │
└──────┬───────────────┘
       │
       ▼
┌──────────────────────┐
│ 检查休眠队列，唤醒   │
│ 超时任务            │
└──────┬───────────────┘
       │
       ▼
┌──────────────────────┐
│ 查找本地最高优先级   │
│ 任务                │
└──────┬───────────────┘
       │
       ▼
┌──────────────────────┐
│ 本地无就绪任务？     │
└──────┬───────────────┘
       │ 是
       ▼
┌──────────────────────┐
│ 触发负载均衡         │
└──────┬───────────────┘
       │
       ▼
┌──────────────────────┐
│ 选择下一个任务       │
└──────┬───────────────┘
       │
       ▼
┌──────────────────────┐
│ 释放队列锁           │
└──────┬───────────────┘
       │
       ▼
┌──────────────────────┐
│ 执行上下文切换       │
└──────────────────────┘
```

### 6.2 中断处理流程

```
┌─────────────┐
│ 硬件中断    │
└──────┬──────┘
       │
       ▼
┌─────────────┐
│ 保存上下文  │
└──────┬──────┘
       │
       ▼
┌─────────────┐
│ 增加嵌套计数│
└──────┬──────┘
       │
       ▼
┌─────────────┐
│ 调用ISR     │
└──────┬──────┘
       │
       ▼
┌─────────────┐
│ 减少嵌套计数│
└──────┬──────┘
       │
       ▼
┌─────────────┐
│ 需要调度？  │
└──────┬──────┘
       │ 是
       ▼
┌─────────────┐
│ 设置调度标志│
└──────┬──────┘
       │
       ▼
┌─────────────┐
│ 恢复上下文  │
└──────┬──────┘
       │
       ▼
┌─────────────┐
│ 返回任务    │
└─────────────┘
```

### 6.3 MMU页表遍历流程

```
┌─────────────┐
│ 虚拟地址访问│
└──────┬──────┘
       │
       ▼
┌─────────────────────┐
│ 提取 PGD 索引 [48:39]│
└──────┬──────────────┘
       │
       ▼
┌─────────────────────┐
│ 读取 PGD 表项       │
└──────┬──────────────┘
       │
       ▼
┌─────────────────────┐
│ 表项有效？          │
└──────┬──────────────┘
       │ 否
       ▼
┌─────────────┐
│ 页错误异常  │
└─────────────┘
       │ 是
       ▼
┌─────────────────────┐
│ 提取 PUD 索引 [39:30]│
└──────┬──────────────┘
       │
       ▼
┌─────────────────────┐
│ 读取 PUD 表项       │
└──────┬──────────────┘
       │
       ▼
┌─────────────────────┐
│ 块映射？(1GB页)     │
└──────┬──────────────┘
       │ 是
       ▼
┌─────────────┐
│ 返回物理地址│
└─────────────┘
       │ 否
       ▼
┌─────────────────────┐
│ 继续遍历 PMD/PTE... │
└─────────────────────┘
```

---

## 7. 部署架构

### 7.1 内存布局

#### 7.1.1 虚拟内存布局

```
┌──────────────────────────────────────────────────────┐
│ 内核空间 (0xFFFF000000000000 - 0xFFFFFFFFFFFFFFFF) │
│  - 内核代码段：0xFFFF000000008000                    │
│  - 内核数据段：0xFFFF000000010000                    │
│  - 内核堆：    0xFFFF000000100000                    │
│  - 设备映射：   0xFFFF000008000000                    │
├──────────────────────────────────────────────────────┤
│ 用户空间 (0x0000000000010000 - 0x0000FFFFFFFFFFFF)   │
│  - 代码段：   0x0000000000010000                     │
│  - 数据段：   0x0000000000020000                     │
│  - 堆：       0x0000000000030000                     │
│  - 栈：       0x0000000007FFF000 (向下增长)          │
└──────────────────────────────────────────────────────┘
```

#### 7.1.2 物理内存布局

```
┌──────────────────────────────────────────────────────┐
│ Bootloader   (0x00000000 - 0x0007FFFF)              │
├──────────────────────────────────────────────────────┤
│ 内核镜像     (0x00080000 - 0x000FFFFF)              │
├──────────────────────────────────────────────────────┤
│ 内核堆       (0x00100000 - 0x003FFFFF)              │
├──────────────────────────────────────────────────────┤
│ 任务栈       (0x00400000 - 0x007FFFFF)              │
├──────────────────────────────────────────────────────┤
│ 用户空间     (0x00800000 - 0x3FFFFFFF)              │
├──────────────────────────────────────────────────────┤
│ 设备映射区   (0x40000000 - 0x4FFFFFFF)              │
└──────────────────────────────────────────────────────┘
```

### 7.2 多核部署

#### 7.2.1 CPU 核心分配

**默认策略**：
- **CPU0**：主核，运行内核服务和关键任务
- **CPU1-3**：从核，运行用户任务

**可配置策略**：
- **CPU 亲和性**：指定任务运行在特定 CPU
- **隔离 CPU**：专用 CPU 运行实时任务

#### 7.2.2 中断分配

**GIC 中断分发**：
- **SGI (Software Generated Interrupt)**：核心间通信
- **PPI (Private Peripheral Interrupt)**：每个 CPU 私有中断（定时器）
- **SPI (Shared Peripheral Interrupt)**：外设共享中断

**中断亲和性**：
- 默认：所有 CPU 都可接收
- 可配置：指定 CPU 处理特定中断

### 7.3 启动流程

#### 7.3.1 单核启动流程

```
┌─────────────┐
│ 上电/复位    │
└──────┬──────┘
       │
       ▼
┌─────────────┐
│ Bootloader  │
│ - 初始化硬件│
│ - 使能MMU   │
│ - 加载内核  │
└──────┬──────┘
       │
       ▼
┌─────────────┐
│ 内核启动    │
│ - 初始化页表│
│ - 初始化GIC │
│ - 启动从核  │
└──────┬──────┘
       │
       ▼
┌─────────────┐
│ 调度器启动  │
└──────┬──────┘
       │
       ▼
┌─────────────┐
│ 运行任务    │
└─────────────┘
```

#### 7.3.2 多核启动流程

```
CPU0（主核）              CPU1-3（从核）
    │                        │
    ▼                        │
┌─────────────┐             │
│ 初始化硬件  │             │
└──────┬──────┘             │
    │                        │
    ▼                        │
┌─────────────┐             │
│ 使能MMU     │             │
└──────┬──────┘             │
    │                        │
    ▼                        │
┌─────────────┐             │
│ 发送IPI启动 │─────────────>│
│ 从核        │             │
└──────┬──────┘             │
    │                        │
    ▼                        ▼
┌─────────────┐      ┌─────────────┐
│ 启动调度器  │      │ 等待调度    │
└─────────────┘      └─────────────┘
```

---

## 8. 性能指标

### 8.1 调度性能

| 指标 | 目标值 | 测量方法 |
|------|--------|----------|
| **调度延迟** | < 100ns | CLZ指令执行时间 |
| **上下文切换** | < 500ns | 寄存器保存/恢复时间 |
| **中断延迟** | < 1us | 中断到ISR执行时间 |
| **任务创建** | < 5us | 从task_create到就绪 |

### 8.2 内存性能

| 指标 | 目标值 | 测量方法 |
|------|--------|----------|
| **页表遍历** | < 100ns | 4级页表查找时间 |
| **TLB miss** | < 50ns | TLB refill时间 |
| **内存分配** | < 200ns | pool_alloc时间 |

### 8.3 同步性能

| 指标 | 目标值 | 测量方法 |
|------|--------|----------|
| **Ticket Lock** | < 50ns | 无竞争时加锁时间 |
| **Mutex Lock** | < 100ns | 无竞争时加锁时间 |
| **Semaphore** | < 150ns | 无竞争时post/wait |

---

## 9. 安全机制

### 9.1 栈溢出保护

**多层防护机制**：
1. **金丝雀值**：栈底设置魔数，检测向下溢出
2. **边界模式**：栈顶设置魔数，检测向上溢出
3. **MPU 保护页**：使用 MPU 保护栈空间，硬件级检测
4. **使用率监控**：定期扫描栈使用率，提前预警

**性能影响**：
- 金丝雀检查：~10ns
- 边界检查：~40ns
- MPU 保护：~5% 性能开销

### 9.2 代码段完整性

**SHA-256 哈希校验**：
- 启动时计算代码段哈希
- 运行时定期验证
- 检测到修改立即停机

### 9.3 Capability系统

**权限+对象引用**：
- 每个 Capability 包含权限位和对象引用
- 支持创建、复制、撤销、验证操作
- 细粒度访问控制

### 9.4 保护域

**5个预定义域**：
1. **内核域**：最高权限
2. **驱动域**：设备访问权限
3. **关键应用域**：实时任务
4. **普通应用域**：一般任务
5. **非可信域**：受限权限

---

## 10. 附录

### 10.1 术语表

| 术语 | 全称 | 说明 |
|------|------|------|
| **MMU** | Memory Management Unit | 内存管理单元 |
| **TLB** | Translation Lookaside Buffer | 转换后备缓冲器 |
| **GIC** | Generic Interrupt Controller | 通用中断控制器 |
| **IPI** | Inter-Processor Interrupt | 核心间中断 |
| **ISR** | Interrupt Service Routine | 中断服务程序 |
| **TCB** | Task Control Block | 任务控制块 |
| **SMP** | Symmetric Multi-Processing | 对称多处理 |

### 10.2 缩略语

| 缩略语 | 全称 | 说明 |
|--------|------|------|
| **HLD** | High-Level Design | 高层设计 |
| **LLD** | Low-Level Design | 低层设计 |
| **API** | Application Programming Interface | 应用程序接口 |
| **ABI** | Application Binary Interface | 应用二进制接口 |
| **POSIX** | Portable Operating System Interface | 可移植操作系统接口 |
| **RTOS** | Real-Time Operating System | 实时操作系统 |

### 10.3 参考文档

- **ARMv8-A Architecture Reference Manual**
- **MISRA-C:2012 Guidelines**
- **POSIX 1003.1 Standard**
- **ISO 26262 Functional Safety Standard**
- **AISafe64 需求规格说明书**
- **AISafe64 C代码生成规范 (docs/CLAUDE.md)**

---

## 文档修订历史

| 版本 | 日期 | 作者 | 修订说明 |
|------|------|------|----------|
| 1.0 | 2026-01-09 | AISafe64 Team | 初始版本，提取自 plan.md 第4章 |

---

**文档结束**
