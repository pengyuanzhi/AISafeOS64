# AISafeOS64 微内核架构设计文档

**版本**: 2.0
**日期**: 2026-03-30
**架构**: 微内核（Microkernel）
**对应需求**: REQUIREMENTS.md v2.4

---

## 文档控制信息

| 项目 | 信息 |
|------|------|
| **文档标题** | AISafeOS64 微内核架构设计文档 |
| **文档版本** | 2.0 |
| **创建日期** | 2026-03-30 |
| **作者** | AISafeOS64 Team |
| **审核状态** | 待审核 |
| **需求基线** | REQUIREMENTS.md v2.4 (160 条需求) |

---

## 1. 引言

### 1.1 文档目的

本文档定义 AISafeOS64 微内核操作系统的系统架构，描述整体结构、关键设计决策、模块划分和接口设计。面向系统架构师、内核开发人员和安全评估人员。

### 1.2 架构定位

AISafeOS64 采用**微内核架构**。内核仅保留最小化机制：

- 线程调度
- IPC 消息传递
- 虚拟内存管理
- 中断路由
- 能力（Capability）管理
- 内核对象生命周期管理

所有其他服务（设备驱动、文件系统、网络、进程管理等）运行在**用户态**，通过 IPC 通信。

### 1.3 设计参考

