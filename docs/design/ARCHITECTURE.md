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

---

# 附录 A: 任务调度器详细设计

> 原始文档: LLD-001-Scheduler.md

# LLD-001: Task Scheduler Low-Level Design
## 1. Module Overview

### 1.1 Purpose
The Task Scheduler module provides O(1) priority-based preemptive scheduling for up to 256 concurrent tasks across 1-8 CPU cores. It supports 256 priority levels, task migration, load balancing, and real-time scheduling constraints.

### 1.2 Scope
This document describes the low-level design of:
- 256-level priority bitmap scheduler
- Multi-core task dispatching and migration
- Context switching implementation
- Sleep/wake management
- Load balancing algorithm

### 1.3 References
- ARMv8-A Architecture Reference Manual
- MISRA-C:2012 Guidelines
- HLD-003: Kernel Module Design
- plan.md Section 4.3 (Scheduling Algorithm)

---

## 2. Data Structure Design

### 2.1 Task Control Block (TCB)

```c
/**
 * @brief Task Control Block
 * @note MISRA-C:2012 compliant
 * @note Cache-line aligned for SMP performance
 */
typedef struct TaskControlBlock
{
    /* Task Identification */
    uint64_t            task_id;             /**< Unique task ID */
    char                name[16];            /**< Task name (null-terminated) */

    /* Priority Management (256 levels) */
    uint8_t             priority;            /**< Current priority (0-255) */
    uint8_t             base_priority;       /**< Base priority (before donation) */
    uint8_t             state;               /**< Task state (see TaskState_t) */
    uint8_t             cpu_affinity;        /**< Preferred CPU (0-7) */

    /* Stack Management */
    uint64_t           *stack_ptr;           /**< Current stack pointer */
    uint64_t           *stack_base;          /**< Stack bottom address */
    uint32_t            stack_size;          /**< Stack size in bytes */
    uint32_t            stack_watermark;     /**< Stack usage watermark */

    /* Timing Information */
    uint64_t            runtime;             /**< Total runtime (ns) */
    uint64_t            last_wake_time;      /**< Last wake timestamp (ns) */
    uint64_t            timeslice;           /**< Time slice (ns) */
    uint64_t            sleep_deadline;      /**< Sleep deadline (ns) */
    uint64_t            sleep_start;         /**< Sleep start time (ns) */

    /* SMP Support */
    uint32_t            cpu_id;              /**< Currently running CPU */
    uint32_t            migrate_target;      /**< Migration target CPU */

    /* Synchronization */
    struct TaskControlBlock *next;           /**< Linked list pointer */
    struct TaskControlBlock *prev;           /**< Linked list pointer */
    uint16_t            lock_count;          /**< Number of held locks */

    /* Safety & Security */
    uint32_t            error_count;         /**< Error counter */
    uint64_t            page_table;          /**< Page table base address */

    /* Address Space Isolation */
    uint32_t            isolation_mode;      /**< 0=shared, 1=independent, 2=mixed */
    uint32_t            address_space_id;    /**< Address space group ID */

    /* CPU Context */
    uint64_t            context[32];         /**< Register save area */

} TCB_t;

/* Compile-time validation */
STATIC_ASSERT(sizeof(TCB_t) <= 1024U, TCB_size_exceeds_limit);
STATIC_ASSERT((offsetof(TCB_t, context) & 0xFU) == 0U, context_misaligned);
```

### 2.2 Task State Enumeration

```c
/**
 * @brief Task State Enumeration
 * @note MISRA-C:2012 compliant
 */
typedef enum
{
    TASK_READY = 0U,        /**< Ready: Waiting for CPU */
    TASK_RUNNING,           /**< Running: Currently executing */
    TASK_BLOCKED,           /**< Blocked: Waiting for resource */
    TASK_SLEEPING,          /**< Sleeping: Delayed wait */
    TASK_SUSPENDED          /**< Suspended: Explicitly suspended */
} TaskState_t;
```

### 2.3 Per-CPU Ready Queue

```c
/**
 * @brief Task List Head
 */
typedef struct
{
    TCB_t *head;            /**< List head */
    TCB_t *tail;            /**< List tail */
    uint32_t count;         /**< Task count */
} TaskList_t;

/**
 * @brief Per-CPU Ready Queue
 * @note 64-byte cache-line aligned
 */
typedef struct __attribute__((aligned(64)))
{
    uint64_t            bitmap[4];           /**< 256-bit priority map (4×64) */
    TaskList_t          queues[256];         /**< 256 priority queues */
    atomic_uint_fast32_t lock;               /**< Spinlock for queue access */
    uint32_t            task_count;          /**< Total task count */
} PerCPUReadyQueue_t;
```

### 2.4 Scheduler Core Structure

```c
/**
 * @brief Global Scheduler Structure
 * @note Access only via scheduler_lock()
 */
typedef struct
{
    /* Current Tasks */
    TCB_t              *current_task[MAX_CPUS];

    /* Ready Queues (per-CPU) */
    PerCPUReadyQueue_t  ready_queues[MAX_CPUS];

    /* Sleep Queue (sorted by deadline) */
    TaskList_t          sleep_queue;
    atomic_uint_fast32_t sleep_queue_lock;

    /* Blocked Queue (waiting for resources) */
    TaskList_t          blocked_queue;
    atomic_uint_fast32_t blocked_queue_lock;

    /* Scheduler State */
    atomic_uint_fast32_t cpu_mask;           /**< Active CPU mask */
    volatile uint64_t   lock_count[MAX_CPUS]; /**< Scheduler lock nesting */
    volatile uint8_t    scheduler_running;   /**< Running flag */

    /* System Time */
    volatile uint64_t   system_ticks;        /**< Tick counter */
    volatile uint64_t   system_time_ns;      /**< System time (ns) */

    /* Statistics */
    uint64_t            task_switches[MAX_CPUS];
    uint64_t            cpu_idle_ticks[MAX_CPUS];

    /* Load Balancing */
    uint32_t            load_balance_threshold;

} Scheduler_t;

/* Global scheduler instance */
extern Scheduler_t g_scheduler;
```

---

## 3. API Interface Definition

### 3.1 Scheduler Initialization

```c
/**
 * @brief Initialize the task scheduler
 * @return 0 on success, negative error code on failure
 *
 * @note Must be called before any other scheduler function
 * @note Not thread-safe
 * @warning Must be called before scheduler_start()
 */
int32_t scheduler_init(void);
```

### 3.2 Scheduler Control

```c
/**
 * @brief Start the scheduler
 * @return Does not return
 *
 * @note Enables interrupts and begins task scheduling
 * @warning Never returns
 */
void scheduler_start(void) __attribute__((noreturn));

/**
 * @brief Trigger task rescheduling
 * @note May be called from task context or ISR
 * @warning Must not be called with scheduler locked
 */
void schedule(void);

/**
 * @brief Lock the scheduler (disable task switching)
 * @note Nestable: multiple calls require matching unlocks
 * @warning Must unlock before calling blocking functions
 */
void scheduler_lock(void);

/**
 * @brief Unlock the scheduler
 * @note Decrements lock count; triggers schedule if count == 0
 */
void scheduler_unlock(void);
```

### 3.3 Task Management

```c
/**
 * @brief Create a new task
 * @param entry Task entry function (must not return)
 * @param priority Task priority (0-255)
 * @param stack_size Stack size in bytes (min 4096)
 * @param name Task name (max 16 chars)
 * @return Task ID (>0) on success, 0 on failure
 *
 * @note Task starts in READY state
 * @warning entry function must not return
 */
uint32_t task_create(void (*entry)(void),
                     uint8_t priority,
                     uint32_t stack_size,
                     const char *name);

/**
 * @brief Delete a task
 * @param task_id Task ID to delete
 * @return 0 on success, negative error code on failure
 *
 * @note Cannot delete current task or idle task
 * @warning Frees task resources including stack
 */
int32_t task_delete(uint32_t task_id);

/**
 * @brief Yield CPU to next ready task
 * @note Only affects tasks of same priority
 */
void task_yield(void);

/**
 * @brief Suspend a task
 * @param task_id Task ID to suspend
 * @return 0 on success, negative error code on failure
 *
 * @note Suspended task does not execute
 */
int32_t task_suspend(uint32_t task_id);

/**
 * @brief Resume a suspended task
 * @param task_id Task ID to resume
 * @return 0 on success, negative error code on failure
 */
int32_t task_resume(uint32_t task_id);
```

### 3.4 Task Sleep API

```c
/**
 * @brief Sleep current task for specified milliseconds
 * @param delay_ms Delay in milliseconds
 *
 * @note Task enters SLEEPING state
 * @note Relative sleep (from current time)
 * @warning Only callable from task context
 */
void task_sleep(uint32_t delay_ms);

/**
 * @brief Sleep until absolute deadline
 * @param deadline_ns Absolute deadline in nanoseconds
 * @return 0 on success, negative error code on failure
 *
 * @note Absolute sleep (to specific time)
 * @warning Only callable from task context
 */
int32_t task_delay_until(uint64_t deadline_ns);

/**
 * @brief Periodic task sleep
 * @param period_ns Period in nanoseconds
 * @param last_wake_time Pointer to last wake time
 *
 * @note Calculates next wake time automatically
 * @note Compensates for execution time drift
 * @warning Only callable from task context
 */
void task_sleep_periodic(uint64_t period_ns, uint64_t *last_wake_time);
```

### 3.5 Task Query API

```c
/**
 * @brief Get current task ID
 * @return Current task ID (>0), 0 if no current task
 */
uint32_t task_get_current(void);

/**
 * @brief Get task priority
 * @param task_id Task ID
 * @return Priority (0-255), or 255 if task not found
 */
uint8_t task_get_priority(uint32_t task_id);

/**
 * @brief Set task priority
 * @param task_id Task ID
 * @param new_priority New priority (0-255)
 * @return 0 on success, negative error code on failure
 *
 * @note May trigger immediate reschedule if priority raised
 */
int32_t task_set_priority(uint32_t task_id, uint8_t new_priority);

/**
 * @brief Get task state
 * @param task_id Task ID
 * @return Task state, or TASK_SUSPENDED if task not found
 */
TaskState_t task_get_state(uint32_t task_id);
```

---

## 4. Algorithm Implementation Details

### 4.1 256-Level Priority Lookup (O(1))

```c
/**
 * @brief Find highest priority in bitmap
 * @param bitmap 256-bit bitmap (array of 4×uint64_t)
 * @return Highest priority (0-255), or 255 if bitmap empty
 *
 * @note Uses ARM64 CLZ instruction for O(1) lookup
 * @note Priority 0 is highest, 255 is lowest
 * @note Caller must hold ready_queue lock
 */
static inline uint8_t find_highest_priority(uint64_t *bitmap)
{
    /* bitmap[0]: priorities 0-63 (most significant) */
    if (bitmap[0] != 0U)
    {
        return (uint8_t)__builtin_clzll(bitmap[0]);
    }

    /* bitmap[1]: priorities 64-127 */
    if (bitmap[1] != 0U)
    {
        return (uint8_t)(64U + __builtin_clzll(bitmap[1]));
    }

    /* bitmap[2]: priorities 128-191 */
    if (bitmap[2] != 0U)
    {
        return (uint8_t)(128U + __builtin_clzll(bitmap[2]));
    }

    /* bitmap[3]: priorities 192-255 (least significant) */
    if (bitmap[3] != 0U)
    {
        return (uint8_t)(192U + __builtin_clzll(bitmap[3]));
    }

    /* No ready tasks */
    return 255U;
}
```

### 4.2 Bitmap Manipulation

