/**
 * @file    vmm.h
 * @brief   虚拟机管理器（VMM）公共 API
 * @author  AISafe64 Team
 * @date    2026-05-03
 * @version 4.0
 *
 * @details 本文件定义了 ARMv8-A 虚拟机管理器的公共接口：
 *          - 包含所有子模块头文件（vCPU, VM, NPT, VGIC, VirtIO, Stats）
 *          - 重新导出公共常量
 *          - 定义 VMM 顶层 API
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef SERVICES_VMM_VMM_H
#define SERVICES_VMM_VMM_H

/* ========================================================================
 * 子模块头文件
 * ======================================================================== */

#include "core/vcpu.h"
#include "core/vm.h"
#include "npt/npt.h"
#include "vgic/vgic.h"
#include "device/virtio.h"
#include "stats/vmm_stats.h"

/* ========================================================================
 * 重新导出公共常量
 *
 * 以下常量在各子模块头文件中定义，此处列出供参考：
 *
 * - VMM_MAX_VMS              (4U)       最大虚拟机数量          [vm.h]
 * - VMM_MAX_VCPUS_PER_VM     (4U)       每 VM 最大 vCPU 数量    [vcpu.h]
 * - VMM_MAX_VDEVICES         (8U)       最大虚拟设备数          [vm.h]
 * - VMM_NPT_LEVELS           (4U)       嵌套页表级别数          [npt.h]
 * - VMM_GUEST_PHYS_SIZE      (0x40000000ULL) Guest 物理地址空间 [npt.h]
 * - VMM_VGIC_MAX_INTERRUPTS  (256U)     最大虚拟中断数量        [vgic.h]
 * - VIRTIO_MAX_QUEUES        (32U)      VirtIO 最大队列数       [virtio.h]
 * ======================================================================== */

/* ========================================================================
 * VMM 顶层 API
 * ======================================================================== */

/**
 * @brief 初始化 VMM 子系统
 *
 * @details 初始化 VMM 全局状态，包括 VM 描述符池和虚拟设备表
 *
 * @return KERNEL_OK 成功
 *
 * @note 必须在使用其他 VMM API 之前调用
 */
kernel_status_t vmm_init(void);

/**
 * @brief 创建虚拟机
 *
 * @details 分配 VM 描述符，初始化 NPT、VGIC 等子模块
 *
 * @param name     VM 名称（不能为 NULL，最长 31 字符）
 * @param mem_size Guest 物理内存大小（不能为 0，不超过 VMM_GUEST_PHYS_SIZE）
 *
 * @return 成功返回 VM ID（>=0），失败返回负错误码
 * @retval -EPERM  VMM 未初始化
 * @retval -EINVAL 参数无效
 * @retval -ENOMEM 无可用 VM 描述符
 *
 * @note 创建后 VM 处于 VM_STATE_CREATED 状态
 */
int32_t vmm_create_vm(const char *name, uint64_t mem_size);

/**
 * @brief 销毁虚拟机
 *
 * @details 释放 VM 关联的所有资源（vCPU、NPT、VGIC、虚拟设备）
 *
 * @param vm_id VM ID
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL VM ID 无效
 * @retval -ENOENT VM 不存在
 * @retval -EBUSY  VM 正在运行，必须先停止
 *
 * @warning 正在运行的 VM 不能直接销毁
 */
kernel_status_t vmm_destroy_vm(uint32_t vm_id);

/**
 * @brief 创建 vCPU
 *
 * @details 在指定 VM 中创建新的 vCPU，初始化寄存器上下文
 *
 * @param vm_id       VM ID
 * @param entry_point 入口点物理地址（必须在 Guest 内存范围内）
 *
 * @return 成功返回 vCPU ID（>=0），失败返回负错误码
 * @retval -EINVAL VM ID 无效或入口点地址越界
 * @retval -ENOENT VM 不存在
 * @retval -ENOMEM 已达到最大 vCPU 数量
 *
 * @note 创建后 vCPU 处于 VCPU_STATE_STOPPED 状态
 * @note 初始 PSTATE 设置为 EL1h 模式（0x3C5）
 */
int32_t vmm_create_vcpu(uint32_t vm_id, paddr_t entry_point);

/**
 * @brief 暂停 vCPU
 *
 * @details 将运行中的 vCPU 切换到停止状态
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL 参数无效或 vCPU 不在运行状态
 */
