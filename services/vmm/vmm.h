/**
 * @file    vmm.h
 * @brief   虚拟机管理器（VMM）接口
 * @author  AISafe64 Team
 * @date    2026-04-04
 * @version 3.0
 *
 * @details 本文件定义了 ARMv8-A 虚拟机管理器接口：
 *          - vCPU 管理（创建/销毁/运行/暂停）
 *          - 二阶段地址翻译（嵌套页表 NPT）
 *          - 虚拟中断注入
 *          - 虚拟设备模拟框架
 *          - VM 退出处理分发
 *          - hypercall 处理
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef SERVICES_VMM_VMM_H
#define SERVICES_VMM_VMM_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================
 * VMM 常量
 * ======================================================================== */

/** @brief 最大虚拟机数量 */
#define VMM_MAX_VMS                4U

/** @brief 每 VM 最大 vCPU 数量 */
#define VMM_MAX_VCPUS_PER_VM      4U

/** @brief 嵌套页表级别数（ARMv8 4级） */
#define VMM_NPT_LEVELS             4U

/** @brief Guest 物理地址空间大小（1GB） */
#define VMM_GUEST_PHYS_SIZE        0x40000000ULL

/** @brief 中断向量表大小 */
#define VMM_VGIC_MAX_INTERRUPTS    256U

/* ========================================================================
 * VM 状态
 * ======================================================================== */

/**
 * @brief 虚拟机状态
 */
typedef enum
{
    VM_STATE_NONE = 0U,        /**< @brief 未创建 */
    VM_STATE_CREATED,          /**< @brief 已创建 */
    VM_STATE_RUNNING,          /**< @brief 运行中 */
    VM_STATE_PAUSED,           /**< @brief 已暂停 */
    VM_STATE_STOPPED           /**< @brief 已停止 */
} vm_state_t;

/**
 * @brief vCPU 状态
 */
typedef enum
{
    VCPU_STATE_OFF = 0U,       /**< @brief 未激活 */
    VCPU_STATE_STOPPED,        /**< @brief 已停止 */
    VCPU_STATE_RUNNING,        /**< @brief 运行中 */
    VCPU_STATE_BLOCKED         /**< @brief 阻塞（等待中断） */
} vcpu_state_t;

/* ========================================================================
 * vCPU 上下文
 * ======================================================================== */

/**
 * @brief vCPU 系统寄存器保存区
 *
 * @details 保存 Guest 可见的系统寄存器
 */
typedef struct
{
    uint64_t sctlr_el1;     /**< @brief 系统控制寄存器 */
    uint64_t ttbr0_el1;     /**< @brief 页表基址寄存器 0 */
    uint64_t ttbr1_el1;     /**< @brief 页表基址寄存器 1 */
    uint64_t tcr_el1;       /**< @brief 页表控制寄存器 */
    uint64_t mair_el1;      /**< @brief 内存属性间接寄存器 */
    uint64_t amair_el1;     /**< @brief 辅助内存属性寄存器 */
    uint64_t vbar_el1;      /**< @brief 向量基地址寄存器 */
    uint64_t esr_el1;       /**< @brief 异常综合征寄存器 */
    uint64_t far_el1;       /**< @brief 故障地址寄存器 */
    uint64_t elr_el1;       /**< @brief 异常链接寄存器 */
    uint64_t spsr_el1;      /**< @brief 保存的程序状态寄存器 */
    uint64_t sp_el1;        /**< @brief 栈指针 EL1 */
    uint64_t sp_el0;        /**< @brief 栈指针 EL0 */
    uint64_t cntvctl_el0;   /**< @brief 虚拟定时器控制 */
    uint64_t cntv_cval_el0; /**< @brief 虚拟定时器比较值 */
} vcpu_sysregs_t;

/**
 * @brief vCPU 通用寄存器保存区
 */
typedef struct
{
    uint64_t x[31];         /**< @brief x0-x30 */
    uint64_t pc;            /**< @brief 程序计数器 */
    uint64_t pstate;        /**< @brief 处理器状态 */
} vcpu_gpregs_t;

/**
 * @brief vCPU 描述符
 */
typedef struct
{
    uint32_t        vcpu_id;          /**< @brief vCPU ID */
    uint32_t        vm_id;            /**< @brief 所属 VM ID */
    vcpu_state_t    state;            /**< @brief vCPU 状态 */
    vcpu_gpregs_t   gp_regs;          /**< @brief 通用寄存器 */
    vcpu_sysregs_t  sys_regs;         /**< @brief 系统寄存器 */
    paddr_t         entry_point;      /**< @brief 入口点物理地址 */
    uint64_t        pending_irq;      /**< @brief 待注入中断位图 */
    bool            irq_pending;      /**< @brief 有待注入中断 */
} vcpu_desc_t;

/* ========================================================================
 * 嵌套页表（NPT）
 * ======================================================================== */

/**
 * @brief 嵌套页表描述符
 *
 * @details 二阶段地址翻译：Guest VA → Guest PA → Host PA
 */
typedef struct
{
    paddr_t    root_paddr;         /**< @brief NPT 根页表物理地址 */
    vaddr_t    root_vaddr;         /**< @brief NPT 根页表虚拟地址 */
    uint64_t   guest_phys_base;    /**< @brief Guest 物理地址基 */
    uint64_t   guest_phys_size;    /**< @brief Guest 物理地址空间大小 */
    uint32_t   ref_count;          /**< @brief 引用计数 */
} nested_page_table_t;

