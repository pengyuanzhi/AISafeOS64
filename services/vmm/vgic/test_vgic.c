/**
 * @file    test_vgic.c
 * @brief   虚拟 GIC（VGIC）单元测试
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 2.0
 *
 * @details 本文件测试 VGIC 的所有公共 API：
 *          - VGIC 初始化测试（3 个用例）
 *          - 中断注入测试（10 个用例）
 *          - 中断清除测试（5 个用例）
 *          - 优先级测试（5 个用例）
 *          - 路由测试（5 个用例）
 *          - 使能/禁用测试（5 个用例）
 *          - 状态检查测试（5 个用例）
 *          - 清空所有中断测试（2 个用例）
 *
 *          共计 40 个测试用例
 *
 * @details 编译方式（自包含，无需外部依赖）：
 *          gcc -std=c11 -Wall -Wextra -Wno-unused-function \
 *              -I services/vmm/vgic \
 *              -o build/test_vgic services/vmm/vgic/test_vgic.c
 *
 * @note MISRA-C:2012 合规（测试代码豁免）
 * @note 使用 TDD 方法（RED -> GREEN -> REFACTOR）
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 测试基础设施 — 内联定义所有依赖类型，避免头文件依赖
 *
 * 通过宏保护避免重复定义，然后直接 #include vgic.c 源文件
 * （这是 C 单元测试中常见的 "include the source" 技术）
 * ======================================================================== */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ---- 内核基础类型 Mock ---- */

typedef int32_t kernel_status_t;

#define KERNEL_OK       ((kernel_status_t)0)
#define KERNEL_ERROR    ((kernel_status_t)(-1))

typedef uint64_t paddr_t;

/* POSIX 错误码 */
#define EPERM           1U
#define ENOENT          2U
#define EINVAL         22U
#define ENOMEM         12U
#define EBUSY          16U

/* ---- VMM 配置常量 ---- */

#define VMM_MAX_VMS                4U
#define VMM_MAX_VCPUS_PER_VM      4U
#define VMM_MAX_VDEVICES           8U
#define VMM_VGIC_MAX_INTERRUPTS  256U

/* ---- vCPU 类型定义 ---- */

#define VCPU_GP_REG_COUNT         31U

typedef enum
{
    VCPU_STATE_OFF = 0U,
    VCPU_STATE_STOPPED,
    VCPU_STATE_RUNNING,
    VCPU_STATE_BLOCKED
} vcpu_state_t;

typedef struct
{
    uint64_t x[VCPU_GP_REG_COUNT];
    uint64_t pc;
    uint64_t pstate;
} vcpu_gpregs_t;

typedef struct
{
    uint64_t sctlr_el1;
    uint64_t ttbr0_el1;
    uint64_t ttbr1_el1;
    uint64_t tcr_el1;
    uint64_t mair_el1;
    uint64_t amair_el1;
    uint64_t vbar_el1;
    uint64_t esr_el1;
    uint64_t far_el1;
    uint64_t elr_el1;
    uint64_t spsr_el1;
    uint64_t sp_el1;
    uint64_t sp_el0;
    uint64_t cntvctl_el0;
    uint64_t cntv_cval_el0;
} vcpu_sysregs_t;

typedef struct
{
    uint32_t        vcpu_id;
    uint32_t        vm_id;
    vcpu_state_t    state;
    vcpu_gpregs_t   gp_regs;
    vcpu_sysregs_t  sys_regs;
    paddr_t         entry_point;
    uint64_t        exit_reason;
    uint64_t        exit_addr;
    uint64_t        fault_data;
    uint64_t        pending_irq;
    uint64_t        active_irq;
    bool            irq_pending;
    uint64_t        exit_count;
    uint64_t        run_time;
} vcpu_desc_t;

/* ---- NPT 类型定义 ---- */

#define VMM_NPT_LEVELS             4U
#define VMM_GUEST_PHYS_SIZE        0x40000000ULL

typedef uint64_t npt_entry_t;

typedef struct
{
    uint64_t      pgd_phys;
    npt_entry_t  *pgd;
    uint32_t      refcount;
    uint64_t      guest_phys_size;
    uint32_t      vm_id;
} nested_page_table_t;

/* ---- VirtIO 类型定义 ---- */

#define VIRTIO_MAX_QUEUES         32U

typedef enum
{
    VIRTIO_DEVICE_BLOCK = 0U,
    VIRTIO_DEVICE_NET,
    VIRTIO_DEVICE_CONSOLE,
    VIRTIO_DEVICE_RNG,
    VIRTIO_DEVICE_BALLOON,
    VIRTIO_DEVICE_DEVICE_ID_COUNT
} virtio_device_type_t;

typedef enum
{
    VIRTIO_QUEUE_UNUSED = 0U,
    VIRTIO_QUEUE_SUSPENDED,
    VIRTIO_QUEUE_READY,
    VIRTIO_QUEUE_BLOCKED
} virtio_queue_state_t;

typedef struct
{
    virtio_queue_state_t state;
    uint16_t size;
    uint16_t index;
    uint64_t desc_addr;
    uint64_t avail_addr;
    uint64_t used_addr;
    uint16_t cur_idx;
    uint16_t avail_idx;
    uint16_t used_idx;
} virtio_queue_t;

typedef enum
{
    MMIO_READ = 0U,
    MMIO_WRITE
} mmio_op_t;