```c
/**
 * @brief Set priority bit in bitmap
 * @param bitmap 256-bit bitmap
 * @param priority Priority (0-255)
 *
 * @note Caller must hold ready_queue lock
 */
static inline void bitmap_set(uint64_t *bitmap, uint8_t priority)
{
    uint32_t index = (uint32_t)(priority >> 6U);
    uint64_t mask = 1ULL << (63U - (priority & 0x3FU));
    bitmap[index] |= mask;
}

/**
 * @brief Clear priority bit in bitmap
 * @param bitmap 256-bit bitmap
 * @param priority Priority (0-255)
 *
 * @note Caller must hold ready_queue lock
 */
static inline void bitmap_clear(uint64_t *bitmap, uint8_t priority)
{
    uint32_t index = (uint32_t)(priority >> 6U);
    uint64_t mask = ~(1ULL << (63U - (priority & 0x3FU)));
    bitmap[index] &= mask;
}
```

### 4.3 Task Enqueue/Dequeue

```c
/**
 * @brief Add task to ready queue
 * @param task Task to enqueue
 *
 * @note Caller must hold ready_queue lock
 */
static void task_enqueue(TCB_t *task)
{
    uint32_t cpu_id = task->cpu_affinity;
    PerCPUReadyQueue_t *queue = &g_scheduler.ready_queues[cpu_id];
    uint8_t prio = task->priority;

    /* Add to priority queue tail */
    if (queue->queues[prio].tail == NULL)
    {
        queue->queues[prio].head = task;
        queue->queues[prio].tail = task;
        task->next = NULL;
        task->prev = NULL;
    }
    else
    {
        task->next = NULL;
        task->prev = queue->queues[prio].tail;
        queue->queues[prio].tail->next = task;
        queue->queues[prio].tail = task;
    }

    /* Set bitmap bit */
    bitmap_set(queue->bitmap, prio);
    queue->task_count++;

    /* Update task state */
    task->state = TASK_READY;
}

/**
 * @brief Remove highest priority task from ready queue
 * @param cpu_id CPU ID
 * @return Task pointer, or NULL if queue empty
 *
 * @note Caller must hold ready_queue lock
 */
static TCB_t* task_dequeue(uint32_t cpu_id)
{
    PerCPUReadyQueue_t *queue = &g_scheduler.ready_queues[cpu_id];
    TCB_t *task;

    /* Find highest priority */
    uint8_t prio = find_highest_priority(queue->bitmap);
    if (prio == 255U)
    {
        return NULL;  /* Queue empty */
    }

    /* Remove from queue head */
    task = queue->queues[prio].head;
    if (task == NULL)
    {
        return NULL;
    }

    /* Update list pointers */
    queue->queues[prio].head = task->next;
    if (task->next != NULL)
    {
        task->next->prev = NULL;
    }
    else
    {
        /* Queue empty, clear bitmap bit */
        bitmap_clear(queue->bitmap, prio);
        queue->queues[prio].tail = NULL;
    }

    task->next = NULL;
    task->prev = NULL;
    queue->task_count--;

    return task;
}
```

### 4.4 Core Scheduling Algorithm

```c
/**
 * @brief Core scheduling function
 *
 * @note Called from schedule(), timer ISR, yield()
 * @note Must be called with scheduler lock held
 */
static void schedule_internal(void)
{
    uint32_t cpu_id = get_cpu_id();
    TCB_t *current_task = g_scheduler.current_task[cpu_id];
    TCB_t *next_task;

    /* 1. Wake sleeping tasks (check sleep queue) */
    wake_sleeping_tasks();

    /* 2. Select next task from local ready queue */
    atomic_lock(&g_scheduler.ready_queues[cpu_id].lock);
    next_task = task_dequeue(cpu_id);

    /* 3. Load balancing if no local tasks */
    if (next_task == NULL)
    {
        next_task = steal_task(cpu_id);
    }

    atomic_unlock(&g_scheduler.ready_queues[cpu_id].lock);

    /* 4. No runnable task, run idle task */
    if (next_task == NULL)
    {
        next_task = g_idle_task[cpu_id];
    }

    /* 5. Context switch if needed */
    if (next_task != current_task)
    {
        g_scheduler.task_switches[cpu_id]++;
        context_switch(current_task, next_task);
    }
}
```

### 4.5 Sleep Queue Management

```c
/**
 * @brief Insert task into sleep queue (sorted by deadline)
 * @param task Task to insert
 *
 * @note Sleep queue is ordered ascending by sleep_deadline
 * @note Caller must hold sleep_queue lock
 */
static void sleep_queue_insert(TCB_t *task)
{
    TCB_t *prev = NULL;
    TCB_t *curr = g_scheduler.sleep_queue.head;

    /* Find insertion position (maintain sorted order) */
    while ((curr != NULL) && (curr->sleep_deadline < task->sleep_deadline))
    {
        prev = curr;
        curr = curr->next;
    }

    /* Insert task */
    if (prev == NULL)
    {
        /* Insert at head */
        task->next = g_scheduler.sleep_queue.head;
        task->prev = NULL;
        if (g_scheduler.sleep_queue.head != NULL)
        {
            g_scheduler.sleep_queue.head->prev = task;
        }
        g_scheduler.sleep_queue.head = task;
    }
    else
    {
        /* Insert in middle or at tail */
        task->next = prev->next;
        task->prev = prev;
        if (prev->next != NULL)
        {
            prev->next->prev = task;
        }
        else
        {
            g_scheduler.sleep_queue.tail = task;
        }
        prev->next = task;
    }
}

/**
 * @brief Wake tasks whose sleep deadline has expired
 *
 * @note Called from schedule() and timer ISR
 * @note Moves tasks from sleep queue to ready queue
 */
static void wake_sleeping_tasks(void)
{
    uint64_t current_time = get_system_time_ns();
    TCB_t *task;
    TCB_t *next;

    atomic_lock(&g_scheduler.sleep_queue_lock);

    task = g_scheduler.sleep_queue.head;
    while ((task != NULL) && (task->sleep_deadline <= current_time))
    {
        next = task->next;

        /* Remove from sleep queue */
        if (task->prev != NULL)
        {
            task->prev->next = task->next;
        }
        else
        {
            g_scheduler.sleep_queue.head = task->next;
        }
        if (task->next != NULL)
        {
            task->next->prev = task->prev;
        }
        else
        {
            g_scheduler.sleep_queue.tail = task->prev;
        }

        /* Change state to READY */
        task->state = TASK_READY;

        /* Add to ready queue */
        uint32_t cpu_id = task->cpu_affinity;
        atomic_lock(&g_scheduler.ready_queues[cpu_id].lock);
        task_enqueue(task);
        atomic_unlock(&g_scheduler.ready_queues[cpu_id].lock);

        task = next;
    }

    atomic_unlock(&g_scheduler.sleep_queue_lock);
}
```

### 4.6 Load Balancing Algorithm

```c
/**
 * @brief Steal task from another CPU
 * @param cpu_id Current CPU ID
 * @return Stolen task, or NULL if no stealable task
 *
 * @note Implements work-stealing load balancing
 * @note Prefers stealing from most-loaded CPU
 */
static TCB_t* steal_task(uint32_t cpu_id)
{
    uint32_t src_cpu;
    uint32_t max_load = 0U;
    TCB_t *task = NULL;

    /* Find most-loaded CPU */
    for (uint32_t i = 0U; i < MAX_CPUS; i++)
    {
        if (i == cpu_id)
        {
            continue;
        }

        uint32_t load = g_scheduler.ready_queues[i].task_count;
        if (load > max_load)
        {
            max_load = load;
            src_cpu = i;
        }
    }

    /* Skip if no CPU has significant load */
    if (max_load < 2U)
    {
        return NULL;
    }

    /* Try to steal from source CPU */
    atomic_lock(&g_scheduler.ready_queues[src_cpu].lock);

    /* Find lowest priority task (least important) */
    for (int32_t prio = 255; prio >= 0; prio--)
    {
        TaskList_t *queue = &g_scheduler.ready_queues[src_cpu].queues[prio];
        if (queue->tail != NULL)
        {
            /* Steal from tail (least recently used) */
            task = queue->tail;

            /* Remove from source queue */
            queue->tail = task->prev;
            if (task->prev != NULL)
            {
                task->prev->next = NULL;
            }
            else
            {
                queue->head = NULL;
                bitmap_clear(g_scheduler.ready_queues[src_cpu].bitmap, (uint8_t)prio);
            }

            g_scheduler.ready_queues[src_cpu].task_count--;
            break;
        }
    }

    atomic_unlock(&g_scheduler.ready_queues[src_cpu].lock);

    /* Update task affinity */
    if (task != NULL)
    {
        task->cpu_affinity = cpu_id;
        task->cpu_id = cpu_id;
        task->next = NULL;
        task->prev = NULL;
    }

    return task;
}
```

---

## 5. Performance Requirements

### 5.1 Timing Constraints

| Operation | Maximum Latency |
|-----------|-----------------|
| **Task Creation** | 50 μs |
| **Task Deletion** | 100 μs |
| **Context Switch** | 5 μs |
| **Schedule() Call** | 10 μs |
| **Priority Lookup** | 200 ns (O(1)) |
| **Task Enqueue** | 500 ns |
| **Task Dequeue** | 500 ns |

### 5.2 Memory Constraints

| Resource | Limit |
|----------|-------|
| **TCB Size** | ≤ 1024 bytes |
| **Ready Queue Memory** | ≤ 64 KB (256 queues × 4 CPUs) |
| **Total Scheduler Memory** | ≤ 512 KB |

### 5.3 Scalability

- **Maximum Tasks**: 256
- **Maximum CPUs**: 8
- **Priority Levels**: 256
- **Task Switches/Second**: > 100,000

---

## 6. MISRA-C:2012 Compliance

### 6.1 Compliance Strategy

All scheduler code shall comply with MISRA-C:2012 rules. Key deviations and justifications:

| Rule | Deviation | Justification |
|------|-----------|---------------|
| Rule 11.5 | Void pointer to TCB_t* | Type-safe via opaque pattern |
| Rule 21.1 | Include paths | Resolved via build system |

### 6.2 Static Analysis

- **Tool**: PC-lint Plus / Coverity
- **Compliance Target**: Zero warnings
- **Frequency**: Every commit

### 6.3 Runtime Checks

```c
/* Compile-time assertions */
STATIC_ASSERT(sizeof(TCB_t) <= 1024U, TCB_too_large);
STATIC_ASSERT(256 == MAX_PRIORITY_LEVELS, priority_levels_fixed);

/* Runtime assertions */
ASSERT(task_id < MAX_TASKS);
ASSERT(priority <= 255);
```

---

## 7. Testing Strategy

### 7.1 Unit Tests

| Test Case | Description |
|-----------|-------------|
| **TC-SCH-001** | Basic task creation and deletion |
| **TC-SCH-002** | Priority ordering (0 highest) |
| **TC-SCH-003** | Round-robin within same priority |
| **TC-SCH-004** | Preemption by higher priority |
| **TC-SCH-005** | Task sleep and wake |
| **TC-SCH-006** | Scheduler lock nesting |
| **TC-SCH-007** | CPU affinity |
| **TC-SCH-008** | Load balancing |
| **TC-SCH-009** | Context switch integrity |
| **TC-SCH-010** | Edge cases (256 tasks, 8 CPUs) |

### 7.2 Integration Tests

| Test Case | Description |
|-----------|-------------|
| **TC-SCH-INT-001** | Scheduler with timer module |
| **TC-SCH-INT-002** | Scheduler with sync primitives |
| **TC-SCH-INT-003** | Multi-core stress test |
| **TC-SCH-INT-004** | Priority inversion prevention |

### 7.3 Performance Tests