| 参考 | 说明 |
|------|------|
| seL4 | 能力模型、形式化验证、对象生命周期 |
| QNX Neutrino | IPC 通道-连接模型、Pulse 消息、用户态驱动 |
| Fuchsia (Zircon) | 内核对象类型系统、VMO 句柄模型 |
| 鸿蒙微内核 (OSDI'24) | 驱动容器、双生驱动、Linux 驱动复用 |
| HelenOS | 多服务器微内核架构、IPC 设计 |

---

## 2. 系统总体架构

### 2.1 四层架构

```
┌─────────────────────────────────────────────────────────────┐
│                     应用层 (Applications)                    │
│           用户应用、安全分区、虚拟机客户系统                     │
├─────────────────────────────────────────────────────────────┤
│                   用户态服务层 (User Services)                │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐       │
│  │进程管理器│ │内存管理器│ │文件系统  │ │网络协议栈│       │
│  │PathMgr  │ │设备管理器│ │VFS      │ │TCP/IP   │       │
│  │驱动服务  │ │安全服务  │ │显示服务  │ │调试服务  │       │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘       │
├─────────────────────────────────────────────────────────────┤
│                      微内核 (Microkernel)                     │
│  ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐ ┌────┐               │
│  │调度│ │IPC │ │内存│ │中断│ │能力│ │对象│               │
│  │器  │ │    │ │管理│ │路由│ │管理│ │池  │               │
│  └────┘ └────┘ └────┘ └────┘ └────┘ └────┘               │
│                   目标代码量 < 50KB                          │
├─────────────────────────────────────────────────────────────┤
│                   硬件抽象层 (HAL/Platform)                   │
│           ARMv8-A | GIC-400/500 | Timer | Cache | UART      │
├─────────────────────────────────────────────────────────────┤
│                      硬件 (Hardware)                         │
│           ARM64 多核处理器 (Cortex-A53/A72)                   │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 设计原则

| 原则 | 说明 | 需求映射 |
|------|------|----------|
| 最小内核 | 内核代码量 < 50KB | KR 系列 |
| IPC 为中心 | 所有服务间通信基于消息传递 | KR-005~008 |
| 能力模型 | 细粒度能力访问控制 | KR-013~016 |
| 用户态服务 | 驱动/文件系统/网络运行在用户态 | DR-001, FS-003, NW-002 |
| 故障隔离 | 服务崩溃不影响内核 | RL 系列 |
| 实时优先 | 调度和中断处理考虑实时性 | RT 系列 |
| 可验证性 | 设计时考虑形式化验证潜力 | FV 系列 |
| 混合语言 | 内核 C（MISRA C:2012），用户态可选 Rust | UL 系列 |

### 2.3 地址空间隔离模型

```
┌──────────────────────────────────────────────────────────────┐
│ 内核空间 (0xFFFF000000000000 - 0xFFFFFFFFFFFFFFFF)          │
│  - 内核代码/数据/堆栈（仅内核映射）                            │
│  - 内核对象池、能力空间                                       │
│  - 直接物理内存映射区                                         │
├──────────────────────────────────────────────────────────────┤
│ 用户空间 (0x0000000000000000 - 0x0000FFFFFFFFFFFF)          │
│  ┌──────────────────────────────────────┐                   │
│  │ 进程 A 地址空间                       │                   │
│  │  代码段 | 数据段 | 堆 | 栈 | 共享区  │                   │
│  └──────────────────────────────────────┘                   │
│  ┌──────────────────────────────────────┐                   │
│  │ 进程 B 地址空间（完全隔离）            │                   │
│  │  代码段 | 数据段 | 堆 | 栈 | 共享区  │                   │
│  └──────────────────────────────────────┘                   │
│  共享内存区（通过能力授权映射）                                 │
└──────────────────────────────────────────────────────────────┘
```

---

## 3. 微内核核心模块

### 3.1 线程调度器

#### 3.1.1 调度策略

| 策略 | 说明 | 需求映射 |
|------|------|----------|
| SCHED_FIFO | 优先级抢占，无时间片 | SC-001 |
| SCHED_RR | 优先级抢占 + 轮转时间片 | SC-002 |
| SCHED_EDF | 截止时间优先调度 | SC-003 |
| SCHED_SPORADIC | 偶发任务调度 | SC-003 |
| ARINC 653 | 分区时间触发调度 | SC-005, SC-006 |

#### 3.1.2 核心数据结构

```c
/**
 * @brief 线程控制块（微内核版）
 * @note MISRA-C:2012 合规，缓存行对齐
 */
typedef struct KernelThread
{
    /* 身份标识 */
    uint64_t            thread_id;         /**< 线程唯一 ID */
    uint32_t            process_id;        /**< 所属进程 ID */
    char                name[16];          /**< 线程名称 */

    /* 调度信息 */
    uint8_t             priority;          /**< 当前优先级 (0-255) */
    uint8_t             base_priority;     /**< 基础优先级 */
    uint8_t             state;             /**< 线程状态 */
    uint8_t             cpu_affinity;      /**< CPU 亲和性 */
    uint32_t            cpu_id;            /**< 当前运行 CPU */

    /* 栈管理 */
    uint64_t           *stack_ptr;         /**< 当前栈指针 */
    uint64_t           *stack_base;        /**< 栈基地址 */
    uint32_t            stack_size;        /**< 栈大小（字节） */

    /* 地址空间 */
    uint64_t            page_table;        /**< 页表基址 */
    uint32_t            asid;              /**< 地址空间 ID */

    /* 上下文 */
    uint64_t            context[32];       /**< 寄存器保存区 */

    /* 内核对象引用 */
    uint32_t            kobj_ref;          /**< 对象池引用 */

    /* 调度链表 */
    struct KernelThread *next;
    struct KernelThread *prev;
} KThread_t;

/**
 * @brief 每 CPU 就绪队列（256 级位图）
 */
typedef struct __attribute__((aligned(64)))
{
    uint64_t            bitmap[4];         /**< 256 位优先级位图 */
    KThread_t          *queues[256];       /**< 256 级就绪队列 */
    atomic_uint32_t     lock;              /**< 自旋锁 */
    uint32_t            thread_count;      /**< 线程计数 */
} PerCPUReadyQueue_t;
```

#### 3.1.3 O(1) 优先级查找

```c
/**
 * @brief 使用 CLZ 指令 O(1) 查找最高优先级
 * @param bitmap 256 位优先级位图
 * @return 最高优先级 (0-255)，255 表示无就绪线程
 */
static inline uint8_t find_highest_priority(const uint64_t *bitmap)
{
    if (bitmap[0] != 0U)
    {
        return (uint8_t)__builtin_clzll(bitmap[0]);
    }
    if (bitmap[1] != 0U)
    {
        return (uint8_t)(64U + __builtin_clzll(bitmap[1]));
    }
    if (bitmap[2] != 0U)
    {
        return (uint8_t)(128U + __builtin_clzll(bitmap[2]));
    }
    if (bitmap[3] != 0U)
    {
        return (uint8_t)(192U + __builtin_clzll(bitmap[3]));
    }
    return 255U;
}
```

### 3.2 IPC 子系统

#### 3.2.1 IPC 机制总览

| 机制 | 用途 | 延迟目标 | 需求映射 |
|------|------|----------|----------|
| 同步消息 (Send/Recv/Reply) | RPC 风格请求-响应 | 同核往返 < 1μs | KR-005 |
| 异步通知 (Notification) | 事件信号 | < 500ns | KR-006 |
| Pulse 轻量级消息 | 中断转发、状态变化 | 优先级 + code + value | KR-007 |
| 共享内存 | 大数据零拷贝传输 | 带宽 > 10GB/s | KR-008 |

#### 3.2.2 通道-连接模型（Channel-Connection）

借鉴 QNX Neutrino 的消息传递模型：

```
客户端线程                     服务端线程
    │                              │
    │  ConnectAttach(channel)      │
    │─────────────────────────────>│  建立连接 (Connection)
    │                              │
    │  MsgSend(conn, data, reply)  │
    │─────────────────────────────>│  同步发送（客户端阻塞）
    │                              │
    │                              │  处理请求
    │                              │
    │  <───────────────────────────│  MsgReply（客户端恢复）
    │                              │
    │  MsgSendPulse(conn, pulse)   │
    │─────────────────────────────>│  异步 Pulse（客户端不阻塞）
    │                              │
```

#### 3.2.3 IPC 快速路径优化

为实现同核往返 < 1μs 的目标，采用以下优化：

- **快速路径 (Fast Path)**：同核 IPC 直接切换线程上下文，跳过队列
- **寄存器传递**：小消息（≤ 4 个寄存器）直接通过寄存器传递，无内存拷贝
- **页表共享**：内核空间在所有地址空间中共享映射，IPC 无需切换页表
- **TLB 保留**：IPC 期间保持发送方的 TLB 条目

### 3.3 内存管理

#### 3.3.1 物理内存管理

内核内部**禁止动态内存分配**（MM-001），使用静态分配和对象池：

```c
/**
 * @brief 内核对象池（Souls 分配器）
 * @note 固定大小对象池，O(1) 分配/释放，无碎片
 */
typedef struct ObjectPool
{
    uint8_t            *buffer;           /**< 内存缓冲区 */
    uint32_t            obj_size;         /**< 对象大小 */
    uint32_t            capacity;         /**< 池容量 */
    uint32_t           *free_stack;       /**< 空闲索引栈 */
    atomic_uint32_t     free_count;       /**< 空闲计数 */
    TicketLock_t        lock;             /**< 自旋锁 */
} ObjectPool_t;
```

#### 3.3.2 虚拟内存管理

ARMv8-A 4 级页表结构（PGD → PUD → PMD → PTE）：

```
虚拟地址 [63:0]
  |9 位  |9 位  |9 位  |9 位  |12 位 |
  | PGD  | PUD  | PMD  | PTE  |偏移  |
  | L0   | L1   | L2   | L3   |      |
```

支持混合页大小：
- **4KB 标准页**：L3 级别
- **2MB 大页**：L2 级别块映射
- **1GB 大页**：L1 级别块映射

#### 3.3.3 地址空间操作（通过能力保护）

| 操作 | 说明 | 能力要求 |
|------|------|----------|
| VMSpace_Create | 创建新地址空间 | CSpace 管理权 |
| VM_Map | 映射物理页到虚拟地址 | 页帧能力 + VMSpace 能力 |
| VM_Unmap | 解除映射 | VMSpace 写权限 |
| VM_Protect | 修改页面权限 | VMSpace 写权限 |

### 3.4 中断路由

#### 3.4.1 中断到用户态投递

微内核不直接处理设备中断，而是将中断**路由到用户态驱动线程**：

```
硬件中断 → GIC → 内核中断入口
                  │
                  ▼
            查找绑定关系
            （中断号 → 线程）
                  │
                  ▼
         通知用户态线程
         （Notification/Pulse）
                  │
                  ▼
         用户态驱动处理中断
```

关键 API：
- `InterruptAttach(irq, notification_cap)` — 绑定中断到通知对象
- `InterruptWait(notification_cap)` — 等待中断（阻塞）

### 3.5 能力（Capability）管理

#### 3.5.1 能力模型

借鉴 seL4 的能力模型，但支持更细粒度的权限控制：

```c
/**
 * @brief 能力描述符
 */
typedef struct Capability
{
    uint32_t            cap_id;            /**< 能力 ID */
    uint32_t            kobj_id;           /**< 指向的内核对象 ID */
    uint8_t             kobj_type;         /**< 内核对象类型 */
    uint8_t             rights;            /**< 权限位 */
    uint16_t            badge;             /**< 标识（用于连接） */
    uint32_t            parent_cap;        /**< 父能力 ID */
    uint32_t            cspace_id;         /**< 所属 CSpace */
} Cap_t;

/* 权限位定义 */
#define CAP_RIGHT_READ      (1U << 0)      /**< 读取 */
#define CAP_RIGHT_WRITE     (1U << 1)      /**< 写入 */
#define CAP_RIGHT_EXECUTE   (1U << 2)      /**< 执行 */
#define CAP_RIGHT_GRANT     (1U << 3)      /**< 转发 */
#define CAP_RIGHT_REVOKE    (1U << 4)      /**< 撤销 */
```

#### 3.5.2 能力空间（CSpace）

每个进程拥有独立的 CSpace：

```
进程 A 的 CSpace                     进程 B 的 CSpace
┌─────────────────┐                 ┌─────────────────┐
│ Cap 0: Thread   │                 │ Cap 0: Thread   │
│ Cap 1: VMSpace  │                 │ Cap 1: VMSpace  │
│ Cap 2: Endpoint │◄── 共享 ──────►  │ Cap 2: Endpoint │
│ Cap 3: PageFrame│                 │ Cap 3: Notification│
│ Cap 4: CSpace   │                 │ Cap 4: CSpace   │
└─────────────────┘                 └─────────────────┘
```

#### 3.5.3 能力操作

| 操作 | 说明 | 需求映射 |
|------|------|----------|
| CSpace_Create | 创建新的能力空间 | KR-016 |
| Cap_Copy | 复制能力（可降权） | KR-014 |
| Cap_Move | 移动能力 | KR-014 |
| Cap_Revoke | 撤销派生的所有能力 | KR-015 |
| Cap_Delete | 删除能力 | KR-015 |

### 3.6 内核对象生命周期管理

#### 3.6.1 统一类型系统

所有内核资源抽象为统一对象类型：

```c
/**
 * @brief 内核对象类型
 */
typedef enum
{
    KOBJ_THREAD = 0U,      /**< 线程 */
    KOBJ_ENDPOINT,          /**< IPC 端点 */
    KOBJ_NOTIFICATION,      /**< 通知对象 */
    KOBJ_CSPACE,            /**< 能力空间 */
    KOBJ_VMSPACE,           /**< 虚拟地址空间 */
    KOBJ_PAGE_FRAME,        /**< 物理页帧 */
    KOBJ_INTERRUPT,         /**< 中断对象 */
    KOBJ_DEVICE,            /**< 设备对象 */
    KOBJ_CHANNEL,           /**< IPC 通道 */
    KOBJ_CONNECTION,        /**< IPC 连接 */
    KOBJ_TYPE_COUNT         /**< 类型总数 */
} KObjectType_t;

/**
 * @brief 内核对象头部（所有内核对象公共前缀）
 */
typedef struct KernelObjectHeader
{
    KObjectType_t       type;              /**< 对象类型 */
    uint32_t            obj_id;            /**< 对象 ID */
    atomic_uint32_t     ref_count;         /**< 引用计数 */
    uint32_t            parent_id;         /**< 父对象 ID */
    uint32_t            flags;             /**< 状态标志 */
    TicketLock_t        lock;              /**< 对象锁 */
} KObjHeader_t;
```

#### 3.6.2 引用计数与级联销毁

```
Thread (ref=2)
  ├─持有─> Endpoint (ref=1)
  │          └─持有─> Notification (ref=1)
  └─持有─> VMSpace (ref=1)
               └─持有─> PageFrame[] (ref=N)

当 Thread ref → 0:
  级联释放 Endpoint → Notification
  级联释放 VMSpace → PageFrame[]
```

#### 3.6.3 延迟释放（RCU 风格）

并发场景下的安全释放：

1. 对象引用计数归零时，标记为"待释放"
2. 等待所有 CPU 完成当前临界区（宽限期）
3. 确认无悬挂引用后，回收对象到对象池

---

## 4. 用户态服务架构

### 4.1 核心服务

| 服务 | 职责 | 语言 | 需求映射 |
|------|------|------|----------|
| ProcessManager | 进程创建/销毁/监控 | C/Rust | KR-024 |
| MemoryManager | 物理内存分配、虚拟地址映射 | C/Rust | KR-024 |
| PathManager | 设备路径命名空间、服务注册 | C/Rust | KR-024 |
| DeviceManager | 驱动加载、设备节点管理 | C/Rust | DR-004, DR-008 |
| FileSystem | VFS + 具体文件系统实现 | C/Rust | FS-001~003 |
| NetworkStack | TCP/IP 协议栈 | C/Rust | NW-001~003 |
| SecurityService | TEE、密钥管理 | C/Rust | SE 系列 |
| DebugService | GDB stub、日志、追踪 | C | DI-001~003 |

### 4.2 用户态驱动模型

```
┌─────────────────────────────────────────────────┐
│                 用户态驱动空间                     │
│                                                  │
│  ┌─────────────────┐  ┌─────────────────┐       │
│  │ 原生驱动容器    │  │ Linux 驱动容器  │       │
│  │ (Native Driver) │  │ (LxD Container) │       │
│  │                 │  │                 │       │
│  │ 直接 IPC 调用   │  │ Linux LTS 内核  │       │
│  │ MMIO/DMA 映射   │  │ 驱动兼容层      │       │
│  └────────┬────────┘  └────────┬────────┘       │
│           │                    │                 │
├───────────┼────────────────────┼─────────────────┤
│           │   微内核 IPC       │                 │
│           ▼                    ▼                 │
│  ┌──────────────────────────────────────┐        │
│  │         微内核                        │        │
│  │  中断路由 | MMIO 映射 | DMA 管理      │        │
│  └──────────────────────────────────────┘        │
└─────────────────────────────────────────────────┘
```

#### 4.2.1 双生驱动模型（Twin Driver）

参考鸿蒙微内核论文（OSDI'24）：

- **控制面（Control Plane）**：运行在 Linux 驱动容器中，复用现有 Linux 驱动
- **数据面（Data Plane）**：运行在原生容器中，高性能 I/O 路径
- **IC2 接口**：控制面与数据面之间的快速通信通道

### 4.3 服务间通信模式

```
应用进程                        文件系统服务
   │                               │
   │  1. MsgSend(fs_conn, request) │
   │──────────────────────────────>│
   │  （应用阻塞）                   │
   │                               │ 2. 处理请求
   │                               │
   │  3. MsgReply(result)          │
   │<──────────────────────────────│
   │  （应用恢复）                   │
```

---

## 5. 系统调用接口

### 5.1 调用约定

ARMv8-A 使用 `SVC #0` 指令触发系统调用：

- **x0**: 系统调用号
- **x1-x6**: 参数
- **x0**: 返回值（成功返回 0 或正数，失败返回负 POSIX 错误码）

### 5.2 微内核系统调用分类

#### 5.2.1 线程管理

| 系统调用 | 功能 | 需求映射 |
|----------|------|----------|
| sys_thread_create | 创建线程 | KR-001 |
| sys_thread_exit | 退出线程 | KR-001 |
| sys_thread_suspend | 挂起线程 | KR-001 |
| sys_thread_resume | 恢复线程 | KR-001 |
| sys_thread_set_priority | 设置优先级 | KR-002 |
| sys_thread_set_affinity | 设置 CPU 亲和性 | KR-003 |
| sys_thread_yield | 让出 CPU | SC-001 |

#### 5.2.2 IPC 操作

| 系统调用 | 功能 | 需求映射 |
|----------|------|----------|
| sys_channel_create | 创建 IPC 通道 | KR-023 |
| sys_channel_destroy | 销毁通道 | KR-023 |
| sys_connect_attach | 附加到通道 | KR-023 |
| sys_msg_send | 同步发送消息 | KR-005 |
| sys_msg_recv | 接收消息 | KR-005 |
| sys_msg_reply | 回复消息 | KR-005 |
| sys_msg_send_pulse | 发送 Pulse | KR-007 |
| sys_notification_signal | 信号通知 | KR-006 |
| sys_notification_wait | 等待通知 | KR-006 |

#### 5.2.3 内存管理

| 系统调用 | 功能 | 需求映射 |
|----------|------|----------|
| sys_vmspace_create | 创建地址空间 | KR-009 |
| sys_vm_map | 映射页面 | KR-010 |
| sys_vm_unmap | 解除映射 | KR-010 |
| sys_vm_protect | 修改权限 | KR-011 |

#### 5.2.4 能力管理

| 系统调用 | 功能 | 需求映射 |
|----------|------|----------|
| sys_cspace_create | 创建能力空间 | KR-016 |
| sys_cap_copy | 复制能力 | KR-014 |
| sys_cap_revoke | 撤销能力 | KR-015 |
| sys_cap_delete | 删除能力 | KR-015 |

#### 5.2.5 中断管理

| 系统调用 | 功能 | 需求映射 |
|----------|------|----------|
| sys_interrupt_attach | 绑定中断到通知对象 | IN-006 |
| sys_interrupt_detach | 解除中断绑定 | IN-006 |

---

## 6. 安全架构

### 6.1 安全机制总览

| 机制 | 层级 | 需求映射 |
|------|------|----------|
| 能力访问控制 | 内核 | KR-013~016 |
| 地址空间隔离 | MMU | KR-009 |
| 页面权限控制 (RWX) | MMU | KR-011 |
| 安全启动链 | Bootloader | SE-009 |
| ASLR | 用户态加载器 | SE-006 |
| 栈保护（金丝雀/影子栈） | 编译器 + 内核 | SE-004 |
| 代码签名验证 | 加载器 | SE-008 |
| 审计日志 | 安全服务 | SE-005 |

### 6.2 安全启动链

```
Boot ROM (不可变)
    │ 验证签名
    ▼
Bootloader (Stage 1)
    │ 验证签名
    ▼
微内核镜像 (Stage 2)
    │ 验证签名
    ▼
用户态服务镜像 (Stage 3)
```

### 6.3 认证目标

| 认证 | 等级 | 领域 | 需求映射 |
|------|------|------|----------|
| ISO 26262 | ASIL-D | 汽车功能安全 | CA-001 |
| IEC 61508 | SIL-4 | 工业功能安全 | CA-002 |
| Common Criteria | EAL5+ | 信息安全 | CA-003 |

---

## 7. 虚拟化架构

### 7.1 虚拟化模式

| 模式 | 说明 | 需求映射 |
|------|------|----------|
| ARM VHE | 虚拟化扩展（EL2） | VZ-001 |
| vCPU 调度 | 虚拟 CPU 调度策略 | VZ-003 |
| 二阶段地址翻译 | EPT/NPT 嵌套页表 | VZ-005 |
| ARINC 653 分区 | 时间/空间隔离 | SC-006 |

### 7.2 虚拟机架构

```
┌─────────────────────────────────────────────┐
│             虚拟机管理器 (VMM)                │
│  ┌──────────────┐  ┌──────────────┐         │
│  │ Guest OS A   │  │ Guest OS B   │         │
│  │ (Linux)      │  │ (AISafeOS64) │         │
│  │  vCPU 0-1    │  │  vCPU 2-3    │         │
│  └──────────────┘  └──────────────┘         │
│                                             │
│  设备虚拟化: VirtIO | 直通 Passthrough       │
├─────────────────────────────────────────────┤
│                 微内核                        │
│  vCPU 调度 | 二阶段翻译 | 虚拟中断注入        │
└─────────────────────────────────────────────┘
```

---

## 8. 启动流程

### 8.1 多阶段启动

```
Stage 0: Boot ROM
    │ 固化代码，不可修改
    ▼
Stage 1: Bootloader (EL3/EL2)
    │ 硬件初始化、MMU 使能、设备树解析
    │ 安全启动验证（签名校验）
    │ 加载微内核镜像
    ▼
Stage 2: 微内核初始化 (EL1)
    │ 对象池初始化、能力空间创建
    │ 调度器初始化、中断控制器初始化
    │ 启动 SMP 从核
    │ 创建第一个用户态线程 (init)
    ▼
Stage 3: 用户态初始化 (EL0)
    │ ProcessManager 启动
    │ MemoryManager 启动
    │ PathManager 启动
    │ DeviceManager 启动 → 加载驱动
    │ FileSystem 启动
    │ 启动应用
```

---

## 9. 性能目标

### 9.1 关键性能指标

| 指标 | 目标值 | 需求映射 |
|------|--------|----------|
| IPC 同核往返延迟 | < 1μs | PF-001, KR-005 |
| 中断到用户态延迟 | < 20μs | IN-005 |
| 调度延迟 | < 10μs | SC-008 |
| 上下文切换 | < 5μs | PF-003 |
| 共享内存带宽 | > 10GB/s | KR-008 |
| 通知延迟 | < 500ns | KR-006 |

### 9.2 内核代码量约束

| 模块 | 预估代码量 |
|------|-----------|
| 调度器 | ~5KB |
| IPC | ~8KB |
| 内存管理 | ~10KB |
| 中断路由 | ~3KB |
| 能力管理 | ~8KB |
| 对象管理 | ~5KB |
| 启动/异常处理 | ~5KB |
| **总计** | **< 50KB** |

---

## 10. 构建系统

### 10.1 CMake 构建配置

```cmake
cmake_minimum_required(VERSION 3.20)
project(AISafeOS64 C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# 交叉编译工具链
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/toolchain-arm64.cmake)

# MISRA 合规编译选项
add_compile_options(
    -Wall -Wextra -Werror -Wpedantic
    -Wconversion -Wsign-conversion
    -ffreestanding -fno-builtin
    -fdata-sections -ffunction-sections
)
```

### 10.2 目录结构

```
AISafeOS64/
├── kernel/               # 微内核源码
│   ├── sched/            # 调度器
│   ├── ipc/              # IPC 子系统
│   ├── mm/               # 内存管理
│   ├── irq/              # 中断路由
│   ├── cap/              # 能力管理
│   ├── kobj/             # 内核对象管理
│   └── arch/arm64/       # ARM64 架构代码
├── services/             # 用户态服务
│   ├── proc/             # ProcessManager
│   ├── mem/              # MemoryManager
│   ├── path/             # PathManager
│   ├── dev/              # DeviceManager
│   ├── fs/               # FileSystem
│   └── net/              # NetworkStack
├── drivers/              # 用户态驱动
│   ├── native/           # 原生驱动
│   └── linux/            # Linux 驱动容器
├── lib/                  # 用户态库
│   ├── libc/             # C 库
│   └── libkernel/        # 内核 API 绑定
├── include/              # 公共头文件
├── tests/                # 测试
├── scripts/              # 构建脚本
├── cmake/                # CMake 模块
├── kconfig/              # MenuConfig
└── lds/                  # 链接器脚本
```

---

## 11. 可追溯性矩阵

### 11.1 架构设计到需求映射

| 架构模块 | 需求 ID |
|----------|---------|
| 线程调度器 | KR-001~004, SC-001~008, MP-001~005 |
| IPC 子系统 | KR-005~008, KR-017~024, PF-001 |
| 内存管理 | KR-009~012, MM-001~007 |
| 能力管理 | KR-013~016 |
| 对象管理 | KR-017~022 |
| 中断路由 | IN-001~006 |
| 用户态服务 | DR-001~008, FS-001~003, NW-001~003 |
| 安全架构 | SE-001~009, SF-001~004, CA-001~003 |
| 虚拟化 | VZ-001~005, DV-001~003 |
| 启动流程 | BK-001~003 |

---

## 版本历史

| 版本 | 日期 | 架构 | 说明 |
|------|------|------|------|
| 1.0 | 2026-01-09 | 单体 RTOS | 初始版本，单体分层架构 |
| **2.0** | **2026-03-30** | **微内核** | **完全重写为微内核架构，对齐 REQUIREMENTS.md v2.4** |

---

*文档完*