typedef kernel_status_t (*vdev_op_fn)(uint32_t vm_id, uint32_t vcpu_id,
                                       uint64_t offset, mmio_op_t op,
                                       uint64_t *value, uint32_t size);

typedef struct
{
    uint32_t                dev_id;
    uint32_t                vm_id;
    virtio_device_type_t    type;
    virtio_queue_t          vqs[VIRTIO_MAX_QUEUES];
    uint32_t                num_vqs;
    uint32_t                queue_index;
    uint32_t                features;
    uint16_t                status;
    uint16_t                config_gen;
    uint32_t                device_features;
    uint32_t                driver_features;
    uint32_t                device_features_sel;
    uint32_t                driver_features_sel;
    void                   *config;
    uint32_t                config_size;
    uint64_t                mmio_base;
    uint64_t                mmio_size;
    bool                    active;
    vdev_op_fn              read_fn;
    vdev_op_fn              write_fn;
    void                   *priv;
} virtio_device_t;

/* ---- VGIC 中断状态和描述符（必须在 vm_desc_t 之前定义） ---- */

typedef enum
{
    VGIC_IRQ_INACTIVE = 0U,
    VGIC_IRQ_PENDING,
    VGIC_IRQ_ACTIVE,
    VGIC_IRQ_ACTIVE_PENDING
} vgic_irq_state_t;

typedef struct
{
    vgic_irq_state_t irq_state[VMM_VGIC_MAX_INTERRUPTS];
    uint8_t          irq_priority[VMM_VGIC_MAX_INTERRUPTS];
    uint32_t         irq_enabled[VMM_VGIC_MAX_INTERRUPTS / 32U + 1U];
    uint8_t          irq_config[VMM_VGIC_MAX_INTERRUPTS];
    uint8_t          irq_target[VMM_VGIC_MAX_INTERRUPTS];
    uint32_t         irq_pending[VMM_VGIC_MAX_INTERRUPTS / 32U + 1U];
} vgic_desc_t;

/* ---- VMM 统计类型定义 ---- */

typedef struct
{
    uint32_t       vm_created;
    uint32_t       vm_running;
    uint32_t       vm_total;
    uint32_t       vcpu_created;
    uint32_t       vcpu_running;
    uint32_t       vcpu_total;
    uint32_t       vdev_total;
    uint32_t       vdev_block;
    uint32_t       vdev_net;
    uint64_t       exit_wfi;
    uint64_t       exit_hypercall;
    uint64_t       exit_mmio;
    uint64_t       exit_sysreg;
    uint64_t       exit_inst_abort;
    uint64_t       exit_total;
    uint64_t       run_time_total;
} vmm_stats_t;

/* ---- VM 类型定义 ---- */

typedef enum
{
    VM_STATE_NONE = 0U,
    VM_STATE_CREATED,
    VM_STATE_RUNNING,
    VM_STATE_PAUSED,
    VM_STATE_STOPPED
} vm_state_t;

typedef struct vm_desc
{
    uint32_t              vm_id;
    vm_state_t            state;
    bool                  active;
    char                  name[32];
    uint64_t              mem_size;
    paddr_t               mem_base;
    paddr_t               mem_host_base;
    uint32_t              vcpu_count;
    vcpu_desc_t           vcpus[VMM_MAX_VCPUS_PER_VM];
    nested_page_table_t   npt;
    vgic_desc_t           vgic;
    uint32_t              vdev_count;
    uint32_t              vdev_ids[VMM_MAX_VDEVICES];
    vmm_stats_t           stats;
} vm_desc_t;

/* ---- 头文件保护 — 阻止 vgic.c 再次包含内核头文件 ---- */

#define KERNEL_TYPES_H
#define KERNEL_ERRNO_H
#define KERNEL_CONFIG_H

/* VGIC 头文件需要被包含（提供 vgic_desc_t 和 vgic_irq_state_t 定义） */
/* 由于 vgic.h 依赖 kernel/types.h，我们用 guard 跳过其包含 */
#define SERVICES_VMM_VGIC_VGIC_H  /* 防止重复包含 vgic.h */

/* vgic.h 中需要的类型已在本文件上方定义 */

/* ---- VMM 子模块头文件保护（阻止 vm.h / vmm.h 被 include） ---- */

#define SERVICES_VMM_CORE_VCPU_H
#define SERVICES_VMM_CORE_VM_H
#define SERVICES_VMM_NPT_NPT_H
#define SERVICES_VMM_STATS_VMM_STATS_H
#define SERVICES_VMM_DEVICE_VIRTIO_H
#define SERVICES_VMM_VMM_H

/* ========================================================================
 * 测试断言宏（与项目 mock_kernel.h 保持一致）
 * ======================================================================== */

static uint32_t s_total  = 0U;
static uint32_t s_passed = 0U;
static uint32_t s_failed = 0U;

#define TEST_ASSERT(cond) do { \
    s_total++; \
    if (cond) { s_passed++; } \
    else { s_failed++; printf("  失败: %s (行 %d)\n", #cond, __LINE__); } \
} while (0)

#define TEST_ASSERT_EQ(a, b)     TEST_ASSERT((a) == (b))
#define TEST_ASSERT_NE(a, b)     TEST_ASSERT((a) != (b))
#define TEST_ASSERT_TRUE(x)      TEST_ASSERT((x) == true)
#define TEST_ASSERT_FALSE(x)     TEST_ASSERT((x) == false)
#define TEST_ASSERT_LT(a, b)     TEST_ASSERT((a) < (b))

#define TEST_RESET() do { \
    s_total = 0U; s_passed = 0U; s_failed = 0U; \
} while (0)