kernel_status_t vmm_vcpu_pause(uint32_t vm_id, uint32_t vcpu_id);

/**
 * @brief 运行 vCPU
 *
 * @details 启动或恢复 vCPU 执行，VM 状态同步设为 RUNNING
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL 参数无效
 * @retval -EBUSY  vCPU 已在运行
 */
kernel_status_t vmm_vcpu_run(uint32_t vm_id, uint32_t vcpu_id);

/**
 * @brief 注入虚拟中断到 vCPU
 *
 * @details 通过 VGIC 注入中断到指定 vCPU 的中断位图
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 * @param irq     中断号（0 ~ VMM_VGIC_MAX_INTERRUPTS-1）
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL 参数无效（中断号越界）
 */
kernel_status_t vmm_inject_irq(uint32_t vm_id, uint32_t vcpu_id, uint32_t irq);

/**
 * @brief 处理 VM 退出
 *
 * @details 根据 ESR_EL1 的 EC 字段分发到对应的退出处理函数
 *          支持的退出类型：
 *          - WFI/WFE (0x01): 低功耗等待
 *          - HVC (0x08): Hypercall
 *          - 数据中止 (0x0A/0x24): MMIO 访问
 *          - 指令中止 (0x0E): 指令异常
 *          - 系统寄存器陷阱 (0x06): MRS/MSR
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 *
 * @return KERNEL_OK 成功处理
 */
kernel_status_t vmm_handle_exit(uint32_t vm_id, uint32_t vcpu_id);

/**
 * @brief 获取 VM 描述符
 *
 * @param vm_id VM ID
 *
 * @return VM 描述符指针，VM 不存在返回 NULL
 */
vm_desc_t *vmm_get_vm(uint32_t vm_id);

/**
 * @brief 映射 Guest 物理页到嵌套页表
 *
 * @details 在 NPT 中建立 Guest PA → Host PA 映射
 *
 * @param vm_id       VM ID
 * @param guest_paddr Guest 物理地址
 * @param host_paddr  Host 物理地址
 * @param flags       页表属性标志
 *
 * @return KERNEL_OK 成功
 * @retval -EINVAL 参数无效
 * @retval -ENOENT VM 不存在
 */
kernel_status_t vmm_map_guest_page(uint32_t vm_id, paddr_t guest_paddr,
                                    paddr_t host_paddr, uint64_t flags);

/**
 * @brief 注册虚拟设备
 *
 * @details 注册 VirtIO 设备到 VM 的 MMIO 地址空间
 *
 * @param vm_id     VM ID
 * @param type      设备类型（virtio_device_type_t 枚举值）
 * @param name      设备名称
 * @param mmio_base MMIO 基址（Guest 物理地址）
 * @param mmio_size MMIO 大小（字节）
 *
 * @return 成功返回设备 ID（>=0），失败返回负错误码
 */
int32_t vmm_register_vdevice(uint32_t vm_id, uint32_t type,
                               const char *name,
                               uint64_t mmio_base, uint64_t mmio_size);

/**
 * @brief 处理 MMIO 访问
 *
 * @details 当 Guest 访问 MMIO 地址空间时，查找对应的虚拟设备并调用回调
 *
 * @param vm_id      VM ID
 * @param vcpu_id    vCPU ID
 * @param fault_addr 故障地址（Guest 物理地址）
 * @param is_write   是否为写操作
 * @param value      读/写值指针
 * @param size       访问宽度（字节）
 *
 * @return KERNEL_OK 成功
 * @retval -EFAULT 无匹配设备
 */
kernel_status_t vmm_handle_mmio(uint32_t vm_id, uint32_t vcpu_id,
                                  uint64_t fault_addr, bool is_write,
                                  uint64_t *value, uint32_t size);

/**
 * @brief 获取 VMM 统计信息
 *
 * @param vm_count   活跃 VM 数输出（可为 NULL）
 * @param vcpu_count 活跃 vCPU 数输出（可为 NULL）
 * @param vdev_count 活跃虚拟设备数输出（可为 NULL）
 */
void vmm_get_stats(uint32_t *vm_count, uint32_t *vcpu_count,
                     uint32_t *vdev_count);

#endif /* SERVICES_VMM_VMM_H */
