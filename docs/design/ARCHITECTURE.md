# AISafeOS64 微内核架构设计文档

**版本**: 2.1
**日期**: 2026-04-04
**架构**: 微内核（Microkernel）
**对应需求**: REQUIREMENTS.md v2.4

---

## 文档控制信息

| 项目 | 信息 |
|------|------|
| **文档标题** | AISafeOS64 微内核架构设计文档 |
| **文档版本** | 2.1 |
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

## 12. 内核子系统详细设计

### 12.1 调度器（Scheduler）

#### 12.1.1 256 级位图调度

调度器采用 4 个 `uint64_t` 组成 256 位优先级位图，使用 CLZ（Count Leading Zeros）指令实现 O(1) 最高优先级查找：

```
优先级位图结构：
bitmap[0]: 优先级   0 -  63（0 为最高）
bitmap[1]: 优先级  64 - 127
bitmap[2]: 优先级 128 - 191
bitmap[3]: 优先级 192 - 255
```

每个优先级对应一个 FIFO 就绪队列，同优先级线程按 FIFO 顺序调度。

#### 12.1.2 EDF 调度（Earliest Deadline First）

EDF 调度器为实时任务提供动态优先级调度：

- 每个任务指定截止时间（deadline）和周期（period）
- 使用红黑树按截止时间排序，O(log n) 入队/出队
- 支持周期性任务的自动补充预算
- 截止时间 miss 检测和通知机制

**适用场景**：周期性实时任务（如电机控制、传感器采样）

#### 12.1.3 ARINC 653 分区调度

ARINC 653 模块实现航空标准的分区时间窗口调度：

- **时间分区**：将 CPU 时间划分为固定长度的主时间框架（MAF）
- **空间分区**：每个分区拥有独立的地址空间和资源
- **健康监控**：分区级别的健康监控和故障隔离
- **确定性调度**：严格的时间触发调度，保证分区间的时序隔离

**适用场景**：航空电子系统（DO-178C）、安全关键混合关键性系统

#### 12.1.4 SMP 多核调度

| 特性 | 说明 |
|------|------|
| 负载均衡 | 周期性检查各核负载，迁移过载线程 |
| CPU 亲和性 | 每线程 8 位亲和性掩码，支持绑定到指定核心 |
| 调度域 | 层级调度域结构（SMT → MC → NUMA） |
| IPI 触发 | 跨核调度通过 GIC SGI 发送 IPI_RESCHEDULE |
| 迁移锁 | 线程迁移期间持有双核锁，防止竞态 |

### 12.2 IPC 子系统

#### 12.2.1 Channel（通道）

通道是服务端的消息汇聚点：

- 服务端线程通过 `ChannelCreate()` 创建通道，获得接收端点
- 通道维护一个消息队列和连接列表
- 支持 `MsgSend()` 同步阻塞调用和 `MsgSendPulse()` 异步发送
- 消息按优先级排序，紧急消息优先处理

**关键数据**：

```c
typedef struct Channel
{
    uint32_t            channel_id;       /* 通道 ID */
    uint32_t            owner_thread;     /* 所属线程 */
    KThread_t          *recv_queue;       /* 等待接收的线程队列 */
    Connection_t       *conn_list;        /* 连接链表 */
    PulseQueue_t        pulse_queue;      /* Pulse 消息队列 */
    uint32_t            msg_count;        /* 待处理消息计数 */
} Channel_t;
```

#### 12.2.2 Endpoint（端点）

端点是 IPC 的基本通信端点，支持同步 Send/Receive/Reply 模式：

- 每个端点绑定到一个内核对象（通道、通知等）
- 支持消息缓冲和发送方阻塞队列
- 端点间通过 Badge 机制标识发送方身份
- 端点可通过能力在进程间共享

#### 12.2.3 Notification（通知）

通知是轻量级的异步信号机制：

- 基于 `uint64_t` 位掩码，支持 64 种独立事件
- `NotificationSignal()` 设置指定位，唤醒等待线程
- `NotificationWait()` 等待指定位集合，返回时自动清除
- 常用于中断到用户态的投递、线程间事件同步
- 延迟目标 < 500ns

### 12.3 虚拟内存管理

#### 12.3.1 ASID（Address Space ID）

ARMv8-A 的 ASID 机制用于 TLB 标签，避免每次地址空间切换时的 TLB 冲刷：

- 8 位硬件 ASID（0-255），由内核分配管理
- 每个进程绑定唯一 ASID，减少 TLB 失效率
- ASID 回收：当 ASID 耗尽时，执行全局 TLB 无效化后重新分配
- 支持 ASID 版本号机制检测过期 TLB 条目

#### 12.3.2 VMA（Virtual Memory Area）

VMA 描述进程地址空间中一段连续的虚拟内存区域：

```c
typedef struct VMA
{
    uint64_t            start;            /* 起始虚拟地址 */
    uint64_t            end;              /* 结束虚拟地址（不含） */
    uint32_t            permissions;      /* 权限（R/W/X） */
    uint32_t            flags;            /* 标志（共享/私有/固定等） */
    uint32_t            backing_kobj;     /* 后端内核对象（如页帧） */
    struct VMA         *prev;             /* 前驱 */
    struct VMA         *next;             /* 后继 */
} VMA_t;
```