/* ========================================================================
 * 虚拟中断控制器（VGIC）
 * ======================================================================== */

/**
 * @brief 虚拟 GIC 中断状态
 */
typedef enum
{
    VGIC_IRQ_INACTIVE = 0U,   /**< @brief 未激活 */
    VGIC_IRQ_PENDING,         /**< @brief 待处理 */
    VGIC_IRQ_ACTIVE,          /**< @brief 活跃 */
    VGIC_IRQ_ACTIVE_PENDING   /**< @brief 活跃且待处理 */
} vgic_irq_state_t;

/**
 * @brief 虚拟 GIC 描述符
 */
typedef struct
{
    vgic_irq_state_t irq_state[VMM_VGIC_MAX_INTERRUPTS]; /**< @brief 中断状态 */
    uint32_t         irq_priority[VMM_VGIC_MAX_INTERRUPTS]; /**< @brief 中断优先级 */
    uint32_t         irq_enabled[VMM_VGIC_MAX_INTERRUPTS / 32U + 1U]; /**< @brief 使能位图 */
} vgic_desc_t;

/* ========================================================================
 * 虚拟机描述符
 * ======================================================================== */

/**
 * @brief 虚拟机描述符
 */
typedef struct vm_desc
{
    uint32_t              vm_id;              /**< @brief VM ID */
    vm_state_t            state;              /**< @brief VM 状态 */
    char                  name[32];           /**< @brief VM 名称 */
    vcpu_desc_t           vcpus[VMM_MAX_VCPUS_PER_VM]; /**< @brief vCPU 数组 */
    uint32_t              vcpu_count;         /**< @brief vCPU 数量 */
    nested_page_table_t   npt;                /**< @brief 嵌套页表 */
    vgic_desc_t           vgic;               /**< @brief 虚拟 GIC */
    paddr_t               mem_base;           /**< @brief Guest 内存基地址 */
    uint64_t              mem_size;           /**< @brief Guest 内存大小 */
} vm_desc_t;

/* ========================================================================
 * VMM API
 * ======================================================================== */

/**
 * @brief 初始化 VMM 子系统
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_init(void);

/**
 * @brief 创建虚拟机
 *
 * @param name     VM 名称
 * @param mem_size Guest 内存大小
 *
 * @return 成功返回 VM ID，失败返回负错误码
 */
int32_t vmm_create_vm(const char *name, uint64_t mem_size);

/**
 * @brief 销毁虚拟机
 *
 * @param vm_id VM ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_destroy_vm(uint32_t vm_id);

/**
 * @brief 创建 vCPU
 *
 * @param vm_id       VM ID
 * @param entry_point 入口点物理地址
 *
 * @return 成功返回 vCPU ID，失败返回负错误码
 */
int32_t vmm_create_vcpu(uint32_t vm_id, paddr_t entry_point);

/**
 * @brief 暂停 vCPU
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_vcpu_pause(uint32_t vm_id, uint32_t vcpu_id);

/**
 * @brief 运行 vCPU
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_vcpu_run(uint32_t vm_id, uint32_t vcpu_id);

/**
 * @brief 注入虚拟中断
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 * @param irq     中断号
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_inject_irq(uint32_t vm_id, uint32_t vcpu_id, uint32_t irq);

/**
 * @brief 处理 VM 退出
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 *
 * @return KERNEL_OK 成功处理，负数需要进一步处理
 */
kernel_status_t vmm_handle_exit(uint32_t vm_id, uint32_t vcpu_id);

/**
 * @brief 获取 VM 描述符
 *
 * @param vm_id VM ID
 *
 * @return VM 描述符指针，不存在返回 NULL
 */
vm_desc_t *vmm_get_vm(uint32_t vm_id);

/**
 * @brief 映射 Guest 物理页到嵌套页表
 *
 * @param vm_id      VM ID
 * @param guest_paddr Guest 物理地址
 * @param host_paddr  Host 物理地址
 * @param flags       页表属性标志
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_map_guest_page(uint32_t vm_id, paddr_t guest_paddr,
                                    paddr_t host_paddr, uint64_t flags);

/**
 * @brief 获取 VMM 统计信息
 *
 * @param vm_count   活跃 VM 数输出
 * @param vcpu_count 活跃 vCPU 数输出
 * @param vdev_count 活跃虚拟设备数输出
 */
void vmm_get_stats(uint32_t *vm_count, uint32_t *vcpu_count,
                     uint32_t *vdev_count);

/**
 * @brief 注册虚拟设备
 *
 * @param vm_id    VM ID
 * @param type     设备类型
 * @param name     设备名称
 * @param mmio_base MMIO 基址
 * @param mmio_size MMIO 大小
 *
 * @return 成功返回设备 ID，负数表示错误
 */
int32_t vmm_register_vdevice(uint32_t vm_id, uint32_t type,
                               const char *name,
                               uint64_t mmio_base, uint64_t mmio_size);

/**
 * @brief 处理 MMIO 访问
 *
 * @param vm_id     VM ID
 * @param vcpu_id   vCPU ID
 * @param fault_addr 故障地址
 * @param is_write  是否为写操作
 * @param value     读/写值
 * @param size      访问宽度
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t vmm_handle_mmio(uint32_t vm_id, uint32_t vcpu_id,
                                  uint64_t fault_addr, bool is_write,
                                  uint64_t *value, uint32_t size);

#endif /* SERVICES_VMM_VMM_H */