| Test Case | Metric | Target |
|-----------|--------|--------|
| **TC-SCH-PERF-001** | Context switch time | < 5 μs |
| **TC-SCH-PERF-002** | Schedule latency | < 10 μs |
| **TC-SCH-PERF-003** | Max throughput | > 100k switches/sec |

### 7.4 Coverage Requirements

- **Statement Coverage**: > 95%
- **Branch Coverage**: > 90%
- **MC/DC Coverage**: > 85% (critical functions)

---

## 8. Configuration Options

### 8.1 MenuConfig Options

```kconfig
config SCHEDULER
    bool "Task Scheduler"
    default y

config MAX_TASKS
    int "Maximum number of tasks"
    range 1 256
    default 32
    depends on SCHEDULER

config PRIORITY_LEVELS
    int "Number of priority levels"
    range 1 256
    default 256
    depends on SCHEDULER

config TIME_SLICE_NS
    int "Default time slice (nanoseconds)"
    range 1000 10000000
    default 10000
    depends on SCHEDULER

config LOAD_BALANCE_INTERVAL_MS
    int "Load balance interval (milliseconds)"
    range 1 1000
    default 100
    depends on SCHEDULER && SMP
```

---

## 9. Error Handling

### 9.1 Error Codes

| Error Code | Description |
|------------|-------------|
| `ERROR_INVALID_TASK_ID` | Task ID does not exist |
| `ERROR_INVALID_PRIORITY` | Priority out of range |
| `ERROR_TASK_RUNNING` | Cannot delete running task |
| `ERROR_OUT_OF_MEMORY` | TCB allocation failed |
| `ERROR_STACK_TOO_SMALL` | Stack size below minimum |

### 9.2 Error Recovery

- **Task Creation Failure**: Return 0 (caller checks)
- **Out of Memory**: Trigger system panic (no recovery)
- **Stack Overflow**: Trigger core dump and task restart
- **Context Switch Failure**: Trigger system panic (fatal)

---

## 10. Traceability

### 10.1 Requirements Traceability

| LLD Section | HLD Section | Plan.md Section |
|-------------|-------------|-----------------|
| Data Structures | 4.2.1 TCB | 4.2.1 |
| Scheduling Algorithm | 4.3 Scheduling | 4.3 |
| Context Switch | 4.4 Context Switch | 4.4 |
| Sleep Management | 4.6 Sleep | 4.6 |

### 10.2 Test Coverage Traceability

| Test Case | Requirement |
|-----------|-------------|
| TC-SCH-001 | SCH-001: Task creation |
| TC-SCH-004 | SCH-002: Preemption |
| TC-SCH-PERF-001 | NFR-001: Context switch < 5μs |

---

## Appendix A: Context Switch Assembly

```assembly
/**
 * @file context_switch.S
 * @brief ARM64 context switch implementation
 */

.global context_switch
context_switch:
    /* x0: current TCB
     * x1: current SP
     * x2: next SP
     */

    /* Save current context */
    stp     x29, x30, [x1, #-16]!
    stp     x27, x28, [x1, #-16]!
    stp     x25, x26, [x1, #-16]!
    stp     x23, x24, [x1, #-16]!
    stp     x21, x22, [x1, #-16]!
    stp     x19, x20, [x1, #-16]!

    /* Save SPSR and ELR */
    mrs     x16, spsr_el1
    mrs     x17, elr_el1
    stp     x16, x17, [x1, #-16]!

    /* Save SP and TCB pointer */
    mov     x16, sp
    stp     x16, x0, [x1, #-16]!

    /* Memory barrier */
    dmb     ish

    /* Restore next context */
    ldp     x16, x0, [x2], #16
    mov     sp, x16

    ldp     x16, x17, [x2], #16
    msr     spsr_el1, x16
    msr     elr_el1, x17

    ldp     x19, x20, [x2], #16
    ldp     x21, x22, [x2], #16
    ldp     x23, x24, [x2], #16
    ldp     x25, x26, [x2], #16
    ldp     x27, x28, [x2], #16
    ldp     x29, x30, [x2], #16

    /* Memory barrier */
    dmb     ish

    /* Return to next task */
    ret
```

---

**Document End**

---

# 附录 B: 内存管理详细设计

> 原始文档: LLD-002-Memory.md

# LLD-002: Memory Management Low-Level Design
## 1. Module Overview

### 1.1 Purpose
The Memory Management module provides 4-level page table management for ARMv8-A MMU, virtual-to-physical address translation, page allocation/deallocation, and memory protection for safety-critical systems.

### 1.2 Scope
This document describes the low-level design of:
- 4-level page table hierarchy (PGD → PUD → PMD → PTE)
- Page frame allocation (buddy system)
- Virtual memory mapping and unmapping
- Page fault handling
- TLB management
- Address space isolation

### 1.3 References
- ARMv8-A Architecture Reference Manual (DDI 0487)
- MISRA-C:2012 Guidelines
- HLD-003: Kernel Module Design
- plan.md Section 4.5 (MMU Management)

---

## 2. Data Structure Design

### 2.1 Page Table Hierarchy

```c
/**
 * @brief 4-level Page Table Entry format (ARMv8-A)
 * @note 64-bit descriptor format
 *
 * [63]    UXN (User Execute Never)
 * [62]    PXN (Privileged Execute Never)
 * [61:52] Reserved (SBZ)
 * [51]    DBM (Dirty Bit Modifier, optional)
 * [50]    Contiguous (hint)
 * [49]    PXN (again, for block descriptors)
 * [48]    UXN (again, for block descriptors)
 * [47:12] Output address (physical page number)
 * [11]    NG (Not Global)
 * [10]    AF (Access Flag)
 * [9:8]   SH (Shareability: 00=Non, 11=Inner)
 * [7:6]   AP (Access permissions: 00=RW, 10=RO)
 * [5:2]   AttrIndx (Memory attributes)
 * [1]     Contiguous (again, for block)
 * [0]     Valid/Type (0=Invalid, 1=Block, 3=Table)
 */
typedef uint64_t pte_t;  /**< Page Table Entry */

/**
 * @brief Page Table Hierarchy
 */
typedef struct
{
    pte_t    pgd[512];    /**< L0: Page Global Directory (512 entries) */
    pte_t    pud[512];    /**< L1: Page Upper Directory (512 entries) */
    pte_t    pmd[512];    /**< L2: Page Middle Directory (512 entries) */
    pte_t    pte[512];    /**< L3: Page Table Entry (512 entries) */
} PageTableHierarchy_t;

/* Compile-time validation */
STATIC_ASSERT(sizeof(PageTableHierarchy_t) == 8192U,
              PageTable_size_mismatch);
```

### 2.2 Page Table Entry Flags

```c
/**
 * @brief Page Table Entry Flags
 * @note Compliant with ARMv8-A specification
 */
#define PAGE_VALID           (1UL << 0)   /**< Descriptor valid */
#define PAGE_TABLE           (1UL << 1)   /**< Table descriptor */
#define PAGE_BLOCK           (1UL << 1)   /**< Block descriptor */

/* Access permissions */
#define PAGE_AP_RO           (2UL << 6)   /**< Read-only */
#define PAGE_AP_RW           (0UL << 6)   /**< Read-write */
#define PAGE_AP_USER         (3UL << 6)   /**< User-accessible */

/* Execute never */
#define PAGE_PXN             (1UL << 53)  /**< Privileged XN */
#define PAGE_UXN             (1UL << 54)  /**< User XN */

/* Memory attributes */
#define PAGE_AF              (1UL << 10)  /**< Access flag */
#define PAGE_SH_INNER        (3UL << 8)   /**< Inner shareable */
#define PAGE_SH_OUTER        (2UL << 8)   /**< Outer shareable */
#define PAGE_SH_NONE         (0UL << 8)   /**< Non-shareable */

/* Normal memory attributes (MAIR_IDX) */
#define PAGE_ATTR_NORMAL     (0UL << 2)   /**< Normal memory */
#define PAGE_ATTR_DEVICE     (1UL << 2)   /**< Device memory */
#define PAGE_ATTR_NC         (2UL << 2)   /**< Non-cacheable */

/* Block mapping attributes */
#define PAGE_BLOCK_ATTR      (PAGE_BLOCK | PAGE_AF | PAGE_SH_INNER | PAGE_ATTR_NORMAL)

/* Table mapping attributes */
#define PAGE_TABLE_ATTR      (PAGE_TABLE | PAGE_AF | PAGE_SH_INNER)
```

### 2.3 Virtual Memory Area (VMA)

```c
/**
 * @brief Virtual Memory Area descriptor
 */
typedef struct VirtualMemoryArea
{
    uint64_t    virt_start;        /**< Virtual start address */
    uint64_t    virt_end;          /**< Virtual end address */
    uint64_t    phys_start;        /**< Physical start address */
    uint64_t    flags;             /**< Protection flags */
    uint32_t    ref_count;         /**< Reference count */
    struct VirtualMemoryArea *next; /**< Next VMA in list */
} VMA_t;

/* VMA flags */
#define VMA_READ             (1U << 0)
#define VMA_WRITE            (1U << 1)
#define VMA_EXECUTE          (1U << 2)
#define VMA_USER             (1U << 3)
#define VMA_SHARED           (1U << 4)
```

### 2.4 Address Space Descriptor

```c
/**
 * @brief Address Space Descriptor
 * @note Represents a task's virtual address space
 */
typedef struct AddressSpace
{
    uint64_t            page_table;        /**< PGD physical address */
    VMA_t              *vma_list;          /**< List of VMAs */
    atomic_uint_fast32_t lock;             /**< VMA list lock */
    uint32_t            ref_count;         /**< Reference count */
    uint32_t            asid;              /**< Address Space ID */
} AddressSpace_t;
```

### 2.5 Page Frame Descriptor

```c
/**
 * @brief Page Frame Descriptor (buddy system)
 */
typedef struct PageFrame
{
    uint64_t            phys_addr;         /**< Physical address */
    uint32_t            order;             /**< Buddy order (log2 size) */
    uint32_t            ref_count;         /**< Reference count */
    struct PageFrame   *buddy;            /**< Buddy page */
    struct PageFrame   *next;             /**< Free list pointer */
} PageFrame_t;
```

### 2.6 Memory Zone Structure

```c
/**
 * @brief Memory Zone (DMA, Normal, HighMem)
 */
typedef enum
{
    ZONE_DMA = 0U,        /**< DMA-able memory (< 4GB) */
    ZONE_NORMAL,          /**< Normal memory (4GB - 128GB) */
    ZONE_HIGHMEM,         /**< High memory (> 128GB) */
    ZONE_MAX
} MemoryZone_t;

/**
 * @brief Memory Zone Descriptor
 */
typedef struct
{
    uint64_t            start;             /**< Zone start physical addr */
    uint64_t            end;               /**< Zone end physical addr */
    uint64_t            present;           /**< Present pages */
    PageFrame_t        *free_list[MAX_ORDER]; /**< Free lists by order */
    atomic_uint_fast32_t lock;             /**< Zone lock */
} MemoryZone_t;
```

---

## 3. API Interface Definition

### 3.1 Page Table Management