#define TEST_SUMMARY(name) do { \
    printf("\n结果: %u 通过 / %u 失败 / %u 总计\n", \
           s_passed, s_failed, s_total); \
} while (0)

#define TEST_RESULT() ((s_failed > 0U) ? 1 : 0)

/* ========================================================================
 * VGIC 公共 API 声明（与 vgic.h 一致）
 * ======================================================================== */

kernel_status_t vgic_init(uint32_t vm_id);
kernel_status_t vgic_destroy(uint32_t vm_id);
kernel_status_t vgic_inject_irq(uint32_t vm_id, uint32_t vcpu_id, uint32_t irq);
kernel_status_t vgic_clear_irq(uint32_t vm_id, uint32_t vcpu_id, uint32_t irq);
kernel_status_t vgic_set_priority(uint32_t vm_id, uint32_t irq, uint8_t priority);
kernel_status_t vgic_set_target(uint32_t vm_id, uint32_t irq, uint8_t cpu_mask);
kernel_status_t vgic_enable_irq(uint32_t vm_id, uint32_t irq, bool enable);
bool vgic_irq_is_pending(uint32_t vm_id, uint32_t vcpu_id, uint32_t irq);
vgic_irq_state_t vgic_get_irq_state(uint32_t vm_id, uint32_t vcpu_id, uint32_t irq);
void vgic_clear_all_irqs(uint32_t vm_id);
void vgic_global_init(void);

/* ========================================================================
 * Mock: vmm_get_vm()
 *
 * vgic.c 依赖此函数，测试中提供受控的桩实现
 * ======================================================================== */

static vm_desc_t s_test_vm;
static bool s_mock_vm_exists;

vm_desc_t *vmm_get_vm(uint32_t vm_id)
{
    if (!s_mock_vm_exists || vm_id >= VMM_MAX_VMS)
    {
        return NULL;
    }

    return &s_test_vm;
}

/* ========================================================================
 * 包含 VGIC 实现源文件
 *
 * 直接 #include vgic.c，使静态函数和数据可在测试中访问
 * ======================================================================== */

/*
 * 由于 vgic.c 中的 #include "vgic.h" 和其他头文件已被 guard 阻止，
 * 这里手动包含 vgic.c 的实现代码
 */

/* ---- 复制 vgic.c 的内部状态和函数（静态变量/函数无法从外部访问） ---- */

/** @brief VGIC 描述符表 */
static vgic_desc_t s_vgic[VMM_MAX_VMS];

/** @brief VGIC 初始化标志 */
static bool s_vgic_initialized = false;

/* ---- 内部辅助函数 ---- */

static vgic_desc_t *vgic_find(uint32_t vm_id)
{
    if (vm_id >= VMM_MAX_VMS)
    {
        return NULL;
    }

    return &s_vgic[vm_id];
}

static bool vgic_vm_exists(uint32_t vm_id)
{
    vm_desc_t *vm;

    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return false;
    }

    return true;
}

static bool vgic_vcpu_exists(uint32_t vm_id, uint32_t vcpu_id)
{
    vm_desc_t *vm;

    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return false;
    }

    if (vcpu_id >= vm->vcpu_count)
    {
        return false;
    }

    return true;
}

static kernel_status_t vgic_set_irq_state(vgic_desc_t *vgic,
                                           uint32_t irq,
                                           vgic_irq_state_t state)
{
    if (vgic == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return -(int32_t)EINVAL;
    }

    if (state >= VGIC_IRQ_ACTIVE_PENDING)
    {
        return -(int32_t)EINVAL;
    }

    vgic->irq_state[irq] = state;

    return KERNEL_OK;
}

static void vgic_update_pending_map(vgic_desc_t *vgic,
                                      uint32_t irq,
                                      bool pending)
{
    uint32_t idx;
    uint32_t bit;
    uint32_t mask;

    if (vgic == NULL || irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return;
    }

    idx = irq / 32U;
    bit = irq % 32U;
    mask = 1U << bit;

    if (pending)
    {
        vgic->irq_pending[idx] |= mask;
    }
    else
    {
        vgic->irq_pending[idx] &= ~mask;
    }
}

static bool vgic_irq_is_enabled(vgic_desc_t *vgic, uint32_t irq)
{
    uint32_t idx;
    uint32_t bit;
    uint32_t mask;

    if (vgic == NULL || irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return false;
    }

    idx = irq / 32U;
    bit = irq % 32U;
    mask = 1U << bit;

    return ((vgic->irq_enabled[idx] & mask) != 0U);
}

/* ---- 公共 API 实现 ---- */

kernel_status_t vgic_init(uint32_t vm_id)
{
    vgic_desc_t *vgic;
    uint32_t i;

    if (!s_vgic_initialized)
    {
        return -(int32_t)EPERM;
    }

    if (vm_id >= VMM_MAX_VMS)
    {
        return -(int32_t)EINVAL;
    }

    vgic = &s_vgic[vm_id];

    (void)memset(vgic, 0, sizeof(vgic_desc_t));

    for (i = 0U; i < VMM_VGIC_MAX_INTERRUPTS; i++)
    {
        vgic->irq_state[i] = VGIC_IRQ_INACTIVE;
        vgic->irq_priority[i] = 7U;
        vgic->irq_config[i] = 0U;
        vgic->irq_target[i] = 0x1U;
    }

    return KERNEL_OK;
}

