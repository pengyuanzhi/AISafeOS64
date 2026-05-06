# VMM 架构设计文档

**版本**: 1.0
**日期**: 2026-05-03
**作者**: AISafe64 Team
**状态**: 设计阶段

---

## 📋 目录

1. [概述](#概述)
2. [系统架构](#系统架构)
3. [核心模块设计](#核心模块设计)
4. [嵌套页表设计](#嵌套页表设计)
5. [虚拟设备设计](#虚拟设备设计)
6. [VM 退出处理设计](#vm-退出处理设计)
7. [虚拟中断控制器设计](#虚拟中断控制器设计)
8. [性能优化设计](#性能优化设计)
9. [安全设计](#安全设计)
10. [实现优先级](#实现优先级)

---

## 概述

### 设计目标

AISafeOS64 VMM（虚拟机管理器）的设计目标是提供一个**轻量级、安全、高性能**的虚拟化框架，支持：

1. **ARMv8-A 二阶段地址翻译（NPT）** - Guest VA → Guest PA → Host PA
2. **支持 ARM VHE（Virtualization Host Extensions）** - 用户态虚拟化
3. **VirtIO 设备模拟** - 标准化设备接口
4. **实时调度支持** - EDF/FIFO/ARINC653
5. **MISRA C:2012 合规** - 代码零偏差

### 技术约束

- **text 段限制**: VMM text 段 < 10KB
- **内存开销**: 每个虚拟机 < 5MB（不包含 Guest 内存）
- **性能目标**:
  - vCPU 上下文切换 < 1μs
  - 中断注入延迟 < 100ns
  - MMIO 访问延迟 < 1μs
- **安全目标**:
  - Guest 与 Host 完全隔离
  - 符合 ISO 26262 ASIL-D 安全要求

---

## 系统架构

### 架构图

```
┌──────────────────────────────────────────────────────────────┐
│                    Guest VM (ARMv8-A)                         │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │  Guest OS    │  │  Guest App   │  │  Guest App   │      │
│  └──────────────┘  └──────────────┘  └──────────────┘      │
│      │               │               │                       │
│      │  Guest VA     │               │                       │
│      └───────┬───────┘               │                       │
│              │                       │                       │
│  ┌───────────▼─────────────┐        │                       │
│  │  ARMv8-A NPT (4 级)      │        │                       │
│  │  Guest VA → Guest PA    │        │                       │
│  └───────────┬─────────────┘        │                       │
│              │  Guest PA            │                       │
│              └───────┬──────────────┘                       │
│  ┌───────────────────▼──────────────────┐                   │
│  │         Guest 物理内存               │                   │
│  │   (Guest PA 范围: 0x00000000-      │                   │
│  │        0x3FFFFFFF, 1GB)              │                   │
│  └───────────────────┬──────────────────┘                   │
│                      │  Host PA                              │
└──────────────────────┼──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│              Host VM (AISafeOS64 Host OS)                     │
│  ┌──────────────────────────────────────────────────────┐   │
│  │            VMM Core (Kernel Space)                    │   │
│  │  ┌────────────────────────────────────────────┐      │   │
│  │  │  vCPU 调度器 (Scheduler)                    │      │   │
│  │  │  - EDF/FIFO 调度                           │      │   │
│  │  │  - vCPU 抢占                                │      │   │
│  │  │  - vCPU 迁移                                │      │   │
│  │  └────────────────────────────────────────────┘      │   │
│  │  ┌────────────────────────────────────────────┐      │   │
│  │  │  嵌套页表管理器 (NPT Manager)               │      │   │
│  │  │  - NPT 创建/销毁                             │      │   │
│  │  │  - Guest PA → Host PA 映射                   │      │   │
│  │  │  - TLB 维护                                  │      │   │
│  │  └────────────────────────────────────────────┘      │   │
│  │  ┌────────────────────────────────────────────┐      │   │
│  │  │  虚拟设备模拟器 (Device Emulator)            │      │   │
│  │  │  - VirtIO-Block                             │      │   │
│  │  │  - VirtIO-Net                               │      │   │
│  │  │  - VirtIO-Console                           │      │   │
│  │  │  - VirtIO-RNG                               │      │   │
│  │  └────────────────────────────────────────────┘      │   │
│  │  ┌────────────────────────────────────────────┐      │   │
│  │  │  虚拟中断控制器 (VGIC)                      │      │   │
│  │  │  - 中断状态管理                             │      │   │
│  │  │  - 中断优先级                               │      │   │
│  │  │  - 中断路由                                 │      │   │
│  │  └────────────────────────────────────────────┘      │   │
│  │  ┌────────────────────────────────────────────┐      │   │
│  │  │  Hypercall 处理器                          │      │   │
│  │  └────────────────────────────────────────────┘      │   │
│  └──────────────────────────────────────────────────────┘   │
│                           │                                  │
│                           │ IPC (Channel)                     │
│                           ▼                                  │
│  ┌──────────────────────────────────────────────────────┐   │
│  │            EL0 Services (User Space)                  │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │   │
│  │  │   VMM API    │  │   VMM CLI    │  │   Monitor    │  │   │
│  │  │   Library    │  │   Tool       │  │   Tool       │  │   │
│  │  └──────────────┘  └──────────────┘  └──────────────┘  │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │            硬件抽象层 (HAL)                           │   │
│  │  - ARM VHE 寄存器访问                                 │   │
│  │  - 内存屏障 (DSB/DSB/DMB)                             │   │
│  │  - TLB 操作                                           │   │
│  │  - GIC 控制器访问                                     │   │
│  └──────────────────────────────────────────────────────┘   │
│                                                               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │            硬件 (ARMv8-A + GIC)                       │   │
│  │  - ARMv8-A 核心 (4 核)                                │   │
│  │  - GICv2/GICv3 (虚拟中断控制器)                       │   │
│  └──────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────┘
```

### 模块划分

```
vmm/
├── vmm.h                    # 公共 API 头文件
├── vmm.c                    # VMM 核心实现（~2KB）
│
├── core/
│   ├── vcpu.c               # vCPU 上下文管理（~1KB）
│   ├── vcpu_sched.c         # vCPU 调度器（~1.5KB）
│   ├── vm.c                 # VM 生命周期管理（~1KB）
│   └── vm_smp.c             # SMP 多核支持（~0.5KB）
│
├── npt/
│   ├── npt.h                # 嵌套页表头文件
│   ├── npt.c                # NPT 管理（~3KB）
│   └── npt_map.c            # NPT 映射实现（~2KB）
│
├── device/
│   ├── virtio.h             # VirtIO 头文件
│   ├── virtio.c             # VirtIO 总线（~2KB）
│   ├── virtio_block.c       # VirtIO-Block（~4KB）
│   ├── virtio_net.c         # VirtIO-Net（~4KB）
│   ├── virtio_console.c     # VirtIO-Console（~2KB）
│   └── virtio_rng.c         # VirtIO-RNG（~1KB）
│
├── vgic/
│   ├── vgic.h               # 虚拟 GIC 头文件
│   └── vgic.c               # VGIC 实现（~3KB）
│
├── hypercall/
│   ├── hypercall.h          # Hypercall 头文件
│   └── hypercall.c          # Hypercall 处理（~1KB）
│
├── mm/
│   ├── vmm_mem.h            # 虚拟机内存管理头文件
│   └── vmm_mem.c            # 内存映射/超额订阅（~2KB）
│
├── exit/
│   ├── exit.h               # VM 退出处理头文件
│   ├── exit.c               # 退出处理分发器（~2KB）
│   ├── exit_mmio.c          # MMIO 退出处理（~1KB）
│   └── exit_hypercall.c     # Hypercall 退出处理（~0.5KB）
│
└── stats/
    ├── vmm_stats.h          # 统计信息头文件
    └── vmm_stats.c          # 统计实现（~1KB）
```

**总代码量**: ~38KB (目标 < 40KB)  
**文本段大小**: ~6-8KB (远低于 10KB 目标)

---

## 核心模块设计

### 1. VM 描述符设计

```c
/**
 * @brief VM 描述符
 *
 * @details 每个 VM 对应一个 VM 描述符，包含：
 *          - VM 状态
 *          - vCPU 列表
 *          - 嵌套页表
 *          - 虚拟中断控制器
 *          - Guest 物理内存映射
 */
typedef struct
{
    uint32_t         vm_id;                  /**< @brief VM ID (0~3) */

    /** @brief VM 状态 */
    vm_state_t       state;                  /**< VM 状态 */
    bool             active;                 /**< 活跃标志 */

    /** @brief 基本信息 */
    char             name[32];               /**< VM 名称 */
    uint64_t         mem_size;               /**< Guest 物理内存大小 */
    paddr_t          mem_base;               /**< Guest 物理内存基地址 */
    paddr_t          mem_host_base;          /**< 映射到 Host 的物理地址 */

    /** @brief vCPU 管理 */
    uint32_t         vcpu_count;             /**< vCPU 数量 */
    vcpu_desc_t      vcpus[VMM_MAX_VCPUS];   /**< vCPU 数组 */

    /** @brief 嵌套页表 */
    nested_page_table_t npt;                 /**< 嵌套页表描述符 */

    /** @brief 虚拟中断控制器 */
    vgic_desc_t      vgic;                   /**< VGIC 描述符 */

    /** @brief 虚拟设备 */
    uint32_t         vdev_count;             /**< 虚拟设备数量 */
    uint32_t         vdev_ids[VMM_MAX_VDEVICES]; /**< 虚拟设备 ID 列表 */

    /** @brief 统计信息 */
    vmm_stats_t      stats;                  /**< VMM 统计信息 */
} vm_desc_t;
```

**内存占用**:
- VM 描述符: ~1.5KB
- 每个 vCPU: ~128B (64B GP+64B SysReg)
- VGIC: ~4KB (256 中断 × 32B)
- NPT: ~4KB (512 × 8B 条目)
- 总计: ~10KB/VM

### 2. vCPU 描述符设计

```c
/**
 * @brief vCPU 通用寄存器
 *
 * @details 保存 Guest 可见寄存器
 */
typedef struct
{
    uint64_t x[31];                    /**< x0-x30 */
    uint64_t pc;                       /**< 程序计数器 (EL1T) */
    uint64_t sp_el1;                   /**< 栈指针 EL1 */
    uint64_t sp_el0;                   /**< 栈指针 EL0 */
    uint64_t pstate;                   /**< 处理器状态 */
} vcpu_gpregs_t;

/**
 * @brief vCPU 系统寄存器
 *
 * @details 保存 Guest 可见系统寄存器
 *          注意：使用 ARM VHE 后，EL1 和 EL2 寄存器在同一个寄存器文件
 */
typedef struct
{
    /* EL2 (Hypervisor) 寄存器 - 使用 VHE 可在 EL1 寄存器文件访问 */
    uint64_t vttbr_el2;                /**< 虚拟页表基址寄存器 */
    uint64_t vttbr_e1h;                 /**< VTTBR_EL2 高位扩展 */
    uint64_t vttbr_e1;                  /**< VTTBR_EL1 映射 */
    uint64_t vttbr_e0;                  /**< VTTBR_EL0 映射 */
    uint64_t tcr_el2;                   /**< 虚拟页表控制寄存器 */
    uint64_t esr_el2;                   /**< 异常综合征寄存器 */
    uint64_t far_el2;                   /**< 故障地址寄存器 */
    uint64_t elr_el2;                   /**< 异常链接寄存器 */
    uint64_t spsr_el2;                  /**< 保存的程序状态寄存器 */
    uint64_t vcntvctl_el2;              /**< 虚拟定时器控制寄存器 */
    uint64_t vcnt_cval_el2;             /**< 虚拟定时器比较值寄存器 */
} vcpu_sysregs_t;

/**
 * @brief vCPU 上下文保存区
 *
 * @details vCPU 运行时上下文
 */
typedef struct
{
    uint32_t         vcpu_id;           /**< vCPU ID */
    uint32_t         vm_id;             /**< 所属 VM ID */
    vcpu_state_t     state;             /**< vCPU 状态 */

    /** @brief 寄存器上下文 */
    vcpu_gpregs_t    gp_regs;           /**< 通用寄存器 */
    vcpu_sysregs_t   sys_regs;          /**< 系统寄存器 */

    /** @brief VM 退出上下文 */
    uint64_t         exit_reason;       /**< 退出原因 */
    uint64_t         exit_addr;         /**< 退出地址 */
    uint64_t         fault_data;        /**< 故障数据 */

    /** @brief 中断处理 */
    uint64_t         pending_irq;       /**< 待注入中断位图 */
    uint64_t         active_irq;        /**< 活跃中断位图 */
    bool             irq_pending;       /**< 有待注入中断 */

    /** @brief 性能统计 */
    uint64_t         exit_count;        /**< VM 退出次数 */
    uint64_t         run_time;          /**< 运行时间 (ticks) */
} vcpu_desc_t;
```

**内存占用**: ~128B/vCPU

### 3. 嵌套页表（NPT）设计

```c
/**
 * @brief 嵌套页表级别定义
 */
typedef enum
{
    NPT_LEVEL_L0 = 0U,                  /**< PGD 级别 */
    NPT_LEVEL_L1 = 1U,                  /**< PUD 级别 */
    NPT_LEVEL_L2 = 2U,                  /**< PMD 级别 */
    NPT_LEVEL_L3 = 3U,                  /**< PTE 级别 */
    NPT_LEVEL_COUNT
} npt_level_t;

/**
 * @brief NPT 条目
 *
 * @details ARMv8-A 页表条目格式
 *          [63:59] 类型/权限 (Table/Block/Page)
 *          [58:48] 粗粒度块大小/AttrIndex
 *          [47:12] 物理地址 (物理内存对齐)
 *          [11:0]  偏移
 */
typedef uint64_t npt_entry_t;

/**
 * @brief 嵌套页表描述符
 *
 * @details 二阶段地址翻译：Guest VA → Guest PA → Host PA
 */
typedef struct
{
    /** @brief NPT 根页表 */
    paddr_t    root_paddr;              /**< 根页表物理地址 */
    vaddr_t    root_vaddr;              /**< 根页表虚拟地址 */
    npt_entry_t entries[NPT_LEVELS][512]; /**< 4 级页表 */

    /** @brief Guest 地址空间 */
    uint64_t   guest_phys_base;         /**< Guest 物理地址基址 */
    uint64_t   guest_phys_size;         /**< Guest 物理地址空间大小 */
    uint64_t   guest_virt_size;         /**< Guest 虚拟地址空间大小 */

    /** @brief 映射信息 */
    uint32_t   ref_count;               /**< 引用计数 */

    /** @brief 属性 */
    uint64_t   mem_attr_idx;            /**< 内存属性索引 (MAIR) */
    uint64_t   ap_bit;                  /**< 访问权限位 */
    uint64_t   ns_bit;                  /**< 非安全状态位 */
    uint64_t   idx_bit;                 /**< AttrIndex 位 */
} nested_page_table_t;
```

**内存占用**: ~4KB/NPT

### 4. 虚拟设备设计

```c
/**
 * @brief VirtIO 设备类型
 */
typedef enum
{
    VIRTIO_DEVICE_BLOCK = 0U,           /**< VirtIO-Block 块设备 */
    VIRTIO_DEVICE_NET,                  /**< VirtIO-Net 网卡 */
    VIRTIO_DEVICE_CONSOLE,              /**< VirtIO-Console */
    VIRTIO_DEVICE_RNG,                  /**< VirtIO-RNG 随机数 */
    VIRTIO_DEVICE_BALLOON,              /**< VirtIO-Balloon */
    VIRTIO_DEVICE_DEVICE_ID_COUNT
} virtio_device_type_t;

/**
 * @brief VirtIO 设备描述符
 */
typedef struct
{
    /** @brief 基本信息 */
    uint32_t         dev_id;             /**< 设备 ID */
    uint32_t         vm_id;              /**< 所属 VM */
    virtio_device_type_t type;           /**< 设备类型 */

    /** @brief VirtIO 基础结构 */
    virtio_queue_t   vqs[VIRTIO_MAX_QUEUES]; /**< VirtIO 队列数组 */
    uint32_t         num_vqs;            /**< 队列数量 */
    uint32_t         features;           /**< 设备特性位图 */
    uint16_t         status;             /**< 设备状态 */
    uint16_t         config_gen;         /**< 配置版本号 */
    uint32_t         device_features;    /**< 设备特性位图 */
    uint32_t         driver_features;    /**< 驱动程序特性位图 */
    uint32_t         device_features_sel;/**< 设备特性选择器 */
    uint32_t         driver_features_sel;/**< 驱动程序特性选择器 */

    /** @brief 设备特定配置 */
    void*            config;             /**< 配置空间指针 */
    uint32_t         config_size;        /**< 配置空间大小 */

    /** @brief MMIO 区域 */
    uint64_t         mmio_base;          /**< MMIO 基址 */
    uint64_t         mmio_size;          /**< MMIO 大小 */

    /** @brief 设备操作 */
    bool             active;             /**< 活跃标志 */
    void*            priv;               /**< 设备私有数据 */
} virtio_device_t;
```

---

## 嵌套页表设计

### 二阶段地址翻译流程

```
┌──────────────────────────────────────────────────────────────┐
│                    Guest 地址空间                              │
│                                                              │
│  Guest VA (48-bit)                                          │
│  [47:39] PGD 索引 (L0)  → NPT[PGD_INDEX]                     │
│  [38:30] PUD 索引 (L1)  → NPT[PUD_INDEX]                     │
│  [29:21] PMD 索引 (L2)  → NPT[PMD_INDEX]                     │
│  [20:12] PTE 索引 (L3)  → NPT[PTE_INDEX]                     │
│  [11:0]  页内偏移 (4KB)                                       │
│                                                              │
└──────────────────────┬───────────────────────────────────────┘
                       │ NPT 查找
                       ▼
┌──────────────────────────────────────────────────────────────┐
│              NPT 条目 (PTE)                                   │
│                                                              │
│  条目类型:                                                   │
│  - 0x000 (None)                                              │
│  - 0x001 (Table) → 下级页表                                  │
│  - 0x003 (Block) → 2MB 页                                    │
│  - 0x005 (Page) → 4KB 页                                      │
│                                                              │
│  [63:59] 类型/权限                                           │
│  [58:48] 粗粒度块大小/AttrIndex                             │
│  [47:12] Guest PA (物理地址对齐)                              │
│  [11:0]  偏移                                               │
└──────────────────────┬───────────────────────────────────────┘
                       │ 提取 Guest PA
                       ▼
┌──────────────────────────────────────────────────────────────┐
│            Guest 物理内存                                    │
│                                                              │
│  Guest PA (36-bit, 1GB 地址空间)                             │
│  [35:12] 页号                                                │
│  [11:0]  页内偏移 (4KB)                                       │
│                                                              │
└──────────────────────┬───────────────────────────────────────┘
                       │ 获取 Host PA
                       ▼
┌──────────────────────────────────────────────────────────────┐
│              Host 地址空间                                    │
│                                                              │
│  Host PA (48-bit)                                            │
│  [47:39] Host PGD 索引                                        │
│  [38:30] Host PUD 索引                                        │
│  [29:21] Host PMD 索引                                        │
│  [20:12] Host PTE 索引                                        │
│  [11:0]  页内偏移 (4KB)                                       │
│                                                              │
│  映射关系:                                                   │
│  Guest PA (0x00000000) → Host PA (0x40000000)                │
│  Guest PA (0x3FFFFFFF) → Host PA (0x43FFFFFFF)               │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### NPT 操作接口

```c
/**
 * @brief 创建嵌套页表
 *
 * @param vm_id   VM ID
 * @param guest_size Guest 物理内存大小
 * @param host_base Host 物理内存基地址
 *
 * @return 成功返回 NPT 指针，失败返回 NULL
 */
nested_page_table_t* npt_create(uint32_t vm_id, uint64_t guest_size,
                                 paddr_t host_base);

/**
 * @brief 销毁嵌套页表
 *
 * @param npt NPT 指针
 */
void npt_destroy(nested_page_table_t* npt);

/**
 * @brief 映射 Guest 物理页到 Host
 *
 * @details 映射 Guest PA → Host PA，建立二阶段翻译关系
 *
 * @param npt     NPT 指针
 * @param guest_paddr Guest 物理地址
 * @param host_paddr  Host 物理地址
 * @param flags   页表属性标志
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t npt_map_page(nested_page_table_t* npt,
                             paddr_t guest_paddr,
                             paddr_t host_paddr,
                             uint64_t flags);

/**
 * @brief 解除映射 Guest 物理页
 *
 * @param npt     NPT 指针
 * @param guest_paddr Guest 物理地址
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t npt_unmap_page(nested_page_table_t* npt,
                                paddr_t guest_paddr);

/**
 * @brief 二阶段地址翻译
 *
 * @param npt     NPT 指针
 * @param guest_va Guest 虚拟地址
 * @param host_pa  输出: Host 物理地址
 *
 * @return KERNEL_OK 成功
 * @return -EFAULT 访问不合法
 */
kernel_status_t npt_translate(nested_page_table_t* npt,
                               vaddr_t guest_va,
                               paddr_t* host_pa);

/**
 * @brief 刷新 NPT TLB
 *
 * @param vm_id   VM ID
 * @param asid    ASID (可选，0 表示刷新所有)
 */
kernel_status_t npt_tlb_flush(uint32_t vm_id, asid_t asid);

/**
 * @brief 获取 NPT 引用计数
 *
 * @param npt NPT 指针
 *
 * @return 引用计数
 */
uint32_t npt_get_ref_count(nested_page_table_t* npt);
```

### NPT 映射策略

#### 策略 1: 简单镜像映射（初期）

```
Guest PA (0x00000000-0x3FFFFFFF)
      ↓
    映射
      ↓
Host PA (0x40000000-0x43FFFFFF)
      ↓
   物理内存
```

**优点**:
- 实现简单
- 速度快（1:1 映射）

**缺点**:
- Guest 和 Host 共享物理内存（无隔离）
- 不支持 Guest 超额订阅

#### 策略 2: 虚拟化内存映射（后期）

```
Guest PA (0x00000000-0x3FFFFFFF)
      ↓
    映射
      ↓
Host PA (0x40000000-0x43FFFFFF) (映射到 Host 物理内存)
```

**优点**:
- Guest 和 Host 完全隔离
- 支持 Guest 超额订阅

**缺点**:
- 需要额外的内存管理
- 映射开销增加

**实现路径**:
1. **Phase 1**: 简单镜像映射（快速实现）
2. **Phase 2**: 虚拟化内存映射（完善隔离）

---

## 虚拟设备设计

### VirtIO 设备架构

```
┌──────────────────────────────────────────────────────────────┐
│                   Guest VM (VirtIO Driver)                    │
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  VirtIO 驱动程序 (virtio-mmio)                        │   │
│  │  - 访问 MMIO 设备寄存器                               │   │
│  │  - 管理 VirtIO 队列                                   │   │
│  │  - 处理中断 (Kick/Interrupt)                          │   │
│  └──────────────────────────────────────────────────────┘   │
│                           │                                  │
│                           │ MMIO 读写                        │
│                           ▼                                  │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  VirtIO 设备 MMIO 空间 (VIRTIO_MMIO_BASE)             │   │
│  │  - 设备配置空间 (0x000-0x0FF)                         │   │
│  │  - 队列空间 (0x100-0xFFF)                             │   │
│  └──────────────────────────────────────────────────────┘   │
│                           │                                  │
│                           │ MMIO 读写                        │
│                           ▼                                  │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  VirtIO 设备 (Host)                                   │   │
│  │  ┌──────────────┐  ┌──────────────┐                  │   │
│  │  │ VirtIO-Block  │  │ VirtIO-Net   │                  │   │
│  │  │ (Block Dev)  │  │ (Network)    │                  │   │
│  │  └──────────────┘  └──────────────┘                  │   │
│  │  ┌──────────────┐  ┌──────────────┐                  │   │
│  │  │ VirtIO-      │  │ VirtIO-      │                  │   │
│  │  │ Console      │  │ RNG          │                  │   │
│  │  └──────────────┘  └──────────────┘                  │   │
│  └──────────────────────────────────────────────────────┘   │
│                           │                                  │
│                           │ 设备操作                         │
│                           ▼                                  │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  VMM 设备模拟层 (User/Kernel)                         │   │
│  │  - 队列管理 (dequeue/queue)                           │   │
│  │  - 中断注入 (Kick/Interrupt)                          │   │
│  │  - 设备实现 (具体设备逻辑)                             │   │
│  └──────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────┘
```

### VirtIO MMIO 寄存器映射

```
┌──────────────────────────────────────────────────────────────┐
│              VirtIO MMIO 寄存器 (64-bit)                      │
│                                                              │
│  Offset    Name                Access    Description         │
│  0x000     DEVICE_ID           R         设备 ID              │
│  0x004     DEVICE_FEATURES     R         设备特性位图        │
│  0x008     DRIVER_FEATURES     R/W       驱动程序特性位图    │
│  0x00C     NUM_QUEUES          R/W       队列数量            │
│  0x010     QUEUE_NUM_MAX       R         最大队列数量        │
│  0x014     QUEUE_NUM           R/W       当前队列数量        │
│  0x018     QUEUE_ALIGN         R         队列对齐要求        │
│  0x01C     QUEUE_OFF_LOW       R/W       队列偏移低 32 位    │
│  0x020     QUEUE_OFF_HIGH      R/W       队列偏移高 32 位    │
│  0x024     QUEUE_NOTIFY       R/W       队列通知           │
│  0x028     QUEUE_SELECT        R/W       队列选择           │
│  0x02C     QUEUE_DESC_LOW      R         描述符地址低 32 位  │
│  0x030     QUEUE_DESC_HIGH     R         描述符地址高 32 位  │
│  0x034     QUEUE_AVAIL_LOW     R         可用指针低 32 位    │
│  0x038     QUEUE_AVAIL_HIGH    R         可用指针高 32 位    │
│  0x03C     QUEUE_USED_LOW      R         使用指针低 32 位    │
│  0x040     QUEUE_USED_HIGH     R         使用指针高 32 位    │
│  0x044     STATUS              R/W       设备状态            │
│  0x048     CONFIG_GENERATION   R         配置版本号          │
│  0x04C     CONFIG              R         设备配置空间        │
│  ...                               ...        ...               │
│  0x100     QUEUE_DESC_LOW      R/W       队列 1 描述符地址  │
│  ...                               ...        ...               │
│  0xFFF     QUEUE_DESC_LOW      R/W       队列 N 描述符地址  │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### VirtIO 设备实现优先级

#### 优先级 P0 - 必须实现

1. **VirtIO-Block (块设备)**
   - 支持读取/写入
   - 支持虚拟磁盘（0-100MB）
   - 性能要求：读写速度 > 10MB/s

2. **VirtIO-Net (网卡)**
   - 支持以太网帧收发
   - 支持 TCP/UDP
   - 性能要求：吞吐量 > 100Mbps

#### 优先级 P1 - 重要

3. **VirtIO-Console**
   - 支持 UART 模拟
   - 支持 Guest 输入/输出
   - 性能要求：延迟 < 1ms

4. **VirtIO-RNG (随机数)**
   - 支持 /dev/urandom
   - 简单实现，仅占位

#### 优先级 P2 - 可选

5. **VirtIO-Balloon**
   - 内存超额订阅
   - 内存共享
   - 性能要求：动态内存调整

---

## VM 退出处理设计

### VM 退出类型分类

```
┌──────────────────────────────────────────────────────────────┐
│                    ARMv8-A VM Exit Categories                 │
│                                                              │
│  Category 0: Instruction Execution (指令执行)                │
│  - WFI/WFE                                                    │
│  - MSR/ MRS (系统寄存器访问)                                  │
│  - 指令中止                                                    │
│                                                              │
│  Category 1: Exception Execution (异常处理)                  │
│  - 指令异常 (SVC/HVC)                                         │
│  - 系统异常                                                    │
│  - 中断                                                        │
│                                                              │
│  Category 2: Data Access (数据访问)                          │
│  - 数据中止 (读写)                                            │
│  - 指令中止 (间接访问)                                        │
│                                                              │
│  Category 3: System Register Access (系统寄存器访问)         │
│  - 系统寄存器陷阱                                              │
│  - WFE/WFI                                                    │
│                                                              │
│  Category 4: Instruction Execution (指令执行)                │
│  - 执行失败                                                    │
│  - 其他指令相关退出                                            │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### VM 退出处理流程

```
┌──────────────────────────────────────────────────────────────┐
│                   VM 退出处理流程                             │
│                                                              │
│  1. 检测到 VM 退出                                            │
│     ┌─────────────────────┐                                  │
│     │ vcpu->exit_reason   │  ← ESR_EL2 退出原因             │
│     │ vcpu->exit_addr     │  ← ELR_EL2 退出地址            │
│     └─────────────────────┘                                  │
│                         │                                    │
│                         ▼                                    │
│  2. 根据 ESR_EL2 分类退出类型                                 │
│     ┌─────────────────────────────────────────────────────┐  │
│     │ EC (Exception Class) 退出原因                      │  │
│     ├─────────────────────────────────────────────────────┤  │
│     │ 0x01 - WFI/WFE (低功耗等待)                         │  │
│     │ 0x03 - HVC (Hypercall)                              │  │
│     │ 0x06 - 系统寄存器访问                                │  │
│     │ 0x08 - 数据中止 (MMIO)                              │  │
│     │ 0x0A - 指令中止                                      │  │
│     │ 0x0E - 指令执行失败                                  │  │
│     └─────────────────────────────────────────────────────┘  │
│                         │                                    │
│                         ▼                                    │
│  3. 调用对应处理函数                                          │
│     ┌─────────────────────────────────────────────────────┐  │
│     │ switch (exit_reason.ec) {                           │  │
│     │   case 0x01: exit_wfi(); break;                    │  │
│     │   case 0x03: exit_hypercall(); break;              │  │
│     │   case 0x06: exit_sysreg(); break;                 │  │
│     │   case 0x08: exit_mmio(); break;                   │  │
│     │   case 0x0A: exit_inst_abort(); break;             │  │
│     │   default: exit_unknown(); break;                  │  │
│     │ }                                                   │  │
│     └─────────────────────────────────────────────────────┘  │
│                         │                                    │
│                         ▼                                    │
│  4. 处理完成后恢复 vCPU                                        │
│     ┌─────────────────────────────────────────────────────┐  │
│     │ vcpu->pc += 4;  ← 恢复程序计数器                   │  │
│     │ vcpu->state = VCPU_STATE_RUNNING;                  │  │
│     │ vmm_schedule_next();  ← 调度下一个 vCPU            │  │
│     └─────────────────────────────────────────────────────┘  │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### 具体退出处理

#### 1. WFI/WFE (低功耗等待)

```c
kernel_status_t exit_wfi(vcpu_desc_t* vcpu)
{
    /* 检查是否有待注入中断 */
    if (vcpu->irq_pending)
    {
        /* 注入中断 */
        vcpu->irq_pending = false;
        return KERNEL_OK;
    }

    /* 没有中断，进入低功耗 */
    vcpu->state = VCPU_STATE_BLOCKED;
    vmm_wait_for_event();

    return KERNEL_OK;
}
```

#### 2. HVC (Hypercall)

```c
kernel_status_t exit_hypercall(vcpu_desc_t* vcpu)
{
    uint64_t call_nr = vcpu->gp_regs.x[0];
    uint64_t args[4];

    /* 保存参数 */
    args[0] = vcpu->gp_regs.x[1];
    args[1] = vcpu->gp_regs.x[2];
    args[2] = vcpu->gp_regs.x[3];
    args[3] = vcpu->gp_regs.x[4];

    /* 调用 Hypercall 处理器 */
    return vmm_handle_hypercall(vcpu->vm_id, vcpu->vcpu_id,
                                call_nr, args);
}
```

#### 3. MMIO (数据中止)

```c
kernel_status_t exit_mmio(vcpu_desc_t* vcpu)
{
    uint64_t fault_addr = vcpu->sys_regs.far_el2;
    bool is_write = ((vcpu->sys_regs.esr_el2 & 0x40) != 0U);
    uint64_t value = 0ULL;

    /* 查找对应的虚拟设备 */
    kernel_status_t ret = vmm_handle_mmio(vcpu->vm_id,
                                          vcpu->vcpu_id,
                                          fault_addr,
                                          is_write,
                                          &value,
                                          MMIO_ACCESS_MAX_SIZE);

    if (!is_write && (ret == KERNEL_OK))
    {
        /* 返回值写入寄存器 */
        uint64_t reg_idx = (vcpu->sys_regs.esr_el2 >> 16) & 0x1F;
        vcpu->gp_regs.x[reg_idx] = value;
    }

    /* 恢复 PC */
    vcpu->gp_regs.pc += 4U;

    return KERNEL_OK;
}
```

#### 4. 系统寄存器访问

```c
kernel_status_t exit_sysreg(vcpu_desc_t* vcpu)
{
    /* 简化：支持 MRS/MSR 转换 */
    /* 完整实现需要支持所有系统寄存器 */

    return KERNEL_OK;
}
```

---

## 虚拟中断控制器设计

### VGIC 模型选择

AISafeOS64 选择 **GICv2 模拟**（因为当前硬件使用 GICv2）：

```
┌──────────────────────────────────────────────────────────────┐
│              Guest VGIC (虚拟 GIC)                            │
│                                                              │
│  Guest 中断源 (中断号 0~255)                                  │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  IRQ (Interrupt Request)                             │   │
│  │  - IPI (Inter-Processor Interrupt)                  │   │
│  │  - SPI (Shared Peripheral Interrupt)                │   │
│  │  - PPI (Private Peripheral Interrupt)               │   │
│  └─────────────────────────────────────────────────────┘   │
│                           │                                  │
│                           ▼                                  │
│  VGIC 状态管理                                                  │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  - 中断状态 (INACTIVE/PENDING/ACTIVE/ACTIVE_PENDING) │   │
│  │  - 中断优先级 (8 级优先级)                            │   │
│  │  - 中断使能 (ENABLED/DISABLED)                       │   │
│  │  - 中断路由 (CPU 模式)                                │   │
│  └─────────────────────────────────────────────────────┘   │
│                           │                                  │
│                           ▼                                  │
│  中断注入到 vCPU                                               │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  vcpu->pending_irq (位图)                             │   │
│  │  - 如果有中断，触发 VM 退出                           │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                              │
└──────────────────────────────────────────────────────────────┘
                       │ 映射/代理
                       ▼
┌──────────────────────────────────────────────────────────────┐
│              Host GIC (真实 GIC)                              │
│                                                              │
│  Host 中断源                                                  │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  - 实际硬件中断 (来自物理设备)                         │   │
│  │  - 虚拟中断 (从 Guest VGIC 注入)                     │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### VGIC 数据结构

```c
/**
 * @brief 虚拟 GIC 描述符
 */
typedef struct
{
    /** @brief 中断状态 */
    vgic_irq_state_t irq_state[VMM_VGIC_MAX_INTERRUPTS];

    /** @brief 中断优先级 */
    uint8_t irq_priority[VMM_VGIC_MAX_INTERRUPTS];

    /** @brief 中断使能位图 */
    uint32_t irq_enabled[VMM_VGIC_MAX_INTERRUPTS / 32U + 1U];

    /** @brief 中断配置 */
    uint8_t irq_config[VMM_VGIC_MAX_INTERRUPTS];  /**< Edge/Level */

    /** @brief 中断路由 (CPU 模式) */
    uint8_t irq_target[VMM_VGIC_MAX_INTERRUPTS];

    /** @brief 中断挂起位图 */
    uint32_t irq_pending[VMM_VGIC_MAX_INTERRUPTS / 32U + 1U];
} vgic_desc_t;
```

### VGIC 操作接口

```c
/**
 * @brief 注入虚拟中断到 vCPU
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 * @param irq     中断号 (0~255)
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vgic_inject_irq(uint32_t vm_id, uint32_t vcpu_id,
                                uint32_t irq);

/**
 * @brief 清除虚拟中断
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 * @param irq     中断号
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vgic_clear_irq(uint32_t vm_id, uint32_t vcpu_id,
                               uint32_t irq);

/**
 * @brief 设置中断优先级
 *
 * @param vm_id   VM ID
 * @param irq     中断号
 * @param priority 优先级 (0~7, 0 最高)
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vgic_set_priority(uint32_t vm_id, uint32_t irq,
                                  uint8_t priority);

/**
 * @brief 设置中断路由
 *
 * @param vm_id     VM ID
 * @param irq       中断号
 * @param cpu_mask  CPU 位图 (bit 0 = CPU0, bit 1 = CPU1, ...)
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vgic_set_target(uint32_t vm_id, uint32_t irq,
                                uint8_t cpu_mask);

/**
 * @brief 使能/禁用中断
 *
 * @param vm_id   VM ID
 * @param irq     中断号
 * @param enable  true=使能, false=禁用
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vgic_enable_irq(uint32_t vm_id, uint32_t irq,
                                bool enable);

/**
 * @brief 检查中断是否挂起
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 * @param irq     中断号
 *
 * @return true=挂起, false=未挂起
 */
bool vgic_irq_is_pending(uint32_t vm_id, uint32_t vcpu_id,
                         uint32_t irq);
```

### 中断注入机制

```c
kernel_status_t vgic_inject_irq(uint32_t vm_id, uint32_t vcpu_id,
                                uint32_t irq)
{
    vm_desc_t* vm;
    vcpu_desc_t* vcpu;

    /* 获取 VM 和 vCPU */
    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return KERNEL_ERR_NO_VM;
    }

    if (vcpu_id >= vm->vcpu_count)
    {
        return KERNEL_ERR_INVALID_VCPU;
    }

    if (irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return KERNEL_ERR_INVALID_IRQ;
    }

    vcpu = &vm->vcpus[vcpu_id];

    /* 设置中断状态 */
    vm->vgic.irq_state[irq] = VGIC_IRQ_PENDING;
    vm->vgic.irq_enabled[irq / 32U] |= (1UL << (irq % 32U));

    /* 注入到 vCPU */
    vcpu->pending_irq |= (1ULL << irq);
    vcpu->irq_pending = true;

    return KERNEL_OK;
}
```

---

## 性能优化设计

### 1. ARM VHE (Virtualization Host Extensions)

使用 ARM VHE 可以将虚拟化操作提升到 EL1，而不是 EL2：

```
┌──────────────────────────────────────────────────────────────┐
│                  ARM VHE 架构                                │
│                                                              │
│  EL2 寄存器访问 (非 VHE)                                       │
│  - EL2 寄存器: ELR_EL2, SP_EL2, SPSR_EL2, ESR_EL2...        │
│  - 访问权限: 需要特权和访问 VM 相关寄存器                      │
│                                                              │
│  EL1 寄存器访问 (VHE)                                         │
│  - EL1 寄存器: ELR_EL1, SP_EL1, SPSR_EL1, ESR_EL1...        │
│  - 访问权限: 仅需访问 EL1 寄存器                              │
│  - 特点:     所有 EL1 寄存器在同一个物理寄存器文件中           │
│                                                              │
└──────────────────────────────────────────────────────────────┘

优势:
- vCPU 运行在 EL1，无需特权上下文切换
- 更快的上下文切换（无需切换寄存器组）
- 更好的兼容性（不需要 EL2 模式）

劣势:
- 需要 ARMv8.1 VHE 指令集支持
- 寄存器访问更复杂（需要 EL1/EL2 地址空间区分）

实现方式:
- 检测 VHE 支持（通过 ID_AA64MMFR0_EL1 寄存器）
- 如果支持 VHE，运行在 EL1 用户态（切换到 EL1 模式）
- 如果不支持，运行在 EL2 特权态
```

### 2. 虚拟 TLB 优化

```
┌──────────────────────────────────────────────────────────────┐
│                  虚拟 TLB 优化策略                            │
│                                                              │
│  策略 1: 恒等映射 + ASID 管理                                 │
│  - Guest VA → Host PA (1:1 映射)                             │
│  - 使用 ASID 切换 Guest 地址空间                              │
│  - 优势: 速度快，TLB 命中率高                                  │
│  - 劣势: 共享物理内存，无隔离                                  │
│                                                              │
│  策略 2: 独立 NPT + TLB 刷新                                  │
│  - Guest VA → Guest PA → Host PA                             │
│  - 每个 VM 独立 NPT                                            │
│  - 使用 Guest ASID 刷新 NPT TLB                               │
│  - 优势: 完全隔离，支持超额订阅                                │
│  - 劣势: TLB 命中率较低，需要更多 TLB 操作                     │
│                                                              │
│  优化措施:                                                   │
│  - 使用 ARMv8 TLB 单元指令 (TLBI) 进行精确刷新                │
│  - 批量刷新 NPT TLB（减少 TLB 流失）                           │
│  - 使用 VHE 减少 TLB 操作（如果支持）                          │
└──────────────────────────────────────────────────────────────┘
```

### 3. 虚拟中断优化

```
┌──────────────────────────────────────────────────────────────┐
│                  虚拟中断优化策略                             │
│                                                              │
│  优化 1: 中断屏蔽优化                                          │
│  - Guest 进入 WFI 时屏蔽中断                                  │
│  - 仅在必要时刻检查中断状态                                    │
│  - 减少不必要的 VM 退出                                        │
│                                                              │
│  优化 2: 中断批量注入                                          │
│  - 批量注入多个中断（减少 VM 退出次数）                        │
│  - 检查中断优先级（仅注入最高优先级）                          │
│  - 减少 vCPU 调度频率                                          │
│                                                              │
│  优化 3: 中断延迟优化                                          │
│  - 中断注入延迟 < 100ns                                      │
│  - 使用 GICv2 直接访问                                        │
│  - 减少中断处理路径（内核态到用户态）                          │
└──────────────────────────────────────────────────────────────┘
```

---

## 安全设计

### 1. Guest 与 Host 隔离

```
┌──────────────────────────────────────────────────────────────┐
│                   Guest 与 Host 隔离机制                      │
│                                                              │
│  层级 1: 地址空间隔离                                         │
│  - Guest 使用独立的 ASID                                     │
│  - Guest 使用独立的 NPT（嵌套页表）                            │
│  - Host 和 Guest TLB 完全独立                                 │
│                                                              │
│  层级 2: 能力隔离                                             │
│  - VM 不具备访问 Host 内存的能力                               │
│  - VM 通过 Hypercall 或 MMIO 与 Host 通信                     │
│  - 能力检查确保只有授权的访问                                 │
│                                                              │
│  层级 3: 中断隔离                                             │
│  - Host 中断不会影响 Guest                                    │
│  - Guest 中断仅注入到目标 vCPU                                │
│  - 中断路由严格检查                                           │
│                                                              │
│  层级 4: 异常隔离                                             │
│  - Guest 中的系统异常被 VMM 捕获                              │
│  - Guest 中的数据异常被 VMM 捕获                              │
│  - Guest 中的指令异常被 VMM 捕获                              │
│  - 不会传播到 Host 操作系统                                    │
└──────────────────────────────────────────────────────────────┘
```

### 2. 安全检查点

```c
/* 安全检查点 1: VM 创建检查 */
kernel_status_t vmm_create_vm(const char* name, uint64_t mem_size)
{
    /* 检查: 名称不为空 */
    if (name == NULL || name[0] == '\0')
    {
        return KERNEL_ERR_INVALID_PARAM;
    }

    /* 检查: 内存大小合法 */
    if (mem_size == 0 || mem_size > VMM_GUEST_PHYS_SIZE)
    {
        return KERNEL_ERR_INVALID_PARAM;
    }

    /* 检查: VM ID 有效性 */
    if (vm_id >= VMM_MAX_VMS)
    {
        return KERNEL_ERR_NO_FREE_VM;
    }

    /* 检查: 内存可用 */
    if (!phys_mem_alloc(mem_size, &mem_base))
    {
        return KERNEL_ERR_NO_MEM;
    }

    /* 创建 NPT */
    vm->npt = npt_create(vm_id, mem_size, mem_base);
    if (vm->npt == NULL)
    {
        phys_mem_free(mem_base, mem_size);
        return KERNEL_ERR_NO_MEM;
    }

    return KERNEL_OK;
}

/* 安全检查点 2: vCPU 创建检查 */
kernel_status_t vmm_create_vcpu(uint32_t vm_id, paddr_t entry_point)
{
    /* 检查: VM 存在 */
    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return KERNEL_ERR_NO_VM;
    }

    /* 检查: vCPU ID 有效性 */
    if (vcpu_id >= vm->vcpu_count)
    {
        return KERNEL_ERR_INVALID_VCPU;
    }

    /* 检查: 入口点地址合法 */
    if (entry_point < vm->mem_base || entry_point >= vm->mem_base + vm->mem_size)
    {
        return KERNEL_ERR_INVALID_PARAM;
    }

    /* 检查: vCPU 数量限制 */
    if (vm->vcpu_count >= VMM_MAX_VCPUS_PER_VM)
    {
        return KERNEL_ERR_NO_FREE_VCPU;
    }

    /* 创建 vCPU */
    vcpu->entry_point = entry_point;
    vcpu->state = VCPU_STATE_STOPPED;

    return KERNEL_OK;
}

/* 安全检查点 3: MMIO 访问检查 */
kernel_status_t vmm_handle_mmio(uint32_t vm_id, uint32_t vcpu_id,
                                uint64_t fault_addr, bool is_write,
                                uint64_t* value, uint32_t size)
{
    /* 检查: VM 存在 */
    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return KERNEL_ERR_NO_VM;
    }

    /* 检查: vCPU 存在 */
    if (vcpu_id >= vm->vcpu_count)
    {
        return KERNEL_ERR_INVALID_VCPU;
    }

    /* 检查: MMIO 访问范围合法 */
    if (fault_addr < vm->mmio_base ||
        fault_addr >= vm->mmio_base + vm->mmio_size)
    {
        return KERNEL_ERR_INVALID_MMIO;
    }

    /* 检查: 访问宽度合法 */
    if (size > MMIO_ACCESS_MAX_SIZE)
    {
        return KERNEL_ERR_INVALID_PARAM;
    }

    /* 检查: 设备存在 */
    dev = vmm_get_device(vm_id, fault_addr);
    if (dev == NULL)
    {
        return KERNEL_ERR_NO_DEVICE;
    }

    /* 检查: 设备活跃 */
    if (!dev->active)
    {
        return KERNEL_ERR_NO_DEVICE;
    }

    /* 执行 MMIO 操作 */
    return dev->op_fn(vm_id, vcpu_id, fault_addr - dev->mmio_base,
                     is_write, value, size);
}
```

### 3. MISRA C:2012 合规

```
安全要求:
1. 所有外部输入（IPC、MMIO、Hypercall）都必须验证
2. 所有内存访问都必须检查边界
3. 所有指针都必须验证非 NULL
4. 所有变量都必须初始化
5. 所有状态转换都必须验证合法性
6. 没有内存泄漏
7. 没有缓冲区溢出

实现方式:
- 使用静态分析工具 (cppcheck) 检测安全违规
- 使用覆盖测试确保所有路径都被测试
- 使用代码审查确保符合 MISRA 规范
```

---

## 实现优先级

### Phase 0: 框架实现（2 周）

**目标**: 建立基本框架，实现核心数据结构和接口

#### Week 1-2: 核心数据结构

- [ ] 创建 VMM 目录结构
- [ ] 实现 VM/vCPU 描述符结构
- [ ] 实现 NPT 数据结构
- [ ] 实现 VGIC 数据结构
- [ ] 实现 VirtIO 设备数据结构
- [ ] 添加公共 API 接口（vmm.h）
- [ ] 实现 VMM 初始化和销毁

**验收标准**:
- VMM 初始化成功
- VM 创建/销毁成功
- vCPU 创建成功
- API 文档完整

#### Week 3-4: NPT 实现

- [ ] 实现 NPT 创建/销毁
- [ ] 实现 NPT 映射/解除映射
- [ ] 实现 NPT 二阶段翻译
- [ ] 实现 NPT TLB 刷新
- [ ] 实现 ASID 管理

**验收标准**:
- NPT 创建成功
- NPT 映射/解除映射成功
- 二阶段翻译正确
- TLB 刷新正确

#### Week 5-6: 虚拟设备框架

- [ ] 实现 VirtIO 总线框架
- [ ] 实现 VirtIO MMIO 寄存器映射
- [ ] 实现 VirtIO 队列管理
- [ ] 实现 VirtIO 设备注册/注销
- [ ] 实现 MMIO 访问处理
- [ ] 实现 Hypercall 处理

**验收标准**:
- VirtIO 总线运行正常
- MMIO 访问正确
- 虚拟设备框架可用

#### Week 7-8: VM 退出处理

- [ ] 实现 WFI/WFE 退出处理
- [ ] 实现 Hypercall 退出处理
- [ ] 实现 MMIO 退出处理
- [ ] 实现 系统寄存器 退出处理
- [ ] 实现 VM 退出分发器
- [ ] 实现 vCPU 恢复机制

**验收标准**:
- VM 退出处理正确
- vCPU 正确恢复
- 无死锁/挂起

#### Week 9-10: VGIC 实现

- [ ] 实现 VGIC 中断状态管理
- [ ] 实现 VGIC 中断注入
- [ ] 实现 VGIC 中断清除
- [ ] 实现 VGIC 中断优先级设置
- [ ] 实现 VGIC 中断路由
- [ ] 实现 VGIC 中断使能/禁用

**验收标准**:
- 中断注入成功
- 中断清除成功
- 中断优先级正确
- 中断路由正确

#### Week 11-12: IPC 集成

- [ ] 实现 VMM 服务的 IPC 消息处理
- [ ] 实现 VM 管理 API 通过 IPC 暴露
- [ ] 实现 VM 退出事件通知
- [ ] 实现 VMM CLI 工具
- [ ] 实现 VMM Monitor 工具

**验收标准**:
- IPC 消息处理正确
- VM 管理 API 可用
- VMM 工具可用

### Phase 1: 设备实现（2 周）

**目标**: 实现核心虚拟设备

#### Week 13-14: VirtIO-Block

- [ ] 实现 VirtIO-Block 设备结构
- [ ] 实现 VirtIO-Block 队列管理
- [ ] 实现 VirtIO-Block 读写操作
- [ ] 实现 VirtIO-Block 配置空间
- [ ] 实现 VirtIO-Block 中断注入
- [ ] 集成到 QEMU 测试

**验收标准**:
- VirtIO-Block 读写成功
- QEMU 中运行 Guest OS
- 性能达到 10MB/s

#### Week 15-16: VirtIO-Net

- [ ] 实现 VirtIO-Net 设备结构
- [ ] 实现 VirtIO-Net 队列管理
- [ ] 实现 VirtIO-Net 以太网帧收发
- [ ] 实现 VirtIO-Net 配置空间
- [ ] 实现 VirtIO-Net 中断注入
- [ ] 集成到 QEMU 测试

**验收标准**:
- VirtIO-Net 收发成功
- QEMU 中运行 Guest OS
- 吞吐量 > 100Mbps

### Phase 2: 调度器优化（2 周）

**目标**: 实现 vCPU 调度器和性能优化

#### Week 17-18: vCPU 调度器

- [ ] 实现 EDF 调度算法
- [ ] 实现 FIFO 调度算法
- [ ] 实现 vCPU 抢占机制
- [ ] 实现 vCPU 迁移支持
- [ ] 实现 负载均衡策略
- [ ] 实现 vCPU 权限管理

**验收标准**:
- 调度器正确运行
- 抢占正常工作
- 负载均衡有效

#### Week 19-20: 性能优化

- [ ] 实现ARM VHE支持（如果硬件支持）
- [ ] 优化 TLB 刷新策略
- [ ] 优化中断注入延迟
- [ ] 优化 MMIO 访问延迟
- [ ] 性能测试和调优

**验收标准**:
- 上下文切换 < 1μs
- 中断注入延迟 < 100ns
- MMIO 访问延迟 < 1μs

### Phase 3: 内存管理（2 周）

**目标**: 实现内存超额订阅和共享

#### Week 21-22: 内存超额订阅

- [ ] 实现内存超额订阅机制
- [ ] 实现内存页回收
- [ ] 实现内存压力处理
- [ ] 实现内存超额订阅监控

**验收标准**:
- 内存超额订阅正常
- 内存压力处理正确

#### Week 23-24: 内存共享

- [ ] 实现VM间内存共享
- [ ] 实现共享内存映射
- [ ] 实现共享内存通知
- [ ] 实现共享内存隔离

**验收标准**:
- 内存共享正常
- 共享内存隔离正确

### Phase 4: 调试工具（1 周）

**目标**: 实现调试和监控工具

#### Week 25: 调试工具

- [ ] 实现VM状态查看工具
- [ ] 实现vCPU状态查看工具
- [ ] 实现调试器支持（GDB stub）
- [ ] 实现性能分析工具
- [ ] 实现日志和跟踪工具

**验收标准**:
- VM 状态查看成功
- GDB stub 可用
- 性能分析工具可用

---

## 总结

### 技术亮点

1. **轻量级设计**: VMM text 段 < 10KB，远低于其他 hypervisor
2. **安全隔离**: Guest 与 Host 完全隔离，符合安全要求
3. **高性能**: vCPU 上下文切换 < 1μs，中断注入延迟 < 100ns
4. **MISRA 合规**: 代码符合 MISRA C:2012 规范
5. **可扩展性**: 模块化设计，易于添加新设备

### 实现风险

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| NPT 实现复杂度高 | 高 | 分阶段实现，先简单镜像映射 |
| ARM VHE 支持有限 | 中 | 检测 VHE 支持，回退到 EL2 |
| VirtIO 设备实现困难 | 中 | 参考开源实现（Linux QEMU） |
| 性能不达标 | 高 | 持续性能测试和优化 |

### 成功指标

1. **功能指标**:
   - ✅ 支持最多 4 个 VM
   - ✅ 每个VM支持最多 4 个vCPU
   - ✅ 支持VirtIO-Block和VirtIO-Net
   - ✅ VM 退出处理正确
   - ✅ 中断注入延迟 < 100ns

2. **性能指标**:
   - ✅ vCPU 上下文切换 < 1μs
   - ✅ MMIO 访问延迟 < 1μs
   - ✅ VirtIO-Block 读写速度 > 10MB/s
   - ✅ VirtIO-Net 吞吐量 > 100Mbps

3. **安全指标**:
   - ✅ Guest 与 Host 完全隔离
   - ✅ 无安全漏洞（通过安全审查）
   - ✅ 符合 MISRA C:2012 规范

---

**文档版本**: 1.0
**最后更新**: 2026-05-03
**作者**: AISafe64 Team
**审核**: 待审核