```c
/**
 * @brief Initialize MMU subsystem
 * @return 0 on success, negative error code on failure
 *
 * @note Must be called before any MMU operation
 * @warning Must be called with MMU disabled
 */
int32_t mmu_init(void);

/**
 * @brief Create new address space
 * @return Pointer to AddressSpace, or NULL on failure
 *
 * @note Allocates new PGD
 */
AddressSpace_t* mmu_create_address_space(void);

/**
 * @brief Destroy address space
 * @param as Address space to destroy
 *
 * @note Frees all page tables and PGD
 */
void mmu_destroy_address_space(AddressSpace_t *as);

/**
 * @brief Map virtual memory to physical memory
 * @param as Address space
 * @param virt_addr Virtual address (must be page-aligned)
 * @param phys_addr Physical address (must be page-aligned)
 * @param size Size in bytes (must be multiple of page size)
 * @param flags Protection flags (see VMA_*)
 * @return 0 on success, negative error code on failure
 *
 * @note Uses 4KB pages by default
 * @note Automatically uses block mappings where possible
 */
int32_t mmu_map(AddressSpace_t *as,
                uint64_t virt_addr,
                uint64_t phys_addr,
                uint64_t size,
                uint64_t flags);

/**
 * @brief Unmap virtual memory
 * @param as Address space
 * @param virt_addr Virtual address (must be page-aligned)
 * @param size Size in bytes (must be multiple of page size)
 * @return 0 on success, negative error code on failure
 *
 * @note Frees page tables if they become empty
 */
int32_t mmu_unmap(AddressSpace_t *as,
                  uint64_t virt_addr,
                  uint64_t size);

/**
 * @brief Change memory protection flags
 * @param as Address space
 * @param virt_addr Virtual address
 * @param size Size
 * @param new_flags New flags
 * @return 0 on success, negative error code on failure
 */
int32_t mmu_protect(AddressSpace_t *as,
                    uint64_t virt_addr,
                    uint64_t size,
                    uint64_t new_flags);
```

### 3.2 Page Allocation

```c
/**
 * @brief Allocate page frames
 * @param zone Memory zone (ZONE_DMA, ZONE_NORMAL, ZONE_HIGHMEM)
 * @param order Allocation order (0 = 1 page, 1 = 2 pages, ...)
 * @return Physical address of allocated page, or 0 on failure
 *
 * @note Uses buddy system allocator
 * @note Physical address is page-aligned
 */
uint64_t page_alloc(MemoryZone_t zone, uint32_t order);

/**
 * @brief Free page frames
 * @param phys_addr Physical address of page (must be page-aligned)
 * @param order Allocation order
 *
 * @note Merges with buddy if free
 */
void page_free(uint64_t phys_addr, uint32_t order);

/**
 * @brief Get virtual address of physical page
 * @param phys_addr Physical address
 * @return Kernel virtual address
 *
 * @note Uses direct mapping region
 */
void* phys_to_virt(uint64_t phys_addr);

/**
 * @brief Get physical address of virtual address
 * @param virt Virtual address
 * @return Physical address
 */
uint64_t virt_to_phys(void *virt);
```

### 3.3 Page Table Query

```c
/**
 * @brief Translate virtual address to physical address
 * @param as Address space
 * @param virt_addr Virtual address
 * @return Physical address, or 0 if not mapped
 */
uint64_t mmu_lookup(AddressSpace_t *as, uint64_t virt_addr);

/**
 * @brief Query memory protection flags
 * @param as Address space
 * @param virt_addr Virtual address
 * @return Protection flags, or 0 if not mapped
 */
uint64_t mmu_query_flags(AddressSpace_t *as, uint64_t virt_addr);
```

---

## 4. Algorithm Implementation Details

### 4.1 4-Level Page Table Walk

```c
/**
 * @brief Walk page tables and return PTE
 * @param as Address space
 * @param virt_addr Virtual address
 * @param alloc If true, allocate missing page tables
 * @return Pointer to PTE, or NULL if not present/allocation failed
 *
 * @note ARMv8-A 4KB page granularity:
 *       - L0 (PGD): [48:39] = 9 bits, 512 entries
 *       - L1 (PUD): [38:30] = 9 bits, 512 entries
 *       - L2 (PMD): [29:21] = 9 bits, 512 entries
 *       - L3 (PTE): [20:12] = 9 bits, 512 entries
 *       - Offset:  [11:0]  = 12 bits, 4096 bytes
 */
static pte_t* page_table_walk(AddressSpace_t *as,
                               uint64_t virt_addr,
                               bool alloc)
{
    pte_t *pgd;
    pte_t *pud;
    pte_t *pmd;
    pte_t *pte;

    /* Extract indices */
    uint64_t pgd_idx = (virt_addr >> 39U) & 0x1FFU;
    uint64_t pud_idx = (virt_addr >> 30U) & 0x1FFU;
    uint64_t pmd_idx = (virt_addr >> 21U) & 0x1FFU;
    uint64_t pte_idx = (virt_addr >> 12U) & 0x1FFU;

    /* Get PGD */
    pgd = (pte_t *)phys_to_virt(as->page_table);
    if (pgd == NULL)
    {
        return NULL;
    }

    /* Walk L1 (PUD) */
    if ((pgd[pgd_idx] & PAGE_VALID) == 0U)
    {
        if (!alloc)
        {
            return NULL;
        }
        /* Allocate new PUD */
        pud = (pte_t *)page_alloc(ZONE_NORMAL, 0);
        if (pud == NULL)
        {
            return NULL;
        }
        /* Clear PUD */
        memset(pud, 0, PAGE_SIZE);
        /* Set PGD entry */
        pgd[pgd_idx] = virt_to_phys(pud) | PAGE_TABLE_ATTR;
    }
    else
    {
        pud = (pte_t *)phys_to_virt(pgd[pgd_idx] & ~0xFFFU);
    }

    /* Walk L2 (PMD) */
    if ((pud[pud_idx] & PAGE_VALID) == 0U)
    {
        if (!alloc)
        {
            return NULL;
        }
        /* Allocate new PMD */
        pmd = (pte_t *)page_alloc(ZONE_NORMAL, 0);
        if (pmd == NULL)
        {
            return NULL;
        }
        memset(pmd, 0, PAGE_SIZE);
        pud[pud_idx] = virt_to_phys(pmd) | PAGE_TABLE_ATTR;
    }
    else
    {
        pmd = (pte_t *)phys_to_virt(pud[pud_idx] & ~0xFFFU);
    }

    /* Check for 1GB block mapping */
    if ((pmd[pmd_idx] & PAGE_TABLE) == 0U)
    {
        /* Block mapping: return PMD entry */
        return &pmd[pmd_idx];
    }

    /* Walk L3 (PTE) */
    if ((pmd[pmd_idx] & PAGE_VALID) == 0U)
    {
        if (!alloc)
        {
            return NULL;
        }
        /* Allocate new PTE */
        pte = (pte_t *)page_alloc(ZONE_NORMAL, 0);
        if (pte == NULL)
        {
            return NULL;
        }
        memset(pte, 0, PAGE_SIZE);
        pmd[pmd_idx] = virt_to_phys(pte) | PAGE_TABLE_ATTR;
    }
    else
    {
        pte = (pte_t *)phys_to_virt(pmd[pmd_idx] & ~0xFFFU);
    }

    /* Check for 2MB block mapping */
    if ((pte[pte_idx] & PAGE_TABLE) == 0U)
    {
        /* Block mapping: return PTE entry */
        return &pte[pte_idx];
    }

    /* Return PTE entry */
    return &pte[pte_idx];
}
```

### 4.2 Virtual Memory Mapping

```c
/**
 * @brief Map virtual memory range
 * @param as Address space
 * @param virt_addr Virtual address (page-aligned)
 * @param phys_addr Physical address (page-aligned)
 * @param size Size (multiple of page size)
 * @param flags Protection flags
 * @return 0 on success, negative error code on failure
 */
int32_t mmu_map(AddressSpace_t *as,
                uint64_t virt_addr,
                uint64_t phys_addr,
                uint64_t size,
                uint64_t flags)
{
    uint64_t virt_end = virt_addr + size;
    uint64_t phys = phys_addr;
    uint64_t virt;
    pte_t *pte;

    /* Validate alignment */
    if ((virt_addr & PAGE_MASK) != 0U)
    {
        return -EINVAL;
    }
    if ((phys_addr & PAGE_MASK) != 0U)
    {
        return -EINVAL;
    }
    if ((size & PAGE_MASK) != 0U)
    {
        return -EINVAL;
    }

    /* Lock address space */
    atomic_lock(&as->lock);

    /* Map each page */
    for (virt = virt_addr; virt < virt_end; virt += PAGE_SIZE)
    {
        /* Walk page tables (allocate if needed) */
        pte = page_table_walk(as, virt, true);
        if (pte == NULL)
        {
            atomic_unlock(&as->lock);
            return -ENOMEM;
        }

        /* Check if already mapped */
        if ((*pte & PAGE_VALID) != 0U)
        {
            atomic_unlock(&as->lock);
            return -EEXIST;
        }

        /* Create PTE */
        *pte = phys | flags | PAGE_VALID;

        phys += PAGE_SIZE;
    }

    atomic_unlock(&as->lock);

    /* Flush TLB */
    __asm__ volatile("tlbi vmalle1is");
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");

    return 0;
}
```

### 4.3 Block Mapping Optimization

```c
/**
 * @brief Try to create 2MB block mapping
 * @param as Address space
 * @param virt_addr Virtual address (2MB-aligned)
 * @param phys_addr Physical address (2MB-aligned)
 * @param flags Protection flags
 * @return 0 on success, negative error code on failure
 *
 * @note Falls back to 4KB mapping if block mapping fails
 */
static int32_t try_map_block(AddressSpace_t *as,
                              uint64_t virt_addr,
                              uint64_t phys_addr,
                              uint64_t flags)
{
    pte_t *pgd;
    pte_t *pud;
    pte_t *pmd;

    /* Must be 2MB aligned */
    if ((virt_addr & 0x1FFFFFU) != 0U)
    {
        return -EINVAL;
    }
    if ((phys_addr & 0x1FFFFFU) != 0U)
    {
        return -EINVAL;
    }

    /* Extract indices */
    uint64_t pgd_idx = (virt_addr >> 39U) & 0x1FFU;
    uint64_t pud_idx = (virt_addr >> 30U) & 0x1FFU;
    uint64_t pmd_idx = (virt_addr >> 21U) & 0x1FFU;

    /* Get PGD */
    pgd = (pte_t *)phys_to_virt(as->page_table);

    /* Walk to PMD level */
    pud = (pte_t *)phys_to_virt(pgd[pgd_idx] & ~0xFFFU);
    pmd = (pte_t *)phys_to_virt(pud[pud_idx] & ~0xFFFU);

    /* Create 2MB block mapping */
    pmd[pmd_idx] = phys_addr | flags | PAGE_BLOCK;

    return 0;
}
```

### 4.4 Page Fault Handler

```c
/**
 * @brief Page fault handler (called from exception vector)
 * @param fault_addr Faulting virtual address
 * @param fault_status ESR_EL1.FSC (Fault Status Code)
 * @return 1 if handled, 0 if not handled (fatal)
 *
 * @note ARMv8-A Fault Status Codes (FSC):
 *       0x04: Translation fault, level 0
 *       0x05: Translation fault, level 1
 *       0x06: Translation fault, level 2
 *       0x07: Translation fault, level 3
 *       0x08: Access flag fault, level 0
 *       ...
 *       0x0C: Permission fault, level 0
 *       ...
 *       0x0F: Permission fault, level 3
 */
int32_t page_fault_handler(uint64_t fault_addr, uint64_t fault_status)
{
    AddressSpace_t *as;
    uint32_t fsc = (uint32_t)(fault_status & 0x3FU);

    /* Get current address space */
    as = current_task->address_space;

    /* Handle translation fault (page not present) */
    if ((fsc >= 0x04U) && (fsc <= 0x07U))
    {
        /* TODO: Demand paging */
        return 0;  /* Not handled, fatal */
    }

    /* Handle permission fault */
    if ((fsc >= 0x0CU) && (fsc <= 0x0FU))
    {
        /* Access violation */
        log_err("Page fault: permission violation at 0x%llx\n", fault_addr);
        send_sigsegv(current_task);
        return 1;  /* Handled */
    }

    /* Other faults are fatal */
    return 0;
}
```