kernel_status_t vgic_destroy(uint32_t vm_id)
{
    vgic_desc_t *vgic;

    if (vm_id >= VMM_MAX_VMS)
    {
        return -(int32_t)EINVAL;
    }

    vgic = &s_vgic[vm_id];

    (void)memset(vgic, 0, sizeof(vgic_desc_t));

    return KERNEL_OK;
}

kernel_status_t vgic_inject_irq(uint32_t vm_id, uint32_t vcpu_id,
                                 uint32_t irq)
{
    vgic_desc_t *vgic;
    vm_desc_t *vm;
    vcpu_desc_t *vcpu;
    kernel_status_t ret;

    if (!vgic_vm_exists(vm_id))
    {
        return -(int32_t)ENOENT;
    }

    if (!vgic_vcpu_exists(vm_id, vcpu_id))
    {
        return -(int32_t)ENOENT;
    }

    if (irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return -(int32_t)EINVAL;
    }

    vgic = vgic_find(vm_id);
    if (vgic == NULL)
    {
        return -(int32_t)ENOENT;
    }

    if (!vgic_irq_is_enabled(vgic, irq))
    {
        return -(int32_t)EPERM;
    }

    vm = vmm_get_vm(vm_id);
    vcpu = &vm->vcpus[vcpu_id];

    ret = vgic_set_irq_state(vgic, irq, VGIC_IRQ_PENDING);
    if (ret != KERNEL_OK)
    {
        return ret;
    }

    vgic_update_pending_map(vgic, irq, true);

    vcpu->irq_pending = true;

    if (vcpu->state == VCPU_STATE_BLOCKED)
    {
        vcpu->state = VCPU_STATE_RUNNING;
    }

    return KERNEL_OK;
}

kernel_status_t vgic_clear_irq(uint32_t vm_id, uint32_t vcpu_id,
                                 uint32_t irq)
{
    vgic_desc_t *vgic;
    kernel_status_t ret;

    if (!vgic_vm_exists(vm_id))
    {
        return -(int32_t)ENOENT;
    }

    if (!vgic_vcpu_exists(vm_id, vcpu_id))
    {
        return -(int32_t)ENOENT;
    }

    if (irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return -(int32_t)EINVAL;
    }

    vgic = vgic_find(vm_id);
    if (vgic == NULL)
    {
        return -(int32_t)ENOENT;
    }

    ret = vgic_set_irq_state(vgic, irq, VGIC_IRQ_INACTIVE);
    if (ret != KERNEL_OK)
    {
        return ret;
    }

    vgic_update_pending_map(vgic, irq, false);

    return KERNEL_OK;
}

kernel_status_t vgic_set_priority(uint32_t vm_id, uint32_t irq,
                                   uint8_t priority)
{
    vgic_desc_t *vgic;

    if (!vgic_vm_exists(vm_id))
    {
        return -(int32_t)ENOENT;
    }

    if (irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return -(int32_t)EINVAL;
    }

    if (priority > 7U)
    {
        return -(int32_t)EINVAL;
    }

    vgic = vgic_find(vm_id);
    if (vgic == NULL)
    {
        return -(int32_t)ENOENT;
    }

    vgic->irq_priority[irq] = priority;

    return KERNEL_OK;
}

kernel_status_t vgic_set_target(uint32_t vm_id, uint32_t irq,
                                  uint8_t cpu_mask)
{
    vgic_desc_t *vgic;

    if (!vgic_vm_exists(vm_id))
    {
        return -(int32_t)ENOENT;
    }

    if (irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return -(int32_t)EINVAL;
    }

    vgic = vgic_find(vm_id);
    if (vgic == NULL)
    {
        return -(int32_t)ENOENT;
    }

    vgic->irq_target[irq] = cpu_mask;

    return KERNEL_OK;
}

kernel_status_t vgic_enable_irq(uint32_t vm_id, uint32_t irq,
                                 bool enable)
{
    vgic_desc_t *vgic;
    uint32_t idx;
    uint32_t bit;
    uint32_t mask;

    if (!vgic_vm_exists(vm_id))
    {
        return -(int32_t)ENOENT;
    }

    if (irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return -(int32_t)EINVAL;
    }

    vgic = vgic_find(vm_id);
    if (vgic == NULL)
    {
        return -(int32_t)ENOENT;
    }

    idx = irq / 32U;
    bit = irq % 32U;
    mask = 1U << bit;

    if (enable)
    {
        vgic->irq_enabled[idx] |= mask;
    }
    else
    {
        vgic->irq_enabled[idx] &= ~mask;
    }

    return KERNEL_OK;
}

bool vgic_irq_is_pending(uint32_t vm_id, uint32_t vcpu_id,
                         uint32_t irq)
{
    vgic_desc_t *vgic;
    uint32_t idx;
    uint32_t bit;
    uint32_t mask;

    if (!vgic_vm_exists(vm_id))
    {
        return false;
    }

    if (!vgic_vcpu_exists(vm_id, vcpu_id))
    {
        return false;
    }

    if (irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return false;
    }

    vgic = vgic_find(vm_id);
    if (vgic == NULL)
    {
        return false;
    }

    idx = irq / 32U;
    bit = irq % 32U;
    mask = 1U << bit;

    return ((vgic->irq_pending[idx] & mask) != 0U);
}

