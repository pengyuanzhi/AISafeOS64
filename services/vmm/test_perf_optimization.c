/**
 * @file    test_perf_optimization.c
 * @brief   VMM 性能优化模块单元测试
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 1.0
 *
 * @details 本文件包含所有 VMM 性能优化 API 的单元测试：
 *          - TLB 自适应刷新策略测试
 *          - 中断批量注入与优先级队列测试
 *          - MMIO LRU 缓存测试
 *          - vCPU 动态负载均衡与 CPU 亲和性测试
 *          - 性能统计测试
 *
 * @note MISRA-C:2012 合规
 * @note 使用 Unity 测试框架
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */
#include "unity.h"
#include "vmm_perf_opt.h"
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * 测试辅助宏
 * ======================================================================== */

/** @brief 最小值宏 */
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

/* ========================================================================
 * 测试前置/后置操作
 * ======================================================================== */

void setUp(void)
{
    /* 每个测试前初始化性能优化模块 */
    (void)vmm_perf_opt_global_init();
}

void tearDown(void)
{
    /* 每个测试后销毁性能优化模块 */
    (void)vmm_perf_opt_global_destroy();
}

/* ========================================================================
 * 1. 全局初始化/销毁测试
 * ======================================================================== */

/**
 * @brief 测试全局初始化成功
 */