### 4.5 TLB Management

```c
/**
 * @brief Invalidate TLB entry for specific address
 * @param as Address space
 * @param virt_addr Virtual address
 */
static inline void tlb_invalidate_page(AddressSpace_t *as, uint64_t virt_addr)
{
    (void)as;  /* ASID not used yet */

    uint64_t addr = virt_addr >> 12U;
    __asm__ volatile("tlbi vae1is, %0" :: "r"(addr));
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");
}

/**
 * @brief Invalidate entire TLB
 */
static inline void tlb_invalidate_all(void)
{
    __asm__ volatile("tlbi vmalle1is");
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");
}
```

### 4.6 Buddy System Allocator

```c
/**
 * @brief Allocate page frames using buddy system
 * @param zone Memory zone
 * @param order Allocation order (log2 of page count)
 * @return Physical address, or 0 on failure
 *
 * @note Maximum order is MAX_ORDER (typically 10 = 1024 pages)
 */
uint64_t page_alloc(MemoryZone_t zone, uint32_t order)
{
    MemoryZone_t *z = &g_zones[zone];
    PageFrame_t *page;
    PageFrame_t *buddy;
    uint32_t current_order;

    /* Validate order */
    if (order >= MAX_ORDER)
    {
        return 0;
    }

    atomic_lock(&z->lock);

    /* Find free block of required order or higher */
    for (current_order = order; current_order < MAX_ORDER; current_order++)
    {
        if (z->free_list[current_order] != NULL)
        {
            break;
        }
    }

    if (current_order >= MAX_ORDER)
    {
        atomic_unlock(&z->lock);
        return 0;  /* Out of memory */
    }

    /* Remove from free list */
    page = z->free_list[current_order];
    z->free_list[current_order] = page->next;

    /* Split blocks until we reach required order */
    while (current_order > order)
    {
        current_order--;

        /* Calculate buddy address */
        uint64_t buddy_phys = page->phys_addr ^ (1UL << (PAGE_SHIFT + current_order));
        buddy = &g_page_frames[buddy_phys >> PAGE_SHIFT];

        /* Initialize buddy */
        buddy->phys_addr = buddy_phys;
        buddy->order = current_order;
        buddy->ref_count = 0;

        /* Add buddy to free list */
        buddy->next = z->free_list[current_order];
        z->free_list[current_order] = buddy;
    }

    /* Mark page as used */
    page->order = order;
    page->ref_count = 1;

    atomic_unlock(&z->lock);

    return page->phys_addr;
}

/**
 * @brief Free page frames
 * @param phys_addr Physical address
 * @param order Allocation order
 */
void page_free(uint64_t phys_addr, uint32_t order)
{
    MemoryZone_t *zone;
    PageFrame_t *page;
    PageFrame_t *buddy;
    uint64_t buddy_phys;

    /* Determine zone */
    zone = find_zone(phys_addr);
    if (zone == NULL)
    {
        return;
    }

    page = &g_page_frames[phys_addr >> PAGE_SHIFT];

    atomic_lock(&zone->lock);

    /* Merge with free buddies */
    while (order < MAX_ORDER)
    {
        /* Calculate buddy address */
        buddy_phys = phys_addr ^ (1UL << (PAGE_SHIFT + order));
        buddy = &g_page_frames[buddy_phys >> PAGE_SHIFT];

        /* Check if buddy is free */
        if ((buddy->ref_count != 0) || (buddy->order != order))
        {
            break;
        }

        /* Remove buddy from free list */
        remove_from_free_list(zone, buddy, order);

        /* Merge */
        if (buddy_phys < phys_addr)
        {
            phys_addr = buddy_phys;
            page = buddy;
        }
        order++;
    }

    /* Add merged block to free list */
    page->phys_addr = phys_addr;
    page->order = order;
    page->ref_count = 0;
    page->next = zone->free_list[order];
    zone->free_list[order] = page;

    atomic_unlock(&zone->lock);
}
```

---

## 5. Performance Requirements

### 5.1 Timing Constraints

| Operation | Maximum Latency |
|-----------|-----------------|
| **Page Allocation** | 1 μs |
| **Page Free** | 500 ns |
| **Page Table Walk** | 200 ns |
| **TLB Invalidation** | 100 ns |
| **Page Fault Handler** | 5 μs |
| **Memory Mapping** | 10 μs per page |

### 5.2 Memory Constraints

| Resource | Limit |
|----------|-------|
| **Page Table Memory** | ≤ 2% of RAM |
| **Page Frame Descriptors** | 64 bytes per page |
| **VMA Structures** | ≤ 64 bytes per mapping |

### 5.3 TLB Efficiency

- **Block Mapping Rate**: > 80% for kernel mappings
- **TLB Miss Rate**: < 1% (normal workload)

---

## 6. MISRA-C:2012 Compliance

### 6.1 Critical Rules

| Rule | Requirement |
|------|-------------|
| Rule 11.1 | No pointer-integer conversion except uintptr_t |
| Rule 11.4 | No implicit pointer conversions |
| Rule 11.6 | Cast must preserve const/volatile |
| Rule 18.1 | Pointer arithmetic limited to array bounds |

### 6.2 Type Safety

```c
/* ✅ Correct: Use uintptr_t for pointer-integer conversion */
uint64_t paddr = (uint64_t)virt_to_phys(virt);

/* ❌ Wrong: Direct cast */
uint64_t paddr = (uint64_t)virt;
```

### 6.3 Runtime Checks

```c
/* Compile-time assertions */
STATIC_ASSERT(PAGE_SIZE == 4096U, page_size_mismatch);
STATIC_ASSERT(sizeof(pte_t) == 8U, pte_size_mismatch);
```

---

## 7. Testing Strategy

### 7.1 Unit Tests

| Test Case | Description |
|-----------|-------------|
| **TC-MM-001** | Page allocation and deallocation |
| **TC-MM-002** | Buddy system coalescing |
| **TC-MM-003** | Page table walk (all levels) |
| **TC-MM-004** | 4KB page mapping |
| **TC-MM-005** | 2MB block mapping |
| **TC-MM-006** | 1GB block mapping |
| **TC-MM-007** | TLB invalidation |
| **TC-MM-008** | Page fault handling |
| **TC-MM-009** | Memory protection (RO, NX) |
| **TC-MM-010** | Address space isolation |

### 7.2 Integration Tests

| Test Case | Description |
|-----------|-------------|
| **TC-MM-INT-001** | MMU with scheduler |
| **TC-MM-INT-002** | MMU with sync primitives |
| **TC-MM-INT-003** | User-kernel transitions |
| **TC-MM-INT-004** | DMA memory mapping |

### 7.3 Performance Tests

| Test Case | Metric | Target |
|-----------|--------|--------|
| **TC-MM-PERF-001** | Page allocation throughput | > 1M pages/sec |
| **TC-MM-PERF-002** | TLB miss rate | < 1% |
| **TC-MM-PERF-003** | Page table walk latency | < 200 ns |

### 7.4 Coverage Requirements

- **Statement Coverage**: > 95%
- **Branch Coverage**: > 90%
- **MC/DC Coverage**: > 85% (critical functions)

---

## 8. Configuration Options

### 8.1 MenuConfig Options

```kconfig
config MMU
    bool "MMU Support"
    default y

config PAGE_SIZE
    int "Page size (bytes)"
    default 4096
    depends on MMU

config MAX_ORDER
    int "Maximum allocation order"
    range 8 12
    default 10
    depends on MMU
    help
      Maximum order for buddy system.
      10 = 1024 pages (4MB with 4KB pages)

config ZONE_DMA
    bool "DMA memory zone"
    default y
    depends on MMU

config BLOCK_MAPPING_2MB
    bool "2MB block mapping"
    default y
    depends on MMU

config BLOCK_MAPPING_1GB
    bool "1GB block mapping"
    default y
    depends on MMU
```

---

## 9. Error Handling

### 9.1 Error Codes

| Error Code | Description |
|------------|-------------|
| `ERROR_INVALID_ADDRESS` | Address not aligned |
| `ERROR_ALREADY_MAPPED` | Virtual address already mapped |
| `ERROR_NOT_MAPPED` | Virtual address not mapped |
| `ERROR_OUT_OF_MEMORY` | Page allocation failed |
| `ERROR_INVALID_FLAGS` | Invalid protection flags |

### 9.2 Error Recovery

- **Page Allocation Failure**: Return NULL (caller handles)
- **Page Table Allocation Failure**: Return error, rollback
- **Page Fault (Access Violation)**: Send SIGSEGV to task
- **TLB Parity Error**: Trigger system panic (fatal)

---

## 10. Traceability

### 10.1 Requirements Traceability

| LLD Section | HLD Section | Plan.md Section |
|-------------|-------------|-----------------|
| Page Table Hierarchy | 4.5 MMU Management | 4.5.1 |
| Buddy System | 4.2 Memory Management | - |
| Block Mapping | 4.5.0 Early MMU | 4.5.0 |
| Page Fault Handler | 4.5.2 Page Fault | 4.5.2 |

### 10.2 Test Coverage Traceability

| Test Case | Requirement |
|-----------|-------------|
| TC-MM-004 | MM-001: 4KB mapping |
| TC-MM-005 | MM-002: 2MB block mapping |
| TC-MM-PERF-001 | NFR-001: Allocation < 1μs |

---

## Appendix A: ARMv8-A Page Table Format

### A.1 Level 3 Page Table Entry (4KB page)

```
  63   62 61      52 51 50 49 48 47                 12 11 10 9 8 7 6 5 4 3 2 1 0
 +-----+---+---------+--+--+--+--+--------------------+--+--+--+-----+-+-+-+-+
 | UXN | PXN| SBZ     |DB|Cont|   Output Address     |NG|AF|SH|  AP  |AttrIndx|0|
 +-----+---+---------+--+--+--+--+--------------------+--+--+--+-----+-+-+-+-+
```

### A.2 Level 2 Block Descriptor (2MB block)

```
  63   62 61      52 51 50 49 48 47                 12 11 10 9 8 7 6 5 4 3 2 1 0
 +-----+---+---------+--+--+--+--+--------------------+--+--+--+-----+-+-+-+-+
 | UXN | PXN| SBZ     |DB|Cont|   Output Address     |NG|AF|SH|  AP  |AttrIndx|1|
 +-----+---+---------+--+--+--+--+--------------------+--+--+--+-----+-+-+-+-+
```

### A.3 Table Descriptor

```
  63   62            52 51                     12 11 10 9 8 7 6 5 4 3 2 1 0
 +-----+---------------+------------------------+--+--+--+-----+-+-+-+-+
 | UXN | PXN| SBZ      |   Next Table Address    |NG|AF|SH|  AP  |AttrIndx|3|
 +-----+---------------+------------------------+--+--+--+-----+-+-+-+-+
```

---

**Document End**

---

# 附录 C: 同步机制详细设计

> 原始文档: LLD-003-Synchronization.md

# LLD-003: Synchronization and Communication Low-Level Design
## 1. Module Overview

### 1.1 Purpose
The Synchronization and Communication module provides thread-safe primitives for task coordination, including mutexes (with priority inheritance), semaphores, spinlocks, event flags, and message queues. Designed for safety-critical systems with bounded execution times and deterministic behavior.

### 1.2 Scope
This document describes the low-level design of:
- Mutex locks with priority inheritance protocol
- Counting semaphores
- Ticket spinlocks (fair SMP locks)
- Event flag groups
- Message queues
- Condition variables

### 1.3 References
- POSIX 1003.1-2017 (PSE52)
- MISRA-C:2012 Guidelines
- HLD-003: Kernel Module Design
- plan.md Section 4.2 (Data Structures)

---

## 2. Data Structure Design

### 2.1 Mutex Structure

