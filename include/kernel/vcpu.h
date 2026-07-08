/**
 * @file    vcpu.h
 * @brief   vCPU（虚拟 CPU）管理接口
 * @author  AISafe64 Team
 * @date    2026-07-08
 * @version 1.0
 *
 * @details vCPU 管理：创建、运行、销毁虚拟 CPU。
 *          通过 HVC 系统调用从 EL1 进入 EL2 管理 Guest。
 *
 * @revision history
 * v1.0 2026-07-08 初始版本
 */

#ifndef KERNEL_VCPU_H
#define KERNEL_VCPU_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief vCPU 状态
 */
typedef enum
{
    VCPU_STATE_OFF = 0,       /**< @brief 未初始化 */
    VCPU_STATE_READY,         /**< @brief 就绪（可运行） */
    VCPU_STATE_RUNNING,       /**< @brief 正在 Guest 中运行 */
    VCPU_STATE_BLOCKED,       /**< @brief 阻塞（等待 I/O/中断） */
    VCPU_STATE_STOPPED        /**< @brief 已停止 */
} vcpu_state_t;

/**
 * @brief vCPU 上下文（Guest 寄存器快照）
 */
typedef struct
{
    uint64_t x0_x30[31];      /**< @brief 通用寄存器 x0-x30 */
    uint64_t sp;              /**< @brief 栈指针 */
    uint64_t pc;              /**< @brief 程序计数器 */
    uint64_t pstate;          /**< @brief 处理器状态 */
    uint64_t vmpidr;          /**< @brief 虚拟 MPIDR */
} vcpu_regs_t;

/**
 * @brief vCPU 描述符
 */
typedef struct
{
    uint32_t      vcpu_id;    /**< @brief vCPU ID */
    uint32_t      vm_id;      /**< @brief 所属 VM ID（Stage-2 页表） */
    vcpu_state_t  state;      /**< @brief 当前状态 */
    vcpu_regs_t   regs;       /**< @brief Guest 寄存器快照 */
    bool          in_use;     /**< @brief 是否被使用 */
} vcpu_t;

/**
 * @brief 初始化 vCPU 子系统
 */
void vcpu_subsys_init(void);

/**
 * @brief 创建 vCPU
 *
 * @param vm_id 所属 VM ID（Stage-2 地址空间）
 * @param entry_pc Guest 入口地址
 * @param entry_sp Guest 栈指针
 * @param out_vcpu_id 输出 vCPU ID
 * @return 0 成功，< 0 失败
 */
int32_t vcpu_create(uint32_t vm_id, uint64_t entry_pc, uint64_t entry_sp,
                     uint32_t *out_vcpu_id);

/**
 * @brief 运行 vCPU（进入 Guest）
 *
 * @details 通过 HVC 调用 EL2 执行 Guest eret。
 *          返回时表示 Guest 被 trap 回 Host。
 *
 * @param vcpu_id vCPU ID
 * @return 0=正常 trap（如 IRQ），1=IRQ trap，2=Guest 异常，<0=错误
 */
int32_t vcpu_run(uint32_t vcpu_id);

/**
 * @brief 销毁 vCPU
 *
 * @param vcpu_id vCPU ID
 * @return 0 成功
 */
int32_t vcpu_destroy(uint32_t vcpu_id);

#endif /* KERNEL_VCPU_H */