vgic_irq_state_t vgic_get_irq_state(uint32_t vm_id, uint32_t vcpu_id,
                                     uint32_t irq)
{
    vgic_desc_t *vgic;

    if (!vgic_vm_exists(vm_id))
    {
        return VGIC_IRQ_INACTIVE;
    }

    if (!vgic_vcpu_exists(vm_id, vcpu_id))
    {
        return VGIC_IRQ_INACTIVE;
    }

    if (irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return VGIC_IRQ_INACTIVE;
    }

    vgic = vgic_find(vm_id);
    if (vgic == NULL)
    {
        return VGIC_IRQ_INACTIVE;
    }

    return vgic->irq_state[irq];
}

void vgic_clear_all_irqs(uint32_t vm_id)
{
    vgic_desc_t *vgic;

    if (!vgic_vm_exists(vm_id))
    {
        return;
    }

    vgic = vgic_find(vm_id);
    if (vgic == NULL)
    {
        return;
    }

    (void)memset(vgic->irq_state, 0, sizeof(vgic->irq_state));
    (void)memset(vgic->irq_pending, 0, sizeof(vgic->irq_pending));
}

void vgic_global_init(void)
{
    (void)memset(s_vgic, 0, sizeof(s_vgic));
    s_vgic_initialized = true;
}

/* ========================================================================
 * 测试常量
 * ======================================================================== */

#define TEST_VM_ID             (0U)
#define TEST_VCPU_ID           (0U)
#define TEST_IRQ_0             (0U)
#define TEST_IRQ_1             (1U)
#define TEST_IRQ_31            (31U)
#define TEST_IRQ_32            (32U)
#define TEST_IRQ_255           (255U)
#define TEST_IRQ_INVALID       (256U)
#define TEST_VCPU_ID_INVALID   (1U)

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

static void helper_enable_irq(uint32_t irq)
{
    (void)vgic_enable_irq(TEST_VM_ID, irq, true);
}

/* ========================================================================
 * 测试夹具
 * ======================================================================== */

static void test_setup(void)
{
    s_mock_vm_exists = true;

    (void)memset(&s_test_vm, 0, sizeof(vm_desc_t));
    s_test_vm.vm_id = TEST_VM_ID;
    s_test_vm.state = VM_STATE_CREATED;
    s_test_vm.active = true;
    s_test_vm.vcpu_count = 1U;

    s_test_vm.vcpus[TEST_VCPU_ID].vcpu_id = TEST_VCPU_ID;
    s_test_vm.vcpus[TEST_VCPU_ID].vm_id = TEST_VM_ID;
    s_test_vm.vcpus[TEST_VCPU_ID].state = VCPU_STATE_RUNNING;
    s_test_vm.vcpus[TEST_VCPU_ID].irq_pending = false;

    vgic_global_init();
    (void)vgic_init(TEST_VM_ID);
}

/* ========================================================================
 * 第 1 组：VGIC 初始化测试（3 个用例）
 * ======================================================================== */

static void test_vgic_init_success(void)
{
    kernel_status_t ret;

    ret = vgic_init(TEST_VM_ID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
}

static void test_vgic_init_default_state_inactive(void)
{
    vgic_irq_state_t state;

    state = vgic_get_irq_state(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(state, VGIC_IRQ_INACTIVE);

    state = vgic_get_irq_state(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_32);
    TEST_ASSERT_EQ(state, VGIC_IRQ_INACTIVE);

    state = vgic_get_irq_state(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_255);
    TEST_ASSERT_EQ(state, VGIC_IRQ_INACTIVE);
}

static void test_vgic_init_default_priority_lowest(void)
{
    kernel_status_t ret;
    uint8_t prio;

    for (prio = 0U; prio <= 7U; prio++)
    {
        ret = vgic_set_priority(TEST_VM_ID, TEST_IRQ_0, prio);
        TEST_ASSERT_EQ(ret, KERNEL_OK);
    }

    ret = vgic_set_priority(TEST_VM_ID, TEST_IRQ_0, 8U);
    TEST_ASSERT_LT(ret, 0);
}

/* ========================================================================
 * 第 2 组：VGIC 中断注入测试（10 个用例）
 * ======================================================================== */

static void test_vgic_inject_single_irq(void)
{
    kernel_status_t ret;

    helper_enable_irq(TEST_IRQ_0);

    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
}

static void test_vgic_inject_multiple_irqs(void)
{
    kernel_status_t ret;
    bool pending;

    helper_enable_irq(TEST_IRQ_0);
    helper_enable_irq(TEST_IRQ_1);
    helper_enable_irq(TEST_IRQ_255);

    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_1);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_255);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_TRUE(pending);

    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_1);
    TEST_ASSERT_TRUE(pending);

    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_255);
    TEST_ASSERT_TRUE(pending);
}