```c
/**
 * @brief Mutex Lock (with priority inheritance)
 * @note MISRA-C:2012 compliant
 * @note 64-byte cache-line aligned for SMP
 */
typedef struct __attribute__((aligned(64))) Mutex
{
    atomic_uint_fast32_t lock;        /**< Lock state (0=unlocked, 1=locked) */
    TCB_t             *owner;         /**< Current owner task */
    uint8_t             ceiling;       /**< Priority ceiling (0-255) */
    uint8_t             original_prio; /**< Owner's original priority */
    TaskList_t          wait_list;     /**< Blocked tasks */
    uint32_t            nest_count;    /**< Nesting count (for recursive) */
    uint32_t            flags;         /**< Flags (see MUTEX_*) */

} Mutex_t;

/* Mutex flags */
#define MUTEX_PRIORITY_INHERITANCE  (1U << 0)  /**< Enable PI protocol */
#define MUTEX_PRIORITY_CEILING      (1U << 1)  /**< Enable priority ceiling */
#define MUTEX_RECURSIVE             (1U << 2)  /**< Allow recursive locking */

/* Compile-time validation */
STATIC_ASSERT(sizeof(Mutex_t) <= 64U, Mutex_size_exceeds_cache_line);
```

### 2.2 Semaphore Structure

```c
/**
 * @brief Counting Semaphore
 * @note POSIX sem_t compatible
 */
typedef struct
{
    atomic_uint_fast32_t count;       /**< Semaphore count */
    atomic_uint_fast32_t max_count;   /**< Maximum count */
    TaskList_t          wait_list;    /**< Blocked tasks */
    uint32_t            flags;        /**< Flags (see SEM_*) */

} Semaphore_t;

/* Semaphore flags */
#define SEM_FIFO               (1U << 0)  /**< FIFO ordering (default: priority) */
#define SEM_BINARY             (1U << 1)  /**< Binary semaphore (max=1) */
```

### 2.3 Ticket Spinlock Structure

```c
/**
 * @brief Ticket Spinlock (fair FIFO lock)
 * @note ARM64 optimized with WFE instruction
 * @note Safe for SMP usage
 */
typedef struct
{
    atomic_uint_fast16_t next_ticket;    /**< Next ticket to serve */
    atomic_uint_fast16_t serving_ticket; /**< Currently serving ticket */

} TicketLock_t;

/* Compile-time validation */
STATIC_ASSERT(sizeof(TicketLock_t) == 4U, TicketLock_size_mismatch);
```

### 2.4 Event Flag Group Structure

```c
/**
 * @brief Event Flag Group
 * @note 32-bit event flags (32 independent events)
 */
typedef struct
{
    atomic_uint_fast32_t flags;         /**< Event flags (bitmask) */
    TaskList_t          wait_list;      /**< Blocked tasks */

} EventGroup_t;

/* Event wait types */
#define EV_WAIT_ANY            (1U << 0)  /**< Wait for any flag */
#define EV_WAIT_ALL            (1U << 1)  /**< Wait for all flags */
#define EV_AUTO_CLEAR         (1U << 2)  /**< Auto-clear on exit */
```

### 2.5 Message Queue Structure

```c
/**
 * @brief Message Queue (fixed-size messages)
 * @note Ring buffer implementation
 */
typedef struct
{
    void               *buffer;        /**< Ring buffer */
    uint32_t            capacity;      /**< Queue capacity (messages) */
    uint32_t            msg_size;      /**< Message size (bytes) */
    atomic_uint_fast32_t head;         /**< Write index */
    atomic_uint_fast32_t tail;         /**< Read index */
    atomic_uint_fast32_t count;        /**< Current message count */
    atomic_uint_fast32_t lock;         /**< Queue lock */

    TaskList_t          tx_wait_list;  /**< Blocked senders */
    TaskList_t          rx_wait_list;  /**< Blocked receivers */

} MessageQueue_t;

/* Compile-time validation */
STATIC_ASSERT(sizeof(MessageQueue_t) <= 128U, MessageQueue_too_large);
```

### 2.6 Condition Variable Structure

```c
/**
 * @brief Condition Variable (POSIX pthread_cond_t)
 * @note Must be used with mutex
 */
typedef struct
{
    TaskList_t          wait_list;     /**< Blocked tasks */
    uint32_t            wait_count;    /**< Number of waiting tasks */

} ConditionVariable_t;
```

---

## 3. API Interface Definition

### 3.1 Mutex API

```c
/**
 * @brief Initialize mutex
 * @param mutex Mutex to initialize
 * @param flags Flags (MUTEX_PRIORITY_INHERITANCE, MUTEX_RECURSIVE, ...)
 * @return 0 on success, negative error code on failure
 *
 * @note Must be called before first use
 */
int32_t mutex_init(Mutex_t *mutex, uint32_t flags);

/**
 * @brief Lock mutex (blocking)
 * @param mutex Mutex to lock
 * @return 0 on success, negative error code on failure
 *
 * @note Blocks if mutex already locked
 * @note Supports recursive locking if MUTEX_RECURSIVE set
 * @warning Do not call from interrupt context
 */
int32_t mutex_lock(Mutex_t *mutex);

/**
 * @brief Try to lock mutex (non-blocking)
 * @param mutex Mutex to lock
 * @return 0 on success, -EBUSY if mutex locked
 */
int32_t mutex_trylock(Mutex_t *mutex);

/**
 * @brief Unlock mutex
 * @param mutex Mutex to unlock
 * @return 0 on success, negative error code on failure
 *
 * @note Wakes highest-priority waiting task
 * @warning Must be called by owner task
 */
int32_t mutex_unlock(Mutex_t *mutex);

/**
 * @brief Destroy mutex
 * @param mutex Mutex to destroy
 * @return 0 on success, negative error code on failure
 *
 * @warning No operations allowed after destroy
 */
int32_t mutex_destroy(Mutex_t *mutex);
```

### 3.2 Semaphore API

```c
/**
 * @brief Initialize semaphore
 * @param sem Semaphore to initialize
 * @param initial_count Initial count
 * @param max_count Maximum count
 * @param flags Flags (SEM_FIFO, SEM_BINARY, ...)
 * @return 0 on success, negative error code on failure
 */
int32_t sem_init(Semaphore_t *sem,
                 uint32_t initial_count,
                 uint32_t max_count,
                 uint32_t flags);

/**
 * @brief Wait on semaphore (P operation)
 * @param sem Semaphore
 * @param timeout_ms Timeout in milliseconds (0 = infinite)
 * @return 0 on success, -ETIMEDOUT on timeout
 *
 * @note Decrements count if > 0, else blocks
 * @warning Do not call from interrupt context
 */
int32_t sem_wait(Semaphore_t *sem, uint32_t timeout_ms);

/**
 * @brief Signal semaphore (V operation)
 * @param sem Semaphore
 * @return 0 on success, negative error code on failure
 *
 * @note Increments count, wakes waiting task
 * @note Safe to call from interrupt context
 */
int32_t sem_post(Semaphore_t *sem);

/**
 * @brief Get current semaphore count
 * @param sem Semaphore
 * @return Current count
 */
uint32_t sem_get_count(Semaphore_t *sem);

/**
 * @brief Destroy semaphore
 * @param sem Semaphore
 * @return 0 on success, negative error code on failure
 */
int32_t sem_destroy(Semaphore_t *sem);
```

### 3.3 Spinlock API

```c
/**
 * @brief Acquire ticket spinlock
 * @param lock Spinlock
 *
 * @note Blocks until ticket matches serving number
 * @note Uses WFE instruction to reduce power
 * @note Safe for SMP usage
 * @warning Do not sleep while holding spinlock
 */
static inline void spin_lock(TicketLock_t *lock)
{
    uint16_t my_ticket = atomic_fetch_add_explicit(
        &lock->next_ticket, 1U, memory_order_acquire);

    while (atomic_load_explicit(&lock->serving_ticket,
                                memory_order_acquire) != my_ticket)
    {
        /* Wait for event (low-power) */
        __asm__ volatile("wfe");
    }

    /* Acquire barrier */
    atomic_thread_fence(memory_order_acquire);
}

/**
 * @brief Release ticket spinlock
 * @param lock Spinlock
 */
static inline void spin_unlock(TicketLock_t *lock)
{
    /* Release barrier */
    atomic_thread_fence(memory_order_release);

    atomic_fetch_add_explicit(&lock->serving_ticket, 1U,
                             memory_order_release);

    /* Send event to waiting CPUs */
    __asm__ volatile("sev");
}

/**
 * @brief Try to acquire spinlock (non-blocking)
 * @param lock Spinlock
 * @return 0 on success, -EBUSY if lock held
 */
static inline int32_t spin_trylock(TicketLock_t *lock)
{
    uint16_t my_ticket = atomic_load_explicit(&lock->next_ticket,
                                              memory_order_acquire);
    uint16_t serving = atomic_load_explicit(&lock->serving_ticket,
                                            memory_order_acquire);

    if (my_ticket != serving)
    {
        return -EBUSY;
    }

    /* Try to claim ticket */
    uint16_t expected = my_ticket;
    if (!atomic_compare_exchange_strong_explicit(
            &lock->next_ticket, &expected, my_ticket + 1U,
            memory_order_acquire, memory_order_acquire))
    {
        return -EBUSY;
    }

    return 0;
}
```

### 3.4 Event Flag API

```c
/**
 * @brief Create event flag group
 * @return Event group handle, or NULL on failure
 */
EventGroup_t* event_create(void);

/**
 * @brief Wait for event flags
 * @param group Event group
 * @param flags Flags to wait for (bitmask)
 * @param wait_type EV_WAIT_ANY or EV_WAIT_ALL
 * @param timeout_ms Timeout (0 = infinite)
 * @return 0 on success, -ETIMEDOUT on timeout
 *
 * @note Clears flags if EV_AUTO_CLEAR set
 */
int32_t event_wait(EventGroup_t *group,
                   uint32_t flags,
                   uint32_t wait_type,
                   uint32_t timeout_ms);

/**
 * @brief Set event flags
 * @param group Event group
 * @param flags Flags to set (bitmask)
 * @return Previous flags value
 *
 * @note Wakes all tasks whose wait condition is satisfied
 * @note Safe to call from interrupt context
 */
uint32_t event_set(EventGroup_t *group, uint32_t flags);

/**
 * @brief Clear event flags
 * @param group Event group
 * @param flags Flags to clear (bitmask)
 * @return Previous flags value
 */
uint32_t event_clear(EventGroup_t *group, uint32_t flags);

/**
 * @brief Destroy event flag group
 * @param group Event group
 * @return 0 on success, negative error code on failure
 */
int32_t event_destroy(EventGroup_t *group);
```

### 3.5 Message Queue API

```c
/**
 * @brief Create message queue
 * @param capacity Queue capacity (messages)
 * @param msg_size Message size (bytes)
 * @return Queue handle, or NULL on failure
 *
 * @note Allocates ring buffer
 */
MessageQueue_t* msgq_create(uint32_t capacity, uint32_t msg_size);

/**
 * @brief Send message to queue
 * @param queue Message queue
 * @param msg Message buffer
 * @param timeout_ms Timeout (0 = infinite)
 * @return 0 on success, -ETIMEDOUT on timeout, -EAGAIN if queue full
 *
 * @note Blocks if queue full
 * @warning Do not call from interrupt context
 */
int32_t msgq_send(MessageQueue_t *queue,
                  const void *msg,
                  uint32_t timeout_ms);

/**
 * @brief Receive message from queue
 * @param queue Message queue
 * @param msg Message buffer
 * @param timeout_ms Timeout (0 = infinite)
 * @return 0 on success, -ETIMEDOUT on timeout
 *
 * @note Blocks if queue empty
 * @warning Do not call from interrupt context
 */
int32_t msgq_receive(MessageQueue_t *queue,
                     void *msg,
                     uint32_t timeout_ms);

/**
 * @brief Send message (non-blocking)
 * @param queue Message queue
 * @param msg Message buffer
 * @return 0 on success, -EAGAIN if queue full
 *
 * @note Safe to call from interrupt context
 */
int32_t msgq_try_send(MessageQueue_t *queue, const void *msg);

/**
 * @brief Receive message (non-blocking)
 * @param queue Message queue
 * @param msg Message buffer
 * @return 0 on success, -EAGAIN if queue empty
 *
 * @note Safe to call from interrupt context
 */
int32_t msgq_try_receive(MessageQueue_t *queue, void *msg);

/**
 * @brief Destroy message queue
 * @param queue Message queue
 * @return 0 on success, negative error code on failure
 *
 * @warning Wakes all blocked tasks with error
 */
int32_t msgq_destroy(MessageQueue_t *queue);
```