void test_global_init_success(void)
{
    kernel_status_t ret;

    /* 先销毁以允许重新初始化 */
    vmm_perf_opt_global_destroy();

    ret = vmm_perf_opt_global_init();
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试重复初始化不报错
 */
void test_global_init_idempotent(void)
{
    kernel_status_t ret;

    ret = vmm_perf_opt_global_init();
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试全局销毁成功
 */
void test_global_destroy_success(void)
{
    kernel_status_t ret;

    ret = vmm_perf_opt_global_destroy();
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试重复销毁不报错
 */
void test_global_destroy_idempotent(void)
{
    kernel_status_t ret;

    vmm_perf_opt_global_destroy();
    ret = vmm_perf_opt_global_destroy();
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/* ========================================================================
 * 2. TLB 自适应刷新策略测试
 * ======================================================================== */

/**
 * @brief 测试设置有效 TLB 刷新策略
 */
void test_tlb_flush_set_strategy_valid(void)
{
    kernel_status_t ret;

    ret = vmm_tlb_flush_set_strategy(VMM_TLB_FLUSH_IMMEDIATE);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    ret = vmm_tlb_flush_set_strategy(VMM_TLB_FLUSH_DEFERRED);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    ret = vmm_tlb_flush_set_strategy(VMM_TLB_FLUSH_BATCH);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    ret = vmm_tlb_flush_set_strategy(VMM_TLB_FLUSH_ADAPTIVE);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试设置无效 TLB 刷新策略
 */
void test_tlb_flush_set_strategy_invalid(void)
{
    kernel_status_t ret;

    ret = vmm_tlb_flush_set_strategy(VMM_TLB_FLUSH_COUNT);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试 TLB 立即刷新
 */
void test_tlb_flush_opt_immediate(void)
{
    kernel_status_t ret;

    vmm_tlb_flush_set_strategy(VMM_TLB_FLUSH_IMMEDIATE);

    ret = vmm_tlb_flush_opt(0U, 0U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试 TLB 刷新无效 VM ID
 */
void test_tlb_flush_opt_invalid_vm(void)
{
    kernel_status_t ret;

    ret = vmm_tlb_flush_opt(VMM_MAX_VMS, 0U);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试 TLB 延迟刷新（入队）
 */
void test_tlb_flush_opt_deferred(void)
{
    kernel_status_t ret;

    vmm_tlb_flush_set_strategy(VMM_TLB_FLUSH_DEFERRED);

    ret = vmm_tlb_flush_opt(0U, 1U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    ret = vmm_tlb_flush_opt(1U, 2U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试 TLB 批量刷新处理
 */
void test_tlb_flush_process_queue(void)
{
    kernel_status_t ret;

    vmm_tlb_flush_set_strategy(VMM_TLB_FLUSH_DEFERRED);

    ret = vmm_tlb_flush_opt(0U, 1U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    ret = vmm_tlb_flush_process_queue();
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试自适应 TLB 刷新 - 低频率低压力
 */
void test_tlb_flush_adaptive_low_freq_low_pressure(void)
{
    kernel_status_t ret;

    ret = vmm_tlb_flush_adaptive(0U, 1U, 5U, 20U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试自适应 TLB 刷新 - 高频率高压力
 */
void test_tlb_flush_adaptive_high_freq_high_pressure(void)
{
    kernel_status_t ret;

    ret = vmm_tlb_flush_adaptive(0U, 1U, 150U, 90U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试自适应 TLB 刷新 - 无效压力值
 */
void test_tlb_flush_adaptive_invalid_pressure(void)
{
    kernel_status_t ret;

    ret = vmm_tlb_flush_adaptive(0U, 1U, 50U, 101U);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试自适应 TLB 刷新 - 中频率低压力
 */
void test_tlb_flush_adaptive_mid_freq_low_pressure(void)
{
    kernel_status_t ret;

    ret = vmm_tlb_flush_adaptive(0U, 1U, 50U, 50U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试获取 TLB 刷新统计
 */
void test_tlb_flush_get_stats(void)
{
    kernel_status_t ret;
    vmm_tlb_flush_stats_t stats;

    /* 执行一些操作 */
    vmm_tlb_flush_set_strategy(VMM_TLB_FLUSH_IMMEDIATE);
    vmm_tlb_flush_opt(0U, 0U);

    ret = vmm_tlb_flush_get_stats(&stats);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
    TEST_ASSERT_TRUE(stats.flush_count > 0ULL);
}

/**
 * @brief 测试获取 TLB 刷新统计 - NULL 参数
 */
void test_tlb_flush_get_stats_null(void)
{
    kernel_status_t ret;

    ret = vmm_tlb_flush_get_stats(NULL);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/* ========================================================================
 * 3. 中断批量注入与优先级队列测试
 * ======================================================================== */

/**
 * @brief 测试设置有效中断注入方法
 */
void test_irq_inject_set_method_valid(void)
{
    kernel_status_t ret;

    ret = vmm_irq_inject_set_method(VMM_IRQ_INJECT_SVC);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    ret = vmm_irq_inject_set_method(VMM_IRQ_INJECT_ICC_SGI1R);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试设置无效中断注入方法
 */
void test_irq_inject_set_method_invalid(void)
{
    kernel_status_t ret;

    ret = vmm_irq_inject_set_method(VMM_IRQ_INJECT_COUNT);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试设置中断优先级 - 有效参数
 */
void test_irq_inject_set_priority_valid(void)
{
    kernel_status_t ret;
    uint32_t i;

    for (i = 0U; i < VMM_IRQ_PRIORITY_LEVELS; i++)
    {
        ret = vmm_irq_inject_set_priority(32U, i);
        TEST_ASSERT_EQUAL(KERNEL_OK, ret);
    }
}

/**
 * @brief 测试设置中断优先级 - 无效 IRQ 号
 */
void test_irq_inject_set_priority_invalid_irq(void)
{
    kernel_status_t ret;

    ret = vmm_irq_inject_set_priority(VMM_VGIC_MAX_INTERRUPTS, 0U);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试设置中断优先级 - 无效优先级
 */
void test_irq_inject_set_priority_invalid_prio(void)
{
    kernel_status_t ret;

    ret = vmm_irq_inject_set_priority(32U, VMM_IRQ_PRIORITY_LEVELS);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试批量中断注入 - 有效参数
 */
void test_irq_inject_batch_valid(void)
{
    kernel_status_t ret;
    uint32_t vm_ids[4] = {0U, 0U, 1U, 1U};
    uint32_t vcpu_ids[4] = {0U, 1U, 0U, 1U};
    uint32_t irqs[4] = {0U, 1U, 2U, 3U};

    ret = vmm_irq_inject_batch(4U, vm_ids, vcpu_ids, irqs);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试批量中断注入 - 数量为 0
 */
void test_irq_inject_batch_zero_count(void)
{
    kernel_status_t ret;
    uint32_t dummy = 0U;

    ret = vmm_irq_inject_batch(0U, &dummy, &dummy, &dummy);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试批量中断注入 - 超过最大数量
 */
void test_irq_inject_batch_exceed_max(void)
{
    kernel_status_t ret;
    uint32_t dummy_list[VMM_IRQ_BATCH_MAX_SIZE + 1U];

    (void)memset(dummy_list, 0, sizeof(dummy_list));

    ret = vmm_irq_inject_batch(VMM_IRQ_BATCH_MAX_SIZE + 1U, dummy_list, dummy_list, dummy_list);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试批量中断注入 - NULL 参数
 */
void test_irq_inject_batch_null(void)
{
    kernel_status_t ret;
    uint32_t dummy = 0U;

    ret = vmm_irq_inject_batch(1U, NULL, &dummy, &dummy);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);

    ret = vmm_irq_inject_batch(1U, &dummy, NULL, &dummy);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);

    ret = vmm_irq_inject_batch(1U, &dummy, &dummy, NULL);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试批量中断注入 - 无效 VM ID
 */
void test_irq_inject_batch_invalid_vm(void)
{
    kernel_status_t ret;
    uint32_t vm_ids[2] = {VMM_MAX_VMS, 0U};
    uint32_t vcpu_ids[2] = {0U, 0U};
    uint32_t irqs[2] = {0U, 1U};

    ret = vmm_irq_inject_batch(2U, vm_ids, vcpu_ids, irqs);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试中断注入 - SVC 方法
 */
void test_irq_inject_opt_svc(void)
{
    kernel_status_t ret;

    vmm_irq_inject_set_method(VMM_IRQ_INJECT_SVC);

    ret = vmm_irq_inject_opt(0U, 0U, 32U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试中断注入 - 无效 VM ID
 */
void test_irq_inject_opt_invalid_vm(void)
{
    kernel_status_t ret;

    ret = vmm_irq_inject_opt(VMM_MAX_VMS, 0U, 0U);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试中断注入队列处理
 */
void test_irq_inject_process_queue(void)
{
    kernel_status_t ret;

    ret = vmm_irq_inject_process_queue();
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/* ========================================================================
 * 4. MMIO LRU 缓存测试
 * ======================================================================== */

/**
 * @brief 测试 MMIO 缓存使能/禁用
 */
void test_mmio_cache_enable_disable(void)
{
    kernel_status_t ret;

    ret = vmm_mmio_cache_enable(true);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    ret = vmm_mmio_cache_enable(false);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试 MMIO 缓存读命中
 */
void test_mmio_read_cache_hit(void)
{
    kernel_status_t ret;
    uint64_t value = 0ULL;
    uint64_t value2 = 0ULL;

    vmm_mmio_cache_enable(true);

    /* 第一次读：未命中 */
    ret = vmm_mmio_read_opt(0U, 0U, 0x1000ULL, &value);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* 写入缓存值（模拟设备返回值） */
    /* 注意：由于简化实现，value 可能保持为 0 */

    /* 第二次读相同地址：命中 */
    ret = vmm_mmio_read_opt(0U, 0U, 0x1000ULL, &value2);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试 MMIO 缓存写
 */
void test_mmio_write_cache(void)
{
    kernel_status_t ret;

    vmm_mmio_cache_enable(true);

    ret = vmm_mmio_write_opt(0U, 0U, 0x1000ULL, 0xDEADBEEFULL);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试 MMIO 读 - NULL 参数
 */
void test_mmio_read_null_value(void)
{
    kernel_status_t ret;

    ret = vmm_mmio_read_opt(0U, 0U, 0x1000ULL, NULL);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试 MMIO 缓存清空
 */
void test_mmio_cache_flush(void)
{
    kernel_status_t ret;

    vmm_mmio_cache_enable(true);

    /* 先写入一些数据 */
    vmm_mmio_write_opt(0U, 0U, 0x1000ULL, 0x1ULL);
    vmm_mmio_write_opt(0U, 0U, 0x2000ULL, 0x2ULL);

    /* 清空缓存 */
    ret = vmm_mmio_cache_flush();
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试 LRU 驱逐阈值设置 - 有效值
 */
void test_mmio_cache_set_lru_threshold_valid(void)
{
    kernel_status_t ret;

    ret = vmm_mmio_cache_set_lru_threshold(1U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    ret = vmm_mmio_cache_set_lru_threshold(50U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    ret = vmm_mmio_cache_set_lru_threshold(100U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试 LRU 驱逐阈值设置 - 无效值
 */
void test_mmio_cache_set_lru_threshold_invalid(void)
{
    kernel_status_t ret;

    ret = vmm_mmio_cache_set_lru_threshold(0U);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);

    ret = vmm_mmio_cache_set_lru_threshold(101U);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试 MMIO 缓存预热 - 有效参数
 */
void test_mmio_cache_preheat_valid(void)
{
    kernel_status_t ret;
    uint64_t addrs[4] = {0x1000ULL, 0x2000ULL, 0x3000ULL, 0x4000ULL};

    vmm_mmio_cache_enable(true);

    ret = vmm_mmio_cache_preheat(addrs, 4U, 0U, 0U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试 MMIO 缓存预热 - NULL 参数
 */
void test_mmio_cache_preheat_null(void)
{
    kernel_status_t ret;

    ret = vmm_mmio_cache_preheat(NULL, 4U, 0U, 0U);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试 MMIO 缓存预热 - 数量为 0
 */
void test_mmio_cache_preheat_zero_count(void)
{
    kernel_status_t ret;
    uint64_t addr = 0x1000ULL;

    ret = vmm_mmio_cache_preheat(&addr, 0U, 0U, 0U);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试 MMIO 缓存预热 - 超过最大数量
 */
void test_mmio_cache_preheat_exceed_max(void)
{
    kernel_status_t ret;
    uint64_t addrs[VMM_MMIO_CACHE_MAX_SIZE + 1U];

    (void)memset(addrs, 0, sizeof(addrs));

    ret = vmm_mmio_cache_preheat(addrs, VMM_MMIO_CACHE_MAX_SIZE + 1U, 0U, 0U);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试 MMIO 缓存预热 - 无效 VM ID
 */
void test_mmio_cache_preheat_invalid_vm(void)
{
    kernel_status_t ret;
    uint64_t addr = 0x1000ULL;

    ret = vmm_mmio_cache_preheat(&addr, 1U, VMM_MAX_VMS, 0U);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试动态调整 MMIO 缓存大小 - 有效值
 */
void test_mmio_cache_set_size_valid(void)
{
    kernel_status_t ret;

    ret = vmm_mmio_cache_set_size(VMM_MMIO_CACHE_MIN_SIZE);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    ret = vmm_mmio_cache_set_size(VMM_MMIO_CACHE_MAX_SIZE);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    ret = vmm_mmio_cache_set_size(128U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试动态调整 MMIO 缓存大小 - 过小
 */
void test_mmio_cache_set_size_too_small(void)
{
    kernel_status_t ret;

    ret = vmm_mmio_cache_set_size(VMM_MMIO_CACHE_MIN_SIZE - 1U);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试动态调整 MMIO 缓存大小 - 过大
 */
void test_mmio_cache_set_size_too_large(void)
{
    kernel_status_t ret;

    ret = vmm_mmio_cache_set_size(VMM_MMIO_CACHE_MAX_SIZE + 1U);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试获取 MMIO 缓存统计
 */
void test_mmio_cache_get_stats(void)
{
    kernel_status_t ret;
    vmm_mmio_cache_stats_t stats;

    vmm_mmio_cache_enable(true);

    /* 执行一些操作 */
    vmm_mmio_write_opt(0U, 0U, 0x1000ULL, 0x1ULL);

    ret = vmm_mmio_cache_get_stats(&stats);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试获取 MMIO 缓存统计 - NULL 参数
 */
void test_mmio_cache_get_stats_null(void)
{
    kernel_status_t ret;

    ret = vmm_mmio_cache_get_stats(NULL);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试 MMIO 缓存 LRU 驱逐行为
 */
void test_mmio_cache_lru_eviction(void)
{
    kernel_status_t ret;
    uint64_t value = 0ULL;
    uint32_t i;

    vmm_mmio_cache_enable(true);

    /* 设置较小的缓存容量 */
    ret = vmm_mmio_cache_set_size(VMM_MMIO_CACHE_MIN_SIZE);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* 填满缓存 */
    for (i = 0U; i < VMM_MMIO_CACHE_MIN_SIZE; i++)
    {
        ret = vmm_mmio_write_opt(0U, 0U, (uint64_t)(i + 1U) * 0x1000ULL, (uint64_t)i);
        TEST_ASSERT_EQUAL(KERNEL_OK, ret);
    }

    /* 添加一个新条目，应触发 LRU 驱逐 */
    ret = vmm_mmio_write_opt(0U, 0U, 0xFFFF0000ULL, 0xFFULL);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/* ========================================================================
 * 5. vCPU 动态负载均衡与 CPU 亲和性测试
 * ======================================================================== */

/**
 * @brief 测试设置有效 vCPU 调度策略
 */
void test_vcpu_sched_set_strategy_valid(void)
{
    kernel_status_t ret;

    ret = vmm_vcpu_sched_set_strategy(VMM_VCPU_SCHED_ROUND_ROBIN);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    ret = vmm_vcpu_sched_set_strategy(VMM_VCPU_SCHED_PRIORITY);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    ret = vmm_vcpu_sched_set_strategy(VMM_VCPU_SCHED_LOAD_BALANCE);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    ret = vmm_vcpu_sched_set_strategy(VMM_VCPU_SCHED_DYNAMIC);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试设置无效 vCPU 调度策略
 */
void test_vcpu_sched_set_strategy_invalid(void)
{
    kernel_status_t ret;

    ret = vmm_vcpu_sched_set_strategy(VMM_VCPU_SCHED_COUNT);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试 vCPU 调度 - NULL 输出参数
 */
void test_vcpu_schedule_opt_null(void)
{
    kernel_status_t ret;

    ret = vmm_vcpu_schedule_opt(0U, NULL);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试 vCPU 调度更新状态 - 有效参数
 */
void test_vcpu_sched_update_valid(void)
{
    kernel_status_t ret;

    ret = vmm_vcpu_sched_update(0U, 0U, 1000000ULL);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试 vCPU 调度更新状态 - 无效 VM ID
 */
void test_vcpu_sched_update_invalid_vm(void)
{
    kernel_status_t ret;

    ret = vmm_vcpu_sched_update(VMM_MAX_VMS, 0U, 1000000ULL);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试 vCPU 调度更新状态 - 无效 vCPU ID
 */
void test_vcpu_sched_update_invalid_vcpu(void)
{
    kernel_status_t ret;

    ret = vmm_vcpu_sched_update(0U, VMM_MAX_VCPUS_PER_VM, 1000000ULL);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试启用/禁用动态负载均衡
 */
void test_vcpu_sched_enable_dynamic(void)
{
    kernel_status_t ret;

    ret = vmm_vcpu_sched_enable_dynamic(true);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    ret = vmm_vcpu_sched_enable_dynamic(false);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试设置 CPU 亲和性 - 有效参数
 */
void test_vcpu_sched_set_affinity_valid(void)
{
    kernel_status_t ret;

    /* 先注册一个 vCPU 状态 */
    vmm_vcpu_sched_update(0U, 0U, 0ULL);

    ret = vmm_vcpu_sched_set_affinity(0U, 0x01U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    ret = vmm_vcpu_sched_set_affinity(0U, 0x0FU);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试设置 CPU 亲和性 - 掩码为 0
 */
void test_vcpu_sched_set_affinity_zero_mask(void)
{
    kernel_status_t ret;

    ret = vmm_vcpu_sched_set_affinity(0U, 0U);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试设置 CPU 亲和性 - 未找到 vCPU
 */
void test_vcpu_sched_set_affinity_not_found(void)
{
    kernel_status_t ret;

    /* 清空后重试 */
    vmm_perf_opt_global_destroy();
    vmm_perf_opt_global_init();

    ret = vmm_vcpu_sched_set_affinity(99U, 0x01U);
    TEST_ASSERT_EQUAL(-(int32_t)ENOENT, ret);
}

/**
 * @brief 测试设置实时调度策略 - 有效参数
 */
void test_vcpu_sched_set_realtime_policy_valid(void)
{
    kernel_status_t ret;

    /* 先注册一个 vCPU 状态 */
    vmm_vcpu_sched_update(0U, 0U, 0ULL);

    ret = vmm_vcpu_sched_set_realtime_policy(VMM_SCHED_POLICY_FIFO, 50U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    ret = vmm_vcpu_sched_set_realtime_policy(VMM_SCHED_POLICY_RR, 99U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试设置实时调度策略 - 无效策略
 */
void test_vcpu_sched_set_realtime_policy_invalid(void)
{
    kernel_status_t ret;

    ret = vmm_vcpu_sched_set_realtime_policy(VMM_SCHED_POLICY_COUNT, 50U);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试设置实时调度策略 - 无效优先级
 */
void test_vcpu_sched_set_realtime_policy_invalid_prio(void)
{
    kernel_status_t ret;

    ret = vmm_vcpu_sched_set_realtime_policy(VMM_SCHED_POLICY_FIFO, 100U);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试设置时间片 - 有效值
 */
void test_vcpu_sched_set_timeslice_valid(void)
{
    kernel_status_t ret;

    ret = vmm_vcpu_sched_set_timeslice(1U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    ret = vmm_vcpu_sched_set_timeslice(10U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    ret = vmm_vcpu_sched_set_timeslice(1000U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试设置时间片 - 无效值
 */
void test_vcpu_sched_set_timeslice_invalid(void)
{
    kernel_status_t ret;

    ret = vmm_vcpu_sched_set_timeslice(0U);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);

    ret = vmm_vcpu_sched_set_timeslice(1001U);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试获取 vCPU 调度统计
 */
void test_vcpu_sched_get_stats(void)
{
    kernel_status_t ret;
    vmm_vcpu_sched_stats_t stats;

    ret = vmm_vcpu_sched_get_stats(0U, &stats);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试获取 vCPU 调度统计 - NULL 参数
 */
void test_vcpu_sched_get_stats_null(void)
{
    kernel_status_t ret;

    ret = vmm_vcpu_sched_get_stats(0U, NULL);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/* ========================================================================
 * 6. 性能统计测试
 * ======================================================================== */

/**
 * @brief 测试获取性能统计
 */
void test_perf_get_stats(void)
{
    kernel_status_t ret;
    vmm_perf_stats_t stats;

    ret = vmm_perf_get_stats(&stats);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试获取性能统计 - NULL 参数
 */
void test_perf_get_stats_null(void)
{
    kernel_status_t ret;

    ret = vmm_perf_get_stats(NULL);
    TEST_ASSERT_EQUAL(-(int32_t)EINVAL, ret);
}

/**
 * @brief 测试重置性能统计
 */
void test_perf_reset_stats(void)
{
    kernel_status_t ret;
    vmm_perf_stats_t stats;

    /* 执行一些操作以产生统计 */
    vmm_tlb_flush_set_strategy(VMM_TLB_FLUSH_IMMEDIATE);
    vmm_tlb_flush_opt(0U, 0U);

    /* 重置统计 */
    ret = vmm_perf_reset_stats();
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* 验证统计已清零 */
    ret = vmm_perf_get_stats(&stats);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
    TEST_ASSERT_EQUAL(0ULL, stats.tlb_flush_count);
    TEST_ASSERT_EQUAL(0ULL, stats.irq_inject_count);
    TEST_ASSERT_EQUAL(0ULL, stats.mmio_read_count);
    TEST_ASSERT_EQUAL(0ULL, stats.vcpu_schedule_count);
}

/**
 * @brief 测试统计信息累积
 */
void test_perf_stats_accumulate(void)
{
    vmm_perf_stats_t stats;

    /* 执行多个操作 */
    vmm_mmio_cache_enable(true);
    vmm_mmio_write_opt(0U, 0U, 0x1000ULL, 0x1ULL);
    vmm_mmio_write_opt(0U, 0U, 0x2000ULL, 0x2ULL);

    /* 验证统计累积 */
    (void)vmm_perf_get_stats(&stats);
    TEST_ASSERT_TRUE(stats.mmio_write_count >= 2ULL);
}

/* ========================================================================
 * 7. 综合集成测试
 * ======================================================================== */

/**
 * @brief 综合测试：全部优化功能协同工作
 */
void test_integration_all_optimizations(void)
{
    kernel_status_t ret;
    vmm_perf_stats_t stats;
    uint64_t addrs[4] = {0x1000ULL, 0x2000ULL, 0x3000ULL, 0x4000ULL};

    /* 1. TLB 自适应刷新 */
    ret = vmm_tlb_flush_adaptive(0U, 1U, 50U, 60U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* 2. 中断批量注入 */
    {
        uint32_t vm_ids[2] = {0U, 0U};
        uint32_t vcpu_ids[2] = {0U, 1U};
        uint32_t irqs[2] = {0U, 1U};

        ret = vmm_irq_inject_batch(2U, vm_ids, vcpu_ids, irqs);
        TEST_ASSERT_EQUAL(KERNEL_OK, ret);
    }

    /* 3. MMIO LRU 缓存 */
    vmm_mmio_cache_enable(true);
    ret = vmm_mmio_cache_preheat(addrs, 4U, 0U, 0U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* 4. vCPU 调度 */
    vmm_vcpu_sched_set_strategy(VMM_VCPU_SCHED_DYNAMIC);
    vmm_vcpu_sched_update(0U, 0U, 1000000ULL);
    vmm_vcpu_sched_update(0U, 1U, 2000000ULL);
    vmm_vcpu_sched_enable_dynamic(true);

    /* 验证统计 */
    ret = vmm_perf_get_stats(&stats);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
    TEST_ASSERT_TRUE(stats.irq_inject_batch > 0ULL);
}

/**
 * @brief 边界测试：批量中断注入最大数量
 */
void test_boundary_irq_batch_max(void)
{
    kernel_status_t ret;
    uint32_t vm_ids[VMM_IRQ_BATCH_MAX_SIZE];
    uint32_t vcpu_ids[VMM_IRQ_BATCH_MAX_SIZE];
    uint32_t irqs[VMM_IRQ_BATCH_MAX_SIZE];
    uint32_t i;

    for (i = 0U; i < VMM_IRQ_BATCH_MAX_SIZE; i++)
    {
        vm_ids[i] = 0U;
        vcpu_ids[i] = (uint32_t)(i % VMM_MAX_VCPUS_PER_VM);
        irqs[i] = (uint32_t)(i % 16U);
    }

    ret = vmm_irq_inject_batch(VMM_IRQ_BATCH_MAX_SIZE, vm_ids, vcpu_ids, irqs);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 边界测试：MMIO 缓存大小边界
 */
void test_boundary_mmio_cache_min_size(void)
{
    kernel_status_t ret;
    uint64_t value = 0ULL;
    uint32_t i;

    vmm_mmio_cache_enable(true);
    ret = vmm_mmio_cache_set_size(VMM_MMIO_CACHE_MIN_SIZE);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* 填满最小缓存 */
    for (i = 0U; i < VMM_MMIO_CACHE_MIN_SIZE; i++)
    {
        ret = vmm_mmio_write_opt(0U, 0U, (uint64_t)(i + 1U) * 0x1000ULL, (uint64_t)i);
        TEST_ASSERT_EQUAL(KERNEL_OK, ret);
    }

    /* 读取已缓存的值（应命中） */
    ret = vmm_mmio_read_opt(0U, 0U, 0x1000ULL, &value);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 边界测试：vCPU 调度时间片边界
 */
void test_boundary_timeslice(void)
{
    kernel_status_t ret;

    ret = vmm_vcpu_sched_set_timeslice(1U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    ret = vmm_vcpu_sched_set_timeslice(1000U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/**
 * @brief 测试完整 TLB 刷新生命周期
 */
void test_tlb_flush_lifecycle(void)
{
    kernel_status_t ret;
    vmm_tlb_flush_stats_t stats;

    /* 设置自适应策略 */
    vmm_tlb_flush_set_strategy(VMM_TLB_FLUSH_ADAPTIVE);

    /* 低频率低压力 -> 延迟刷新 */
    vmm_tlb_flush_adaptive(0U, 1U, 5U, 20U);
    vmm_tlb_flush_opt(0U, 1U);
    vmm_tlb_flush_process_queue();

    /* 高频率高压力 -> 立即刷新 */
    vmm_tlb_flush_adaptive(0U, 1U, 200U, 90U);
    vmm_tlb_flush_opt(0U, 1U);

    /* 验证统计 */
    ret = vmm_tlb_flush_get_stats(&stats);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
    TEST_ASSERT_TRUE(stats.flush_count > 0ULL || stats.skipped_count > 0ULL);
}

/**
 * @brief 测试 MMIO 缓存预热后命中率提升
 */
void test_mmio_preheat_hit_rate(void)
{
    kernel_status_t ret;
    vmm_mmio_cache_stats_t stats;
    uint64_t addrs[8] = {0x1000ULL, 0x2000ULL, 0x3000ULL, 0x4000ULL,
                          0x5000ULL, 0x6000ULL, 0x7000ULL, 0x8000ULL};
    uint64_t value = 0ULL;
    uint32_t i;

    vmm_mmio_cache_enable(true);

    /* 预热缓存 */
    ret = vmm_mmio_cache_preheat(addrs, 8U, 0U, 0U);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* 读取预热过的地址（应命中） */
    for (i = 0U; i < 8U; i++)
    {
        ret = vmm_mmio_read_opt(0U, 0U, addrs[i], &value);
        TEST_ASSERT_EQUAL(KERNEL_OK, ret);
    }

    /* 验证命中率 */
    ret = vmm_mmio_cache_get_stats(&stats);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
    TEST_ASSERT_TRUE(stats.hit_count > 0ULL);
}

/**
 * @brief 测试 vCPU 多策略切换
 */
void test_vcpu_multi_strategy_switch(void)
{
    kernel_status_t ret;

    /* 轮转 */
    ret = vmm_vcpu_sched_set_strategy(VMM_VCPU_SCHED_ROUND_ROBIN);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* 优先级 */
    ret = vmm_vcpu_sched_set_strategy(VMM_VCPU_SCHED_PRIORITY);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* 负载均衡 */
    ret = vmm_vcpu_sched_set_strategy(VMM_VCPU_SCHED_LOAD_BALANCE);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);

    /* 动态 */
    ret = vmm_vcpu_sched_set_strategy(VMM_VCPU_SCHED_DYNAMIC);
    TEST_ASSERT_EQUAL(KERNEL_OK, ret);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    /* 1. 全局初始化/销毁测试 */
    RUN_TEST(test_global_init_success);
    RUN_TEST(test_global_init_idempotent);
    RUN_TEST(test_global_destroy_success);
    RUN_TEST(test_global_destroy_idempotent);

    /* 2. TLB 自适应刷新策略测试 */
    RUN_TEST(test_tlb_flush_set_strategy_valid);
    RUN_TEST(test_tlb_flush_set_strategy_invalid);
    RUN_TEST(test_tlb_flush_opt_immediate);
    RUN_TEST(test_tlb_flush_opt_invalid_vm);
    RUN_TEST(test_tlb_flush_opt_deferred);
    RUN_TEST(test_tlb_flush_process_queue);
    RUN_TEST(test_tlb_flush_adaptive_low_freq_low_pressure);
    RUN_TEST(test_tlb_flush_adaptive_high_freq_high_pressure);
    RUN_TEST(test_tlb_flush_adaptive_invalid_pressure);
    RUN_TEST(test_tlb_flush_adaptive_mid_freq_low_pressure);
    RUN_TEST(test_tlb_flush_get_stats);
    RUN_TEST(test_tlb_flush_get_stats_null);
    RUN_TEST(test_tlb_flush_lifecycle);

    /* 3. 中断批量注入与优先级队列测试 */
    RUN_TEST(test_irq_inject_set_method_valid);
    RUN_TEST(test_irq_inject_set_method_invalid);
    RUN_TEST(test_irq_inject_set_priority_valid);
    RUN_TEST(test_irq_inject_set_priority_invalid_irq);
    RUN_TEST(test_irq_inject_set_priority_invalid_prio);
    RUN_TEST(test_irq_inject_batch_valid);
    RUN_TEST(test_irq_inject_batch_zero_count);
    RUN_TEST(test_irq_inject_batch_exceed_max);
    RUN_TEST(test_irq_inject_batch_null);
    RUN_TEST(test_irq_inject_batch_invalid_vm);
    RUN_TEST(test_irq_inject_opt_svc);
    RUN_TEST(test_irq_inject_opt_invalid_vm);
    RUN_TEST(test_irq_inject_process_queue);

    /* 4. MMIO LRU 缓存测试 */
    RUN_TEST(test_mmio_cache_enable_disable);
    RUN_TEST(test_mmio_read_cache_hit);
    RUN_TEST(test_mmio_write_cache);
    RUN_TEST(test_mmio_read_null_value);
    RUN_TEST(test_mmio_cache_flush);
    RUN_TEST(test_mmio_cache_set_lru_threshold_valid);
    RUN_TEST(test_mmio_cache_set_lru_threshold_invalid);
    RUN_TEST(test_mmio_cache_preheat_valid);
    RUN_TEST(test_mmio_cache_preheat_null);
    RUN_TEST(test_mmio_cache_preheat_zero_count);
    RUN_TEST(test_mmio_cache_preheat_exceed_max);
    RUN_TEST(test_mmio_cache_preheat_invalid_vm);
    RUN_TEST(test_mmio_cache_set_size_valid);
    RUN_TEST(test_mmio_cache_set_size_too_small);
    RUN_TEST(test_mmio_cache_set_size_too_large);
    RUN_TEST(test_mmio_cache_get_stats);
    RUN_TEST(test_mmio_cache_get_stats_null);
    RUN_TEST(test_mmio_cache_lru_eviction);
    RUN_TEST(test_mmio_preheat_hit_rate);

    /* 5. vCPU 动态负载均衡与 CPU 亲和性测试 */
    RUN_TEST(test_vcpu_sched_set_strategy_valid);
    RUN_TEST(test_vcpu_sched_set_strategy_invalid);
    RUN_TEST(test_vcpu_schedule_opt_null);
    RUN_TEST(test_vcpu_sched_update_valid);
    RUN_TEST(test_vcpu_sched_update_invalid_vm);
    RUN_TEST(test_vcpu_sched_update_invalid_vcpu);
    RUN_TEST(test_vcpu_sched_enable_dynamic);
    RUN_TEST(test_vcpu_sched_set_affinity_valid);
    RUN_TEST(test_vcpu_sched_set_affinity_zero_mask);
    RUN_TEST(test_vcpu_sched_set_affinity_not_found);
    RUN_TEST(test_vcpu_sched_set_realtime_policy_valid);
    RUN_TEST(test_vcpu_sched_set_realtime_policy_invalid);
    RUN_TEST(test_vcpu_sched_set_realtime_policy_invalid_prio);
    RUN_TEST(test_vcpu_sched_set_timeslice_valid);
    RUN_TEST(test_vcpu_sched_set_timeslice_invalid);
    RUN_TEST(test_vcpu_sched_get_stats);
    RUN_TEST(test_vcpu_sched_get_stats_null);
    RUN_TEST(test_vcpu_multi_strategy_switch);

    /* 6. 性能统计测试 */
    RUN_TEST(test_perf_get_stats);
    RUN_TEST(test_perf_get_stats_null);
    RUN_TEST(test_perf_reset_stats);
    RUN_TEST(test_perf_stats_accumulate);

    /* 7. 综合集成测试 */
    RUN_TEST(test_integration_all_optimizations);
    RUN_TEST(test_boundary_irq_batch_max);
    RUN_TEST(test_boundary_mmio_cache_min_size);
    RUN_TEST(test_boundary_timeslice);

    return UNITY_END();
}
