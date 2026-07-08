/**
 * @file    vcpu.c
 * @brief   vCPU（虚拟 CPU）管理实现
 * @author  AISafe64 Team
 * @date    2026-07-08
 * @version 1.0
 *
 * @details vCPU 管理实现：
 *          - 创建 vCPU（初始化 Guest 寄存器快照）
 *          - 运行 vCPU（HVC 进入 Guest，等 trap 返回）
 *          - 销毁 vCPU
 *
 *          HVC 调用约定：
 *          x0 = 参数（入口地址 / VTTBR）
 *          x1 = 操作码（HVC_RUN_GUEST=2）
 *
 * @note    QNX 方案：EL2 hypervisor + EL1 Host + EL1/EL0 Guest
 *
 * @revision history
 * v1.0 2026-07-08 初始版本
 */

#include <kernel/vcpu.h>
#include <kernel/stage2.h>
#include <kernel/config.h>
#include <kernel/errno.h>
#include <string.h>
#include <stdint.h>

#ifndef CONFIG_MAX_VCPUS
#define CONFIG_MAX_VCPUS 8U
#endif

/* ========================================================================
 * 全局状态
 * ======================================================================== */

/** @brief vCPU 描述符池 */
static vcpu_t s_vcpus[CONFIG_MAX_VCPUS];

/** @brief 初始化标志 */
static bool s_initialized = false;

/* ========================================================================
 * 内部辅助
 * ======================================================================== */

/**
 * @brief 分配空闲 vCPU 槽位
 *
 * @return vCPU 指针，无空闲返回 NULL
 */
static vcpu_t *vcpu_alloc(void)
{
    uint32_t i;
    for (i = 0U; i < CONFIG_MAX_VCPUS; i++)
    {
        if (!s_vcpus[i].in_use)
        {
            return &s_vcpus[i];
        }
    }
    return NULL;
}

/* ========================================================================
 * 公共 API
 * ======================================================================== */

void vcpu_subsys_init(void)
{
    uint32_t i;
    for (i = 0U; i < CONFIG_MAX_VCPUS; i++)
    {
        s_vcpus[i].in_use = false;
        s_vcpus[i].state = VCPU_STATE_OFF;
    }
    s_initialized = true;
}

int32_t vcpu_create(uint32_t vm_id, uint64_t entry_pc, uint64_t entry_sp,
                     uint32_t *out_vcpu_id)
{
    vcpu_t *vcpu;
    uint32_t i;

    if ((out_vcpu_id == NULL) || !s_initialized)
    {
        return -(int32_t)EINVAL;
    }

    vcpu = vcpu_alloc();
    if (vcpu == NULL)
    {
        return -(int32_t)ENOMEM;
    }

    /* 分配 vCPU ID */
    vcpu->vcpu_id = 0U;
    for (i = 0U; i < CONFIG_MAX_VCPUS; i++)
    {
        if (&s_vcpus[i] == vcpu)
        {
            vcpu->vcpu_id = i;
            break;
        }
    }

    vcpu->vm_id = vm_id;
    vcpu->state = VCPU_STATE_READY;
    vcpu->in_use = true;

    /* 初始化 Guest 寄存器快照 */
    (void)memset(&vcpu->regs, 0, sizeof(vcpu_regs_t));
    vcpu->regs.pc = entry_pc;
    vcpu->regs.sp = entry_sp;
    vcpu->regs.pstate = 0x3C5ULL;  /* EL1h, DAIF masked */

    /* x0 = 0（Guest 启动参数） */
    vcpu->regs.x0_x30[0] = 0ULL;

    *out_vcpu_id = vcpu->vcpu_id;

    return 0;
}

int32_t vcpu_run(uint32_t vcpu_id)
{
    vcpu_t *vcpu;
    int32_t result;

    if ((vcpu_id >= CONFIG_MAX_VCPUS) || !s_initialized)
    {
        return -(int32_t)EINVAL;
    }

    vcpu = &s_vcpus[vcpu_id];
    if (!vcpu->in_use || (vcpu->state != VCPU_STATE_READY))
    {
        return -(int32_t)EINVAL;
    }

    /* 切换到 Guest 的 Stage-2 页表 */
    (void)s2_switch_vm(vcpu->vm_id);

    /* 设置状态为运行中 */
    vcpu->state = VCPU_STATE_RUNNING;

    /* 通过 HVC 进入 Guest
     * x0 = Guest 入口地址
     * x1 = HVC_RUN_GUEST (2)
     * EL2 handler 会设置 SPSR_EL2/ELR_EL2 并 eret 到 Guest
     */
    {
        uint64_t guest_pc = vcpu->regs.pc;
        int64_t ret;

        __asm__ volatile(
            "mov x0, %1\n"
            "mov x1, #2\n"
            "hvc #0\n"
            "mov %0, x0\n"
            : "=r"(ret)
            : "r"(guest_pc)
            : "x0", "x1", "memory"
        );

        result = (int32_t)ret;
    }

    /* Guest 被 trap 回 Host
     * result:
     *   0 = 正常返回
     *   1 = IRQ trap（Guest 被中断打断）
     *   2 = Guest 同步异常
     */
    vcpu->state = VCPU_STATE_READY;

    return result;
}

int32_t vcpu_destroy(uint32_t vcpu_id)
{
    if ((vcpu_id >= CONFIG_MAX_VCPUS) || !s_initialized)
    {
        return -(int32_t)EINVAL;
    }

    s_vcpus[vcpu_id].in_use = false;
    s_vcpus[vcpu_id].state = VCPU_STATE_STOPPED;

    return 0;
}