### 3.6 Condition Variable API

```c
/**
 * @brief Initialize condition variable
 * @param cond Condition variable
 * @return 0 on success, negative error code on failure
 */
int32_t cond_init(ConditionVariable_t *cond);

/**
 * @brief Wait on condition variable
 * @param cond Condition variable
 * @param mutex Mutex (must be locked)
 * @return 0 on success, negative error code on failure
 *
 * @note Atomically unlocks mutex and blocks
 * @note Re-acquires mutex before returning
 * @warning Must be called with mutex locked
 */
int32_t cond_wait(ConditionVariable_t *cond, Mutex_t *mutex);

/**
 * @brief Signal condition variable (wake one task)
 * @param cond Condition variable
 * @return 0 on success, negative error code on failure
 *
 * @note Wakes highest-priority waiting task
 */
int32_t cond_signal(ConditionVariable_t *cond);

/**
 * @brief Broadcast condition variable (wake all tasks)
 * @param cond Condition variable
 * @return 0 on success, negative error code on failure
 *
 * @note Wakes all waiting tasks
 */
int32_t cond_broadcast(ConditionVariable_t *cond);

/**
 * @brief Destroy condition variable
 * @param cond Condition variable
 * @return 0 on success, negative error code on failure
 */
int32_t cond_destroy(ConditionVariable_t *cond);
```

---

## 4. Algorithm Implementation Details

### 4.1 Mutex Lock with Priority Inheritance

```c
/**
 * @brief Lock mutex (with priority inheritance)
 * @param mutex Mutex
 * @return 0 on success, negative error code on failure
 */
int32_t mutex_lock(Mutex_t *mutex)
{
    TCB_t *current = get_current_task();
    uint32_t cpu_id = get_cpu_id();

    /* Try to acquire lock */
    if (atomic_compare_exchange_strong_explicit(
            &mutex->lock, &(uint32_t){0U}, 1U,
            memory_order_acquire, memory_order_acquire))
    {
        /* Lock acquired */
        mutex->owner = current;
        mutex->nest_count++;

        /* Enable priority inheritance if needed */
        if ((mutex->flags & MUTEX_PRIORITY_INHERITANCE) != 0U)
        {
            mutex->original_prio = current->priority;
        }

        return 0;
    }

    /* Lock held by another task, check for recursive lock */
    if ((mutex->flags & MUTEX_RECURSIVE) != 0U)
    {
        if (mutex->owner == current)
        {
            /* Recursive lock */
            mutex->nest_count++;
            return 0;
        }
    }

    /* Priority inheritance: boost owner's priority */
    if ((mutex->flags & MUTEX_PRIORITY_INHERITANCE) != 0U)
    {
        TCB_t *owner = mutex->owner;

        /* Boost owner's priority if current has higher priority */
        if (current->priority < owner->priority)
        {
            scheduler_lock();

            /* Save owner's original priority (first inheritance) */
            if (owner->priority == mutex->original_prio)
            {
                mutex->original_prio = owner->priority;
            }

            /* Boost owner to current's priority */
            task_set_priority(owner->task_id, current->priority);

            scheduler_unlock();
        }
    }

    /* Add to wait list (priority-ordered) */
    atomic_lock(&g_scheduler.ready_queues[cpu_id].lock);
    task_list_insert_priority(&mutex->wait_list, current);
    atomic_unlock(&g_scheduler.ready_queues[cpu_id].lock);

    /* Block current task */
    current->state = TASK_BLOCKED;
    schedule();

    return 0;
}

/**
 * @brief Unlock mutex (with priority inheritance restoration)
 * @param mutex Mutex
 * @return 0 on success, negative error code on failure
 */
int32_t mutex_unlock(Mutex_t *mutex)
{
    TCB_t *current = get_current_task();

    /* Verify ownership */
    if (mutex->owner != current)
    {
        return -EPERM;
    }

    /* Handle recursive lock */
    if (mutex->nest_count > 1U)
    {
        mutex->nest_count--;
        return 0;
    }

    /* Restore original priority (priority inheritance) */
    if ((mutex->flags & MUTEX_PRIORITY_INHERITANCE) != 0U)
    {
        if (current->priority != mutex->original_prio)
        {
            task_set_priority(current->task_id, mutex->original_prio);
        }
    }

    /* Release lock */
    mutex->nest_count = 0U;
    mutex->owner = NULL;
    atomic_store_explicit(&mutex->lock, 0U, memory_order_release);

    /* Wake highest-priority waiting task */
    TCB_t *next = task_list_pop_highest(&mutex->wait_list);
    if (next != NULL)
    {
        uint32_t cpu_id = next->cpu_affinity;
        scheduler_lock();

        next->state = TASK_READY;
        atomic_lock(&g_scheduler.ready_queues[cpu_id].lock);
        task_enqueue(next);
        atomic_unlock(&g_scheduler.ready_queues[cpu_id].lock);

        scheduler_unlock();
    }

    return 0;
}
```

### 4.2 Semaphore Wait/Post

```c
/**
 * @brief Semaphore wait (P operation)
 * @param sem Semaphore
 * @param timeout_ms Timeout
 * @return 0 on success, -ETIMEDOUT on timeout
 */
int32_t sem_wait(Semaphore_t *sem, uint32_t timeout_ms)
{
    TCB_t *current = get_current_task();

    /* Try to decrement count */
    uint32_t old_count = atomic_load_explicit(&sem->count,
                                              memory_order_acquire);
    while (old_count > 0U)
    {
        if (atomic_compare_exchange_weak_explicit(
                &sem->count, &old_count, old_count - 1U,
                memory_order_acquire, memory_order_acquire))
        {
            /* Successfully decremented */
            return 0;
        }
    }

    /* Count is 0, block */
    if (timeout_ms == 0U)
    {
        /* Infinite wait */
        atomic_lock(&g_scheduler.ready_queues[get_cpu_id()].lock);

        if ((sem->flags & SEM_FIFO) != 0U)
        {
            task_list_push_tail(&sem->wait_list, current);
        }
        else
        {
            task_list_insert_priority(&sem->wait_list, current);
        }

        current->state = TASK_BLOCKED;
        schedule();

        atomic_unlock(&g_scheduler.ready_queues[get_cpu_id()].lock);

        return 0;
    }
    else
    {
        /* Timeout wait */
        uint64_t deadline = get_system_time_ns() +
                            (uint64_t)timeout_ms * 1000000ULL;

        if ((sem->flags & SEM_FIFO) != 0U)
        {
            task_list_push_tail(&sem->wait_list, current);
        }
        else
        {
            task_list_insert_priority(&sem->wait_list, current);
        }

        current->state = TASK_BLOCKED;
        task_delay_until(deadline);

        /* Check if we were signaled or timed out */
        if (current->state == TASK_BLOCKED)
        {
            /* Timeout: remove from wait list */
            task_list_remove(&sem->wait_list, current);
            return -ETIMEDOUT;
        }

        return 0;
    }
}

/**
 * @brief Semaphore post (V operation)
 * @param sem Semaphore
 * @return 0 on success, negative error code on failure
 *
 * @note Safe to call from interrupt context
 */
int32_t sem_post(Semaphore_t *sem)
{
    /* Wake one waiting task */
    TCB_t *task = task_list_pop_highest(&sem->wait_list);
    if (task != NULL)
    {
        uint32_t cpu_id = task->cpu_affinity;

        task->state = TASK_READY;
        atomic_lock(&g_scheduler.ready_queues[cpu_id].lock);
        task_enqueue(task);
        atomic_unlock(&g_scheduler.ready_queues[cpu_id].lock);

        return 0;
    }

    /* No waiters, increment count */
    uint32_t old_count = atomic_load_explicit(&sem->count,
                                              memory_order_acquire);
    uint32_t max_count = atomic_load_explicit(&sem->max_count,
                                              memory_order_acquire);

    while (old_count < max_count)
    {
        if (atomic_compare_exchange_weak_explicit(
                &sem->count, &old_count, old_count + 1U,
                memory_order_release, memory_order_acquire))
        {
            return 0;
        }
    }

    /* Count would overflow */
    return -EOVERFLOW;
}
```

### 4.3 Event Flag Wait

```c
/**
 * @brief Wait for event flags
 * @param group Event group
 * @param flags Flags to wait for
 * @param wait_type EV_WAIT_ANY or EV_WAIT_ALL
 * @param timeout_ms Timeout
 * @return 0 on success, -ETIMEDOUT on timeout
 */
int32_t event_wait(EventGroup_t *group,
                   uint32_t flags,
                   uint32_t wait_type,
                   uint32_t timeout_ms)
{
    TCB_t *current = get_current_task();
    uint32_t current_flags;
    uint32_t matched;

    /* Check if condition already satisfied */
    current_flags = atomic_load_explicit(&group->flags,
                                         memory_order_acquire);

    if ((wait_type & EV_WAIT_ALL) != 0U)
    {
        matched = ((current_flags & flags) == flags) ? 1U : 0U;
    }
    else
    {
        matched = ((current_flags & flags) != 0U) ? 1U : 0U;
    }

    if (matched != 0U)
    {
        /* Condition satisfied, clear flags if auto-clear */
        if ((wait_type & EV_AUTO_CLEAR) != 0U)
        {
            atomic_fetch_and_explicit(&group->flags, ~flags,
                                     memory_order_release);
        }
        return 0;
    }

    /* Add to wait list */
    atomic_lock(&g_scheduler.ready_queues[get_cpu_id()].lock);
    task_list_insert_priority(&group->wait_list, current);
    atomic_unlock(&g_scheduler.ready_queues[get_cpu_id()].lock);

    /* Block */
    current->state = TASK_BLOCKED;

    if (timeout_ms == 0U)
    {
        schedule();
    }
    else
    {
        uint64_t deadline = get_system_time_ns() +
                            (uint64_t)timeout_ms * 1000000ULL;
        task_delay_until(deadline);
    }

    /* Check if we timed out */
    if (current->state == TASK_BLOCKED)
    {
        task_list_remove(&group->wait_list, current);
        return -ETIMEDOUT;
    }

    return 0;
}

/**
 * @brief Set event flags
 * @param group Event group
 * @param flags Flags to set
 * @return Previous flags value
 */
uint32_t event_set(EventGroup_t *group, uint32_t flags)
{
    uint32_t old_flags;
    uint32_t new_flags;
    TCB_t *task;
    TCB_t *next;

    /* Set flags */
    old_flags = atomic_fetch_or_explicit(&group->flags, flags,
                                         memory_order_release);

    /* Wake tasks whose wait condition is satisfied */
    task = group->wait_list.head;
    while (task != NULL)
    {
        next = task->next;

        /* Check wait condition */
        new_flags = atomic_load_explicit(&group->flags,
                                         memory_order_acquire);
        uint32_t matched = 0U;

        if ((task->wait_flags & EV_WAIT_ALL) != 0U)
        {
            matched = ((new_flags & task->event_mask) == task->event_mask) ?
                      1U : 0U;
        }
        else
        {
            matched = ((new_flags & task->event_mask) != 0U) ? 1U : 0U;
        }

        if (matched != 0U)
        {
            /* Remove from wait list */
            task_list_remove(&group->wait_list, task);

            /* Wake task */
            uint32_t cpu_id = task->cpu_affinity;
            task->state = TASK_READY;
            atomic_lock(&g_scheduler.ready_queues[cpu_id].lock);
            task_enqueue(task);
            atomic_unlock(&g_scheduler.ready_queues[cpu_id].lock);
        }

        task = next;
    }

    return old_flags;
}
```