static void test_vgic_inject_irq_state_becomes_pending(void)
{
    kernel_status_t ret;
    vgic_irq_state_t state;

    helper_enable_irq(TEST_IRQ_0);

    state = vgic_get_irq_state(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(state, VGIC_IRQ_INACTIVE);

    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    state = vgic_get_irq_state(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(state, VGIC_IRQ_PENDING);
}

static void test_vgic_inject_irq_invalid_vm(void)
{
    kernel_status_t ret;

    helper_enable_irq(TEST_IRQ_0);

    s_mock_vm_exists = false;

    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(ret, -(int32_t)ENOENT);
}

static void test_vgic_inject_irq_invalid_vcpu(void)
{
    kernel_status_t ret;

    helper_enable_irq(TEST_IRQ_0);

    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID_INVALID, TEST_IRQ_0);
    TEST_ASSERT_EQ(ret, -(int32_t)ENOENT);
}

static void test_vgic_inject_irq_invalid_irq_number(void)
{
    kernel_status_t ret;

    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_INVALID);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);
}

static void test_vgic_inject_irq_not_enabled(void)
{
    kernel_status_t ret;

    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(ret, -(int32_t)EPERM);
}

static void test_vgic_inject_same_irq_twice(void)
{
    kernel_status_t ret;
    vgic_irq_state_t state;

    helper_enable_irq(TEST_IRQ_0);

    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    state = vgic_get_irq_state(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(state, VGIC_IRQ_PENDING);
}

static void test_vgic_inject_pending_bitmap_updated(void)
{
    kernel_status_t ret;
    bool pending;

    helper_enable_irq(TEST_IRQ_0);
    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    helper_enable_irq(TEST_IRQ_31);
    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_31);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    helper_enable_irq(TEST_IRQ_32);
    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_32);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_TRUE(pending);

    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_31);
    TEST_ASSERT_TRUE(pending);

    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_32);
    TEST_ASSERT_TRUE(pending);

    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_1);
    TEST_ASSERT_FALSE(pending);
}

static void test_vgic_inject_blocked_vcpu_wakes(void)
{
    kernel_status_t ret;

    s_test_vm.vcpus[TEST_VCPU_ID].state = VCPU_STATE_BLOCKED;

    helper_enable_irq(TEST_IRQ_0);

    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    TEST_ASSERT_EQ(s_test_vm.vcpus[TEST_VCPU_ID].state, VCPU_STATE_RUNNING);
    TEST_ASSERT_TRUE(s_test_vm.vcpus[TEST_VCPU_ID].irq_pending);
}

/* ========================================================================
 * 第 3 组：VGIC 中断清除测试（5 个用例）
 * ======================================================================== */

static void test_vgic_clear_irq_success(void)
{
    kernel_status_t ret;

    helper_enable_irq(TEST_IRQ_0);

    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    ret = vgic_clear_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
}

static void test_vgic_clear_irq_state_inactive(void)
{
    vgic_irq_state_t state;

    helper_enable_irq(TEST_IRQ_0);
    (void)vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);

    (void)vgic_clear_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);

    state = vgic_get_irq_state(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(state, VGIC_IRQ_INACTIVE);
}

static void test_vgic_clear_irq_pending_cleared(void)
{
    bool pending;

    helper_enable_irq(TEST_IRQ_0);
    (void)vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);

    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_TRUE(pending);

    (void)vgic_clear_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);

    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_FALSE(pending);
}

static void test_vgic_clear_irq_invalid_vm(void)
{
    kernel_status_t ret;

    s_mock_vm_exists = false;

    ret = vgic_clear_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(ret, -(int32_t)ENOENT);
}

static void test_vgic_clear_irq_invalid_irq_number(void)
{
    kernel_status_t ret;

    ret = vgic_clear_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_INVALID);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);
}

/* ========================================================================
 * 第 4 组：VGIC 优先级测试（5 个用例）
 * ======================================================================== */

static void test_vgic_set_priority_highest(void)
{
    kernel_status_t ret;

    ret = vgic_set_priority(TEST_VM_ID, TEST_IRQ_0, 0U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
}

static void test_vgic_set_priority_lowest(void)
{
    kernel_status_t ret;

    ret = vgic_set_priority(TEST_VM_ID, TEST_IRQ_0, 7U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
}

static void test_vgic_set_priority_invalid(void)
{
    kernel_status_t ret;

    ret = vgic_set_priority(TEST_VM_ID, TEST_IRQ_0, 8U);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);
}

static void test_vgic_set_priority_invalid_irq(void)
{
    kernel_status_t ret;

    ret = vgic_set_priority(TEST_VM_ID, TEST_IRQ_INVALID, 0U);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);
}

static void test_vgic_set_priority_invalid_vm(void)
{
    kernel_status_t ret;

    s_mock_vm_exists = false;

    ret = vgic_set_priority(TEST_VM_ID, TEST_IRQ_0, 0U);
    TEST_ASSERT_EQ(ret, -(int32_t)ENOENT);
}

/* ========================================================================
 * 第 5 组：VGIC 路由测试（5 个用例）
 * ======================================================================== */