VMA 管理：
- 使用红黑树按地址排序，支持 O(log n) 查找/插入
- 支持区域合并（相邻且属性相同的 VMA 自动合并）
- 支持区域分裂（部分映射/解除映射时自动分裂）
- 惰性分配（首次访问时触发 page fault 分配物理页）

#### 12.3.3 页表管理

ARMv8-A 4 级页表结构：

| 级别 | 名称 | 索引位 | 页大小 | 表大小 |
|------|------|--------|--------|--------|
| L0 | PGD | [63:48] | — | 4KB（512 项） |
| L1 | PUD | [47:39] | 1GB 块 | 4KB（512 项） |
| L2 | PMD | [38:30] | 2MB 块 | 4KB（512 项） |
| L3 | PTE | [29:21] | 4KB 页 | 4KB（512 项） |

页表项属性：
- 权限控制：AP[2:1] 位控制 EL0/EL1 的读/写权限
- 执行控制：PXN/UXN 位控制特权/非特权执行
- 内存属性：MAIR 索引指定 Normal/Device 类型
- 共享属性：Inner Shareable / Outer Shareable
- 访问位：AF（Access Flag）用于页面替换

### 12.4 能力系统（Capability）

#### 12.4.1 CSpace（能力空间）

每个进程拥有独立的 CSpace，存储该进程持有的所有能力：

- CSpace 使用两级页表结构：CSpace → CNode → Cap Slot
- 每个 Cap Slot 存储一个能力描述符（Cap_t）
- CSpace 的访问通过 MCP（Margin of Capability Privilege）控制
- 初始 CSpace 由内核在进程创建时分配

#### 12.4.2 能力传递与降权

能力在进程间传递时自动降权：

```
原始能力: R|W|X|G|Rvk
    │
    ├── 复制给进程 B: R|W       （去掉执行、转发、撤销权限）
    │       │
    │       └── 进程 B 再复制: R （仅保留读取）
    │
    └── 复制给进程 C: R|X       （去掉写入、转发、撤销权限）
```

降权规则：
- 子能力的权限必须是父能力权限的子集
- GRANT 权限控制是否能将能力转发给其他进程
- REVOKE 权限控制是否能够撤销所有派生能力

#### 12.4.3 能力撤销

撤销机制支持递归撤销所有派生能力：

1. 进程 A 持有 Cap（R|W|G|Rvk），复制给 B
2. 进程 B 持有 Cap（R|W），复制给 C
3. 进程 A 执行 `CapRevoke()`：
   - 撤销 B 的 Cap（标记为无效）
   - 级联撤销 C 从 B 获得的 Cap
   - 释放关联的内核对象引用

### 12.5 同步原语

#### 12.5.1 TicketLock（公平自旋锁）

TicketLock 保证锁获取的 FIFO 顺序，避免饥饿：

```c
typedef struct
{
    atomic_uint16_t     next_ticket;      /* 下一个可用票号 */
    atomic_uint16_t     serving_ticket;   /* 当前服务的票号 */
} TicketLock_t;
```

操作：
- `ticket_lock_acquire()`：获取原子票号，自旋等待直到服务号匹配
- `ticket_lock_release()`：递增服务号，唤醒下一个等待者
- 自旋期间使用 `WFE` 指令降低功耗

#### 12.5.2 优先级继承互斥锁

用于任务上下文的互斥锁，支持优先级继承协议，防止优先级反转：

- 当高优先级线程等待低优先级线程持有的锁时
- 低优先级线程临时提升到等待者的优先级
- 释放锁后恢复原始优先级
- 支持嵌套锁定和优先级传递链
- 检测死锁（循环等待检测）

### 12.6 SMP 多核支持

#### 12.6.1 启动流程

```
PSCI CPU_ON → 从核入口 (smp_boot.S)
    → 初始化栈指针
    → 初始化从核 MMU/页表
    → 使能本地中断控制器 (GIC CPU Interface)
    → 进入调度器空闲循环
```

#### 12.6.2 核心间中断（IPI）

| IPI 类型 | 编号 | 用途 |
|----------|------|------|
| IPI_RESCHEDULE | 0 | 请求目标核重新调度 |
| IPI_STOP | 1 | 停止目标核 |
| IPI_TIMER | 2 | 时钟广播 |
| IPI_CALL_FUNC | 3 | 跨核函数调用 |

---

## 13. 代码统计

### 13.1 内核代码（kernel/）

| 模块 | 文件数 | 代码行数 | 说明 |
|------|--------|----------|------|
| arch/arm64 | 10 | ~2,541 | HAL、启动、异常处理、GIC、IPI、SMP 启动、安全启动 |
| sched/ | 12 | ~3,653 | 调度器、EDF、ARINC 653、线程、互斥锁、自旋锁、定时器、栈保护 |
| ipc/ | 4 | ~2,172 | 通道、端点、IC2、通知 |
| mm/ | 6 | ~4,052 | 物理内存、页表、VM 空间、对象池、内核对象、安全 |
| cap/ | 2 | ~1,559 | 能力管理、CSpace |
| irq/ | 1 | ~502 | 中断路由 |
| verify/ | 2 | ~1,254 | 形式化验证、证据收集 |