### 4.4 Message Queue Implementation

```c
/**
 * @brief Send message to queue
 * @param queue Message queue
 * @param msg Message buffer
 * @param timeout_ms Timeout
 * @return 0 on success, -ETIMEDOUT on timeout, -EAGAIN if queue full
 */
int32_t msgq_send(MessageQueue_t *queue,
                  const void *msg,
                  uint32_t timeout_ms)
{
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint8_t *buffer;

    if ((queue == NULL) || (msg == NULL))
    {
        return -EINVAL;
    }

    /* Try to send (non-blocking) */
    atomic_lock(&queue->lock);

    count = atomic_load_explicit(&queue->count, memory_order_acquire);

    if (count >= queue->capacity)
    {
        atomic_unlock(&queue->lock);

        if (timeout_ms == 0U)
        {
            return -EAGAIN;
        }

        /* Block until space available */
        /* ... (similar to sem_wait) ... */
    }

    /* Calculate next head position */
    head = atomic_load_explicit(&queue->head, memory_order_acquire);
    buffer = (uint8_t *)queue->buffer + (head * queue->msg_size);

    /* Copy message to buffer */
    memcpy(buffer, msg, queue->msg_size);

    /* Update head and count */
    atomic_store_explicit(&queue->head,
                         (head + 1U) % queue->capacity,
                         memory_order_release);
    atomic_fetch_add_explicit(&queue->count, 1U, memory_order_release);

    atomic_unlock(&queue->lock);

    /* Wake waiting receiver */
    TCB_t *receiver = task_list_pop_highest(&queue->rx_wait_list);
    if (receiver != NULL)
    {
        uint32_t cpu_id = receiver->cpu_affinity;
        receiver->state = TASK_READY;
        atomic_lock(&g_scheduler.ready_queues[cpu_id].lock);
        task_enqueue(receiver);
        atomic_unlock(&g_scheduler.ready_queues[cpu_id].lock);
    }

    return 0;
}

/**
 * @brief Receive message from queue
 * @param queue Message queue
 * @param msg Message buffer
 * @param timeout_ms Timeout
 * @return 0 on success, -ETIMEDOUT on timeout
 */
int32_t msgq_receive(MessageQueue_t *queue,
                     void *msg,
                     uint32_t timeout_ms)
{
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint8_t *buffer;

    if ((queue == NULL) || (msg == NULL))
    {
        return -EINVAL;
    }

    /* Try to receive (non-blocking) */
    atomic_lock(&queue->lock);

    count = atomic_load_explicit(&queue->count, memory_order_acquire);

    if (count == 0U)
    {
        atomic_unlock(&queue->lock);

        if (timeout_ms == 0U)
        {
            return -EAGAIN;
        }

        /* Block until message available */
        /* ... (similar to sem_wait) ... */
    }

    /* Calculate next tail position */
    tail = atomic_load_explicit(&queue->tail, memory_order_acquire);
    buffer = (uint8_t *)queue->buffer + (tail * queue->msg_size);

    /* Copy message from buffer */
    memcpy(msg, buffer, queue->msg_size);

    /* Update tail and count */
    atomic_store_explicit(&queue->tail,
                         (tail + 1U) % queue->capacity,
                         memory_order_release);
    atomic_fetch_sub_explicit(&queue->count, 1U, memory_order_release);

    atomic_unlock(&queue->lock);

    /* Wake waiting sender */
    TCB_t *sender = task_list_pop_highest(&queue->tx_wait_list);
    if (sender != NULL)
    {
        uint32_t cpu_id = sender->cpu_affinity;
        sender->state = TASK_READY;
        atomic_lock(&g_scheduler.ready_queues[cpu_id].lock);
        task_enqueue(sender);
        atomic_unlock(&g_scheduler.ready_queues[cpu_id].lock);
    }

    return 0;
}
```

---

## 5. Performance Requirements

### 5.1 Timing Constraints

| Operation | Maximum Latency |
|-----------|-----------------|
| **Mutex Lock (uncontended)** | 100 ns |
| **Mutex Unlock** | 100 ns |
| **Spin Lock/Unlock** | 50 ns |
| **Sem Post** | 200 ns |
| **Sem Wait (uncontended)** | 200 ns |
| **Event Set** | 300 ns |
| **Msg Send (uncontended)** | 500 ns |
| **Msg Receive (uncontended)** | 500 ns |

### 5.2 Memory Constraints

| Primitive | Memory Overhead |
|-----------|-----------------|
| **Mutex** | 64 bytes |
| **Semaphore** | 32 bytes |
| **Spinlock** | 4 bytes |
| **Event Group** | 16 bytes |
| **Message Queue** | 128 bytes + buffer |

### 5.3 Bounded Execution

- **Maximum Block Time**: Deterministic (configurable timeout)
- **Maximum Waiters**: 256 tasks per primitive
- **Priority Inheritance Depth**: ≤ 8 levels

---

## 6. MISRA-C:2012 Compliance

### 6.1 Critical Rules

| Rule | Requirement |
|------|-------------|
| Rule 10.1 | No implicit integer conversions |
| Rule 10.3 | No assignment in boolean expression |
| Rule 10.4 | Logical operators on boolean operands |
| Rule 10.5 | Bitwise NOT on unsigned only |
| Rule 11.1 | No pointer-integer conversions |

### 6.2 Type Safety

```c
/* ✅ Correct: Explicit boolean comparison */
if (atomic_load(&lock->serving) == my_ticket) {
    /* ... */
}

/* ❌ Wrong: Implicit boolean */
if (atomic_load(&lock->serving)) {
    /* ... */
}
```

### 6.3 Runtime Checks

```c
/* Compile-time assertions */
STATIC_ASSERT(sizeof(Mutex_t) == 64U, Mutex_size_wrong);
STATIC_ASSERT(sizeof(TicketLock_t) == 4U, TicketLock_size_wrong);
STATIC_ASSERT((sizeof(MessageQueue_t) & 0x3FU) == 0U,
              MessageQueue_misaligned);
```

---

## 7. Testing Strategy

### 7.1 Unit Tests

| Test Case | Description |
|-----------|-------------|
| **TC-SYNC-001** | Mutex lock/unlock (basic) |
| **TC-SYNC-002** | Mutex recursive locking |
| **TC-SYNC-003** | Mutex priority inheritance |
| **TC-SYNC-004** | Semaphore wait/post |
| **TC-SYNC-005** | Binary semaphore |
| **TC-SYNC-006** | Spinlock fairness (FIFO) |
| **TC-SYNC-007** | Event flag wait/set/clear |
| **TC-SYNC-008** | Message queue send/receive |
| **TC-SYNC-009** | Condition variable wait/signal |
| **TC-SYNC-010** | Timeout handling |

### 7.2 Integration Tests

| Test Case | Description |
|-----------|-------------|
| **TC-SYNC-INT-001** | Producer-consumer (mutex + condvar) |
| **TC-SYNC-INT-002** | Reader-writer lock (using mutex) |
| **TC-SYNC-INT-003** | Barrier synchronization |
| **TC-SYNC-INT-004** | Rendezvous pattern |

### 7.3 Performance Tests

| Test Case | Metric | Target |
|-----------|--------|--------|
| **TC-SYNC-PERF-001** | Mutex contention latency | < 1 μs |
| **TC-SYNC-PERF-002** | Spinlock acquisition time | < 100 ns |
| **TC-SYNC-PERF-003** | Message queue throughput | > 1M msg/sec |

### 7.4 Coverage Requirements

- **Statement Coverage**: > 95%
- **Branch Coverage**: > 90%
- **MC/DC Coverage**: > 85% (critical functions)

---

## 8. Configuration Options

### 8.1 MenuConfig Options

```kconfig
config SYNCHRONIZATION
    bool "Synchronization Primitives"
    default y

config MUTEX_PRIORITY_INHERITANCE
    bool "Mutex Priority Inheritance"
    default y
    depends on SYNCHRONIZATION

config MUTEX_PRIORITY_CEILING
    bool "Mutex Priority Ceiling"
    default n
    depends on SYNCHRONIZATION

config MAX_MSGQ_SIZE
    int "Maximum message queue size"
    range 1 1024
    default 32
    depends on SYNCHRONIZATION

config MAX_EVENT_FLAGS
    int "Number of event flags"
    range 1 64
    default 32
    depends on SYNCHRONIZATION
```

---

## 9. Error Handling

### 9.1 Error Codes

| Error Code | Description |
|------------|-------------|
| `ERROR_INVALID_MUTEX` | Mutex not initialized |
| `ERROR_NOT_OWNER` | Caller does not own mutex |
| `ERROR_WOULD_BLOCK` | Operation would block (trylock) |
| `ERROR_TIMEOUT` | Operation timed out |
| `ERROR_OVERFLOW` | Semaphore count overflow |
| `ERROR_UNDERFLOW` | Semaphore count underflow |

### 9.2 Error Recovery

- **Mutex Lock Timeout**: Return -ETIMEDOUT (caller handles)
- **Priority Inheritance Failure**: Log error, continue (degraded)
- **Message Queue Overflow**: Return -EAGAIN (caller retry)
- **Deadlock Detection**: Trigger core dump (fatal)

---

## 10. Traceability

### 10.1 Requirements Traceability

| LLD Section | HLD Section | Plan.md Section |
|-------------|-------------|-----------------|
| Mutex with PI | 4.2 Data Structures | 4.2.10 |
| Semaphore | POSIX PSE52 | - |
| Spinlock | 4.9 SMP Sync | 4.9.1 |
| Message Queue | POSIX PSE52 | - |

### 10.2 Test Coverage Traceability

| Test Case | Requirement |
|-----------|-------------|
| TC-SYNC-003 | SYNC-001: Priority inheritance |
| TC-SYNC-PERF-001 | NFR-001: Mutex latency < 1μs |
| TC-SYNC-INT-001 | SYNC-002: Producer-consumer |

---

## Appendix A: Priority Inheritance Protocol

### A.1 Protocol Rules

1. **Donation**: When high-priority task H blocks on mutex held by low-priority task L, L temporarily inherits H's priority.
2. **Propagation**: If L blocks on another mutex held by task M, M inherits L's boosted priority (which is H's priority).
3. **Restoration**: When L releases the mutex, its priority is restored to the maximum of:
   - Its original priority
   - The priorities of all tasks still blocked on mutexes it holds

### A.2 Example

```
Initial state:
  Task H: priority 10 (highest)
  Task M: priority 50 (medium)
  Task L: priority 100 (lowest)

Timeline:
  t0: L locks mutex A
  t1: M locks mutex B (which L needs)
  t2: H tries to lock mutex A (held by L)
      → L's priority boosted to 10
  t3: L tries to lock mutex B (held by M)
      → M's priority boosted to 10 (via L)
  t4: M unlocks mutex B
      → M's priority restored to 50
  t5: L unlocks mutex A
      → L's priority restored to 100
```

---

**Document End**

---

**文档结束**