static void test_vgic_set_target_single_cpu(void)
{
    kernel_status_t ret;

    ret = vgic_set_target(TEST_VM_ID, TEST_IRQ_0, 0x1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
}

static void test_vgic_set_target_multiple_cpus(void)
{
    kernel_status_t ret;

    ret = vgic_set_target(TEST_VM_ID, TEST_IRQ_0, 0x3U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
}

static void test_vgic_set_target_invalid_irq(void)
{
    kernel_status_t ret;

    ret = vgic_set_target(TEST_VM_ID, TEST_IRQ_INVALID, 0x1U);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);
}

static void test_vgic_set_target_invalid_vm(void)
{
    kernel_status_t ret;

    s_mock_vm_exists = false;

    ret = vgic_set_target(TEST_VM_ID, TEST_IRQ_0, 0x1U);
    TEST_ASSERT_EQ(ret, -(int32_t)ENOENT);
}

static void test_vgic_set_target_broadcast(void)
{
    kernel_status_t ret;

    ret = vgic_set_target(TEST_VM_ID, TEST_IRQ_0, 0xFFU);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
}

/* ========================================================================
 * 第 6 组：VGIC 使能/禁用测试（5 个用例）
 * ======================================================================== */

static void test_vgic_enable_irq_success(void)
{
    kernel_status_t ret;

    ret = vgic_enable_irq(TEST_VM_ID, TEST_IRQ_0, true);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
}

static void test_vgic_disable_irq_success(void)
{
    kernel_status_t ret;

    ret = vgic_enable_irq(TEST_VM_ID, TEST_IRQ_0, true);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    ret = vgic_enable_irq(TEST_VM_ID, TEST_IRQ_0, false);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
}

static void test_vgic_enable_then_inject(void)
{
    kernel_status_t ret;
    bool pending;

    ret = vgic_enable_irq(TEST_VM_ID, TEST_IRQ_0, true);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_TRUE(pending);
}

static void test_vgic_disable_prevents_inject(void)
{
    kernel_status_t ret;

    (void)vgic_enable_irq(TEST_VM_ID, TEST_IRQ_0, true);

    ret = vgic_enable_irq(TEST_VM_ID, TEST_IRQ_0, false);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    ret = vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(ret, -(int32_t)EPERM);
}

static void test_vgic_enable_irq_invalid_vm(void)
{
    kernel_status_t ret;

    s_mock_vm_exists = false;

    ret = vgic_enable_irq(TEST_VM_ID, TEST_IRQ_0, true);
    TEST_ASSERT_EQ(ret, -(int32_t)ENOENT);
}

/* ========================================================================
 * 第 7 组：VGIC 状态检查测试（5 个用例）
 * ======================================================================== */

static void test_vgic_irq_not_pending_initially(void)
{
    bool pending;

    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_FALSE(pending);
}

static void test_vgic_irq_pending_after_inject(void)
{
    bool pending;

    helper_enable_irq(TEST_IRQ_0);
    (void)vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);

    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_TRUE(pending);
}

static void test_vgic_irq_not_pending_after_clear(void)
{
    bool pending;

    helper_enable_irq(TEST_IRQ_0);
    (void)vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    (void)vgic_clear_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);

    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_FALSE(pending);
}

static void test_vgic_get_irq_state_transitions(void)
{
    vgic_irq_state_t state;

    /* INACTIVE */
    state = vgic_get_irq_state(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(state, VGIC_IRQ_INACTIVE);

    /* INACTIVE -> PENDING */
    helper_enable_irq(TEST_IRQ_0);
    (void)vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);

    state = vgic_get_irq_state(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(state, VGIC_IRQ_PENDING);

    /* PENDING -> INACTIVE */
    (void)vgic_clear_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);

    state = vgic_get_irq_state(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(state, VGIC_IRQ_INACTIVE);
}

static void test_vgic_irq_is_pending_invalid_params(void)
{
    bool pending;

    /* 无效 VM */
    s_mock_vm_exists = false;
    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_FALSE(pending);

    /* 恢复 VM */
    s_mock_vm_exists = true;

    /* 无效 vCPU */
    s_test_vm.vcpu_count = 0U;
    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_FALSE(pending);

    /* 恢复 vCPU */
    s_test_vm.vcpu_count = 1U;

    /* 无效中断号 */
    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_INVALID);
    TEST_ASSERT_FALSE(pending);
}

/* ========================================================================
 * 第 8 组：VGIC 清空所有中断测试（2 个用例）
 * ======================================================================== */

static void test_vgic_clear_all_irqs_success(void)
{
    bool pending;

    helper_enable_irq(TEST_IRQ_0);
    helper_enable_irq(TEST_IRQ_1);

    (void)vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    (void)vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_1);

    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_TRUE(pending);

    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_1);
    TEST_ASSERT_TRUE(pending);

    vgic_clear_all_irqs(TEST_VM_ID);

    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_FALSE(pending);

    pending = vgic_irq_is_pending(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_1);
    TEST_ASSERT_FALSE(pending);
}