### 13.2 用户态服务（services/）

| 服务 | 代码行数 | 说明 |
|------|----------|------|
| fs/ | 1,387 | 虚拟文件系统、文件操作、挂载管理 |
| net/ | 2,074 | TCP/IP 协议栈、套接字、网络接口 |
| init/ | 1,225 | 系统初始化、服务启动、依赖管理 |
| proc/ | 1,027 | 进程管理、生命周期监控 |
| mem/ | 1,018 | 内存分配、映射管理、缺页处理 |
| security/ | 2,010 | 安全服务、认证管理 |
| vmm/ | 1,009 | 虚拟机管理器 |
| path/ | 606 | 设备路径、服务注册 |
| dev/ | 806 | 驱动框架 |

### 13.3 驱动（drivers/）

| 驱动 | 代码行数 | 说明 |
|------|----------|------|
| native/virtio.c | 602 | VirtIO 设备驱动 |
| native/uart.c | 701 | UART 串口驱动 |
| linux/container.c | 493 | Linux 驱动容器 |
| twin/twin_driver.c | 558 | 双生驱动框架 |

### 13.4 头文件（include/kernel/）

39 个公共头文件，总计约 9,900 行，覆盖所有内核子系统类型定义和接口声明。

### 13.5 测试代码（tests/）

| 测试文件 | 代码行数 | 测试项数 |
|----------|----------|----------|
| test_channel.c | 1,854 | 2,410 |
| test_capability_revoke.c | 1,404 | 342 |
| test_endpoint.c | 1,212 | 2,510 |
| test_phys_mem.c | 1,163 | 9,108 |
| test_notification.c | 1,097 | 1,997 |
| test_smp.c | 1,080 | 267 |
| test_scheduler.c | 986 | 13,672 |
| test_mutex.c | 974 | 4,382 |
| test_capability.c | 810 | 64 |
| test_timer.c | 806 | 161 |
| test_ipc.c | 715 | 142 |
| test_vfs.c | 680 | 67 |
| test_certification.c | 655 | 29 |
| test_formal_verify.c | 638 | 38 |
| test_object_pool.c | 582 | 1,548 |
| test_evidence.c | 551 | 34 |
| test_twin.c | 553 | 54 |
| test_spinlock.c | 329 | 2,041 |
| test_security.c | 507 | 27 |
| **合计** | **~15,000** | **~38,861** |

### 13.6 总代码量

| 类别 | 代码行数 |
|------|----------|
| 内核代码（kernel/） | ~15,733 |
| 用户态服务（services/） | ~11,162 |
| 驱动（drivers/） | ~2,354 |
| 系统库（lib/） | ~678 |
| 公共头文件（include/） | ~9,900 |
| 测试代码（tests/） | ~15,000 |
| **总计** | **~54,827** |

---

## 14. 安全认证目标详情

### 14.1 ISO 26262 ASIL-D（汽车功能安全）

| 要求 | AISafeOS64 措施 |
|------|-----------------|
| 安全生命周期 | 完整 V 模型开发流程，需求→设计→编码→测试可追溯 |
| 安全需求规格 | 每条需求可追溯到代码模块和测试用例 |
| 设计安全分析 | FMEA/FTA 分析，识别所有潜在故障模式 |
| 编码标准 | MISRA C:2012 零偏差，PC-lint Plus 静态分析 |
| 单元测试 | MC/DC 覆盖率 > 95%，每个判定条件独立验证 |
| 集成测试 | 系统级功能安全测试，故障注入测试 |
| 验证与确认 | 独立审查、形式化验证（关键路径） |

### 14.2 IEC 61508 SIL-4（工业功能安全）

| 要求 | AISafeOS64 措施 |
|------|-----------------|
| 安全完整性等级 | SIL-4（最高），要求硬件容错 HFT ≥ 1 |
| 诊断覆盖率 | > 99%，CPU/内存/通信全路径诊断 |
| 安全功能响应时间 | 硬实时保证，WCET 分析 |
| 系统性能力 | SC 4（最高），完善的安全管理流程 |
| 代码审查 | 至少 2 名独立审查者审查所有安全关键代码 |
| 形式化验证 | 关键内核路径（调度器、IPC）采用形式化方法验证 |

---

## 版本历史

| 版本 | 日期 | 架构 | 说明 |
|------|------|------|------|
| 1.0 | 2026-01-09 | 单体 RTOS | 初始版本，单体分层架构 |
| 2.0 | 2026-03-30 | 微内核 | 完全重写为微内核架构，对齐 REQUIREMENTS.md v2.4 |
| **2.1** | **2026-04-04** | **微内核** | **添加子系统详细设计、代码统计、安全认证详情** |

---

*文档完*