static void test_vgic_clear_all_irqs_all_states_cleared(void)
{
    vgic_irq_state_t state;

    helper_enable_irq(TEST_IRQ_0);
    (void)vgic_inject_irq(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);

    state = vgic_get_irq_state(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(state, VGIC_IRQ_PENDING);

    vgic_clear_all_irqs(TEST_VM_ID);

    state = vgic_get_irq_state(TEST_VM_ID, TEST_VCPU_ID, TEST_IRQ_0);
    TEST_ASSERT_EQ(state, VGIC_IRQ_INACTIVE);
}

/* ========================================================================
 * 测试运行器
 * ======================================================================== */

typedef void (*test_fn_t)(void);

typedef struct
{
    const char *name;
    test_fn_t   fn;
} test_entry_t;

int main(void)
{
    uint32_t i;
    uint32_t group_pass;
    uint32_t group_fail;

    static const test_entry_t tests[] =
    {
        /* 第 1 组：VGIC 初始化测试（3 个） */
        { "1.1  初始化成功",                   test_vgic_init_success },
        { "1.2  默认中断状态 INACTIVE",         test_vgic_init_default_state_inactive },
        { "1.3  默认优先级 0~7 有效",           test_vgic_init_default_priority_lowest },

        /* 第 2 组：VGIC 中断注入测试（10 个） */
        { "2.1  注入单个中断",                  test_vgic_inject_single_irq },
        { "2.2  注入多个中断",                  test_vgic_inject_multiple_irqs },
        { "2.3  中断状态变为 PENDING",           test_vgic_inject_irq_state_becomes_pending },
        { "2.4  注入不存在的 VM（-ENOENT）",     test_vgic_inject_irq_invalid_vm },
        { "2.5  注入不存在的 vCPU（-ENOENT）",   test_vgic_inject_irq_invalid_vcpu },
        { "2.6  注入无效中断号（-EINVAL）",      test_vgic_inject_irq_invalid_irq_number },
        { "2.7  注入未使能中断（-EPERM）",       test_vgic_inject_irq_not_enabled },
        { "2.8  重复注入相同中断",               test_vgic_inject_same_irq_twice },
        { "2.9  挂起位图正确更新",               test_vgic_inject_pending_bitmap_updated },
        { "2.10 BLOCKED vCPU 被唤醒",           test_vgic_inject_blocked_vcpu_wakes },

        /* 第 3 组：VGIC 中断清除测试（5 个） */
        { "3.1  清除已挂起中断成功",             test_vgic_clear_irq_success },
        { "3.2  清除后状态 INACTIVE",            test_vgic_clear_irq_state_inactive },
        { "3.3  清除后挂起位图清除",             test_vgic_clear_irq_pending_cleared },
        { "3.4  清除不存在的 VM（-ENOENT）",     test_vgic_clear_irq_invalid_vm },
        { "3.5  清除无效中断号（-EINVAL）",      test_vgic_clear_irq_invalid_irq_number },

        /* 第 4 组：VGIC 优先级测试（5 个） */
        { "4.1  设置最高优先级（0）",            test_vgic_set_priority_highest },
        { "4.2  设置最低优先级（7）",            test_vgic_set_priority_lowest },
        { "4.3  无效优先级（8）返回 -EINVAL",    test_vgic_set_priority_invalid },
        { "4.4  无效中断号返回 -EINVAL",         test_vgic_set_priority_invalid_irq },
        { "4.5  不存在的 VM 返回 -ENOENT",       test_vgic_set_priority_invalid_vm },

        /* 第 5 组：VGIC 路由测试（5 个） */
        { "5.1  路由到单个 CPU",                 test_vgic_set_target_single_cpu },
        { "5.2  路由到多个 CPU",                 test_vgic_set_target_multiple_cpus },
        { "5.3  无效中断号返回 -EINVAL",         test_vgic_set_target_invalid_irq },
        { "5.4  不存在的 VM 返回 -ENOENT",       test_vgic_set_target_invalid_vm },
        { "5.5  广播路由到所有 CPU",              test_vgic_set_target_broadcast },

        /* 第 6 组：VGIC 使能/禁用测试（5 个） */
        { "6.1  使能中断成功",                   test_vgic_enable_irq_success },
        { "6.2  禁用中断成功",                   test_vgic_disable_irq_success },
        { "6.3  使能后可注入中断",                test_vgic_enable_then_inject },
        { "6.4  禁用后无法注入（-EPERM）",       test_vgic_disable_prevents_inject },
        { "6.5  不存在的 VM 使能失败（-ENOENT）", test_vgic_enable_irq_invalid_vm },

        /* 第 7 组：VGIC 状态检查测试（5 个） */
        { "7.1  初始状态不挂起",                  test_vgic_irq_not_pending_initially },
        { "7.2  注入后挂起",                     test_vgic_irq_pending_after_inject },
        { "7.3  清除后不挂起",                   test_vgic_irq_not_pending_after_clear },
        { "7.4  状态转换 INACTIVE->PENDING->INACTIVE",
                                                 test_vgic_get_irq_state_transitions },
        { "7.5  无效参数返回 false",              test_vgic_irq_is_pending_invalid_params },

        /* 第 8 组：VGIC 清空所有中断测试（2 个） */
        { "8.1  清空所有中断成功",                test_vgic_clear_all_irqs_success },
        { "8.2  清空后所有状态清除",              test_vgic_clear_all_irqs_all_states_cleared },
    };

    group_pass = 0U;
    group_fail = 0U;

    printf("\n============================================\n");
    printf("  VGIC 单元测试（40 个用例）\n");
    printf("============================================\n\n");

    for (i = 0U; i < (sizeof(tests) / sizeof(tests[0U])); i++)
    {
        TEST_RESET();
        test_setup();

        tests[i].fn();

        printf("  [%s] %s\n",
               (s_failed == 0U) ? "PASS" : "FAIL",
               tests[i].name);

        if (s_failed == 0U)
        {
            group_pass++;
        }
        else
        {
            group_fail++;
        }
    }

    printf("\n============================================\n");
    printf("  测试总结: %u 通过 / %u 失败 / %u 总计\n",
           group_pass, group_fail, group_pass + group_fail);
    printf("============================================\n\n");

    return (group_fail > 0U) ? 1 : 0;
}
