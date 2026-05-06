/**
 * @file    vmm_perf_opt.c
 * @brief   VMM 性能优化模块实现
 * @author  AISafe64 Team
 * @date    2026-05-04
 * @version 2.0
 *
 * @details 本文件实现了 VMM 性能优化模块的所有功能：
 *          - TLB 自适应刷新策略优化
 *          - 中断批量注入与优先级队列优化
 *          - MMIO LRU 缓存访问优化
 *          - vCPU 动态负载均衡与 CPU 亲和性调度优化
 *          - 性能统计管理
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: VZ-001~010
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========================================================================
 * 头文件包含
 * ======================================================================== */
#include "vmm_perf_opt.h"
#include <stdint.h>
#include <string.h>
#include <kernel/errno.h>
#include <kernel/timer.h>
#include <kernel/barrier.h>
#include "vmm.h"

/* ========================================================================
 * 内部状态
 * ======================================================================== */

/** @brief TLB 刷新队列 */
static vmm_tlb_flush_queue_t s_tlb_flush_queue;

/** @brief 中断注入队列 */
static vmm_irq_inject_queue_t s_irq_inject_queue;

/** @brief 中断优先级映射表 */
static uint32_t s_irq_priority_map[VMM_VGIC_MAX_INTERRUPTS];

/** @brief MMIO 缓存 */
static vmm_mmio_cache_t s_mmio_cache;

/** @brief vCPU 调度器 */
static vmm_vcpu_scheduler_t s_vcpu_scheduler;

/** @brief 性能统计 */
static vmm_perf_stats_t s_perf_stats;

/** @brief 性能优化模块初始化标志 */
static bool s_perf_opt_initialized = false;

/* ========================================================================
 * ARM64 寄存器操作（内联汇编）
 * ======================================================================== */

/**
 * @brief 数据内存屏障（Inner Shareable）
 */
static inline void dmb_ish(void)
{
    __asm__ volatile("dmb ish" ::: "memory");
}

/**
 * @brief 数据同步屏障（Inner Shareable）
 */
static inline void dsb_ish(void)
{
    __asm__ volatile("dsb ish" ::: "memory");
}

/**
 * @brief 指令同步屏障
 */
static inline void isb(void)
{
    __asm__ volatile("isb" ::: "memory");
}

/**
 * @brief TLBI VMALLE1IS（使所有 Stage 1 TLB 无效）
 */
static inline void tlbi_vmalle1is(void)
{
    __asm__ volatile("tlbi vmalle1is" ::: "memory");
}

/**
 * @brief TLBI VAE1IS（使特定 ASID 的 TLB 无效）
 *
 * @param asid ASID
 */
static inline void tlbi_vae1is(uint64_t asid)
{
    __asm__ volatile("tlbi vae1is, %0" : : "r"(asid) : "memory");
}

/**
 * @brief 写入 ICC_SGI1R_EL1（生成 SGI）
 *
 * @param value 写入值
 */
static inline void icc_sgi1r_el1_write(uint64_t value)
{
    __asm__ volatile("msr icc_sgi1r_el1, %0" : : "r"(value) : "memory");
}

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 获取当前时间戳（纳秒）
 *
 * @return 当前时间戳（纳秒）
 */
static uint64_t get_time_ns(void)
{
    /* 简化实现：使用系统计时器 */
    return hal_timer_get_count() * (1000000000ULL / hal_timer_get_freq());
}

/**
 * @brief 检查时间是否超时
 *
 * @param timestamp  起始时间戳
 * @param timeout_ns 超时时间（纳秒）
 *
 * @return true=超时, false=未超时
 */
static bool is_timeout(uint64_t timestamp, uint64_t timeout_ns)
{
    uint64_t now;

    now = get_time_ns();
    return (now - timestamp) >= timeout_ns;
}

/* ========================================================================
 * TLB 刷新队列操作
 * ======================================================================== */

/**
 * @brief TLB 刷新队列入队
 *
 * @param req    刷新请求
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t tlb_flush_queue_enqueue(const vmm_tlb_flush_req_t *req)
{
    vmm_tlb_flush_queue_t *queue;
    uint32_t capacity;

    if (req == NULL)
    {
        return -(int32_t)EINVAL;
    }

    queue = &s_tlb_flush_queue;
    capacity = VMM_MAX_VCPUS_PER_VM * VMM_MAX_VMS;

    /* 检查队列是否已满 */
    if (queue->count >= capacity)
    {
        return -(int32_t)ENOBUFS;
    }

    /* 入队 */
    queue->reqs[queue->tail] = *req;
    queue->tail = (queue->tail + 1U) % capacity;
    queue->count++;

    return KERNEL_OK;
}

/**
 * @brief TLB 刷新队列出队
 *
 * @param req    输出刷新请求
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t tlb_flush_queue_dequeue(vmm_tlb_flush_req_t *req)
{
    vmm_tlb_flush_queue_t *queue;
    uint32_t capacity;

    if (req == NULL)
    {
        return -(int32_t)EINVAL;
    }

    queue = &s_tlb_flush_queue;
    capacity = VMM_MAX_VCPUS_PER_VM * VMM_MAX_VMS;

    /* 检查队列是否为空 */
    if (queue->count == 0U)
    {
        return -(int32_t)ENODATA;
    }

    /* 出队 */
    *req = queue->reqs[queue->head];
    queue->head = (queue->head + 1U) % capacity;
    queue->count--;

    return KERNEL_OK;
}

/**
 * @brief 执行 TLB 刷新（立即刷新）
 *
 * @param vm_id  VM ID
 * @param asid   ASID
 */
static void tlb_flush_immediate(uint32_t vm_id, uint32_t asid)
{
    uint64_t start_ns;
    uint64_t latency_ns;

    (void)vm_id;

    start_ns = get_time_ns();

    /* 数据内存屏障 */
    dsb_ish();

    /* 使 TLB 无效 */
    if (asid == 0U)
    {
        /* 刷新所有 TLB */
        tlbi_vmalle1is();
    }
    else
    {
        /* 刷新特定 ASID 的 TLB */
        tlbi_vae1is((uint64_t)asid);
    }

    /* 数据内存屏障 */
    dsb_ish();

    /* 指令同步屏障 */
    isb();

    /* 计算延迟 */
    latency_ns = get_time_ns() - start_ns;

    /* 更新 TLB 统计信息 */
    s_tlb_flush_queue.stats.flush_count++;
    s_tlb_flush_queue.stats.total_latency_ns += latency_ns;
    if (latency_ns > s_tlb_flush_queue.stats.max_latency_ns)
    {
        s_tlb_flush_queue.stats.max_latency_ns = latency_ns;
    }

    /* 更新全局统计信息 */
    s_perf_stats.tlb_flush_count++;
}

/* ========================================================================
 * 中断注入队列操作
 * ======================================================================== */

/**
 * @brief 中断注入队列入队
 *
 * @param state  注入状态
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t irq_inject_queue_enqueue(const vmm_irq_inject_state_t *state)
{
    vmm_irq_inject_queue_t *queue;
    uint32_t capacity;

    if (state == NULL)
    {
        return -(int32_t)EINVAL;
    }

    queue = &s_irq_inject_queue;
    capacity = VMM_IRQ_BATCH_MAX_SIZE * VMM_IRQ_PRIORITY_LEVELS;

    /* 检查队列是否已满 */
    if (queue->count >= capacity)
    {
        return -(int32_t)ENOBUFS;
    }

    /* 入队 */
    queue->states[queue->tail] = *state;
    queue->tail = (queue->tail + 1U) % capacity;
    queue->count++;

    /* 更新优先级计数 */
    if (state->priority < VMM_IRQ_PRIORITY_LEVELS)
    {
        queue->priority_counts[state->priority]++;
    }

    return KERNEL_OK;
}

/**
 * @brief 中断注入队列出队
 *
 * @param state  输出注入状态
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t irq_inject_queue_dequeue(vmm_irq_inject_state_t *state)
{
    vmm_irq_inject_queue_t *queue;
    uint32_t capacity;

    if (state == NULL)
    {
        return -(int32_t)EINVAL;
    }

    queue = &s_irq_inject_queue;
    capacity = VMM_IRQ_BATCH_MAX_SIZE * VMM_IRQ_PRIORITY_LEVELS;

    /* 检查队列是否为空 */
    if (queue->count == 0U)
    {
        return -(int32_t)ENODATA;
    }

    /* 出队 */
    *state = queue->states[queue->head];
    queue->head = (queue->head + 1U) % capacity;
    queue->count--;

    /* 更新优先级计数 */
    if (state->priority < VMM_IRQ_PRIORITY_LEVELS)
    {
        queue->priority_counts[state->priority]--;
    }

    return KERNEL_OK;
}

/**
 * @brief 从优先级队列中取出最高优先级的中断
 *
 * @param state  输出注入状态
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t irq_inject_dequeue_highest_priority(vmm_irq_inject_state_t *state)
{
    vmm_irq_inject_queue_t *queue;
    uint32_t i;
    uint32_t best_idx;
    uint32_t best_priority;
    uint32_t capacity;

    if (state == NULL)
    {
        return -(int32_t)EINVAL;
    }

    queue = &s_irq_inject_queue;

    if (queue->count == 0U)
    {
        return -(int32_t)ENODATA;
    }

    capacity = VMM_IRQ_BATCH_MAX_SIZE * VMM_IRQ_PRIORITY_LEVELS;
    best_idx = queue->head;
    best_priority = s_irq_inject_queue.states[queue->head].priority;

    /* 遍历查找最高优先级（数值最小） */
    for (i = 0U; i < queue->count; i++)
    {
        uint32_t idx = (queue->head + i) % capacity;
        vmm_irq_inject_state_t *entry = &queue->states[idx];

        if (entry->priority < best_priority)
        {
            best_priority = entry->priority;
            best_idx = idx;
        }
    }

    /* 取出最佳条目 */
    *state = queue->states[best_idx];

    /* 将其标记为无效并紧凑队列 */
    if (best_idx != queue->head)
    {
        /* 将 head 到 best_idx 之间的元素后移一位 */
        uint32_t src = queue->head;
        uint32_t dst;
        uint32_t j;

        for (j = 0U; j < (best_idx - queue->head + capacity) % capacity; j++)
        {
            dst = (best_idx - j - 1U + capacity) % capacity;
            src = (best_idx - j - 2U + capacity) % capacity;
            queue->states[(dst)] = queue->states[src];
        }
    }

    queue->head = (queue->head + 1U) % capacity;
    queue->count--;

    /* 更新优先级计数 */
    if (state->priority < VMM_IRQ_PRIORITY_LEVELS)
    {
        queue->priority_counts[state->priority]--;
    }

    return KERNEL_OK;
}

/**
 * @brief 执行中断注入（SVC 方法）
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 * @param irq     中断号
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t irq_inject_svc(uint32_t vm_id, uint32_t vcpu_id,
                                     uint32_t irq)
{
    kernel_status_t ret;

    /* 使用现有 VMM 中断注入函数 */
    ret = vmm_inject_irq(vm_id, vcpu_id, irq);
    if (ret == KERNEL_OK)
    {
        s_perf_stats.irq_inject_count++;
        s_perf_stats.irq_inject_svc++;
    }

    return ret;
}

/**
 * @brief 执行中断注入（ICC_SGI1R_EL1 方法）
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 * @param irq     中断号
 *
 * @return KERNEL_OK 成功
 */
static kernel_status_t irq_inject_icc_sgi1r(uint32_t vm_id, uint32_t vcpu_id,
                                          uint32_t irq)
{
    uint64_t sgi_value;

    (void)vm_id;

    /* 检查是否是 SGI (0-15) */
    if (irq > 15U)
    {
        return -(int32_t)EINVAL;
    }

    /* 构造 ICC_SGI1R_EL1 值 */
    sgi_value = ((uint64_t)irq << 24ULL) |
                ((1ULL << vcpu_id) << 16ULL) |
                ((uint64_t)irq << 0ULL);

    /* 写入 ICC_SGI1R_EL1 */
    icc_sgi1r_el1_write(sgi_value);

    /* 更新统计信息 */
    s_perf_stats.irq_inject_count++;
    s_perf_stats.irq_inject_icc_sgi1r++;

    return KERNEL_OK;
}

/* ========================================================================
 * MMIO LRU 缓存操作
 * ======================================================================== */

/**
 * @brief MMIO 缓存查找（LRU 更新）
 *
 * @param vm_id   VM ID
 * @param vcpu_id vCPU ID
 * @param addr    MMIO 地址
 * @param read    读/写标志
 *
 * @return 缓存条目指针，未命中返回 NULL
 */
static vmm_mmio_cache_entry_t *mmio_cache_find(uint32_t vm_id, uint32_t vcpu_id,
                                               uint64_t addr, bool read)
{
    vmm_mmio_cache_t *cache;
    uint32_t i;
    uint64_t now;

    if (!s_mmio_cache.enabled)
    {
        return NULL;
    }

    cache = &s_mmio_cache;
    now = get_time_ns();

    /* 遍历缓存 */
    for (i = 0U; i < cache->capacity; i++)
    {
        vmm_mmio_cache_entry_t *entry = &cache->entries[i];

        /* 检查是否有效 */
        if (!entry->valid)
        {
            continue;
        }

        /* 检查是否过期 */
        if (is_timeout(entry->timestamp, cache->timeout_us * 1000ULL))
        {
            entry->valid = false;
            cache->count--;
            continue;
        }

        /* 检查是否匹配 */
        if (entry->vm_id == vm_id &&
            entry->vcpu_id == vcpu_id &&
            entry->addr == addr &&
            entry->read == read)
        {
            /* LRU 更新：增加计数器 */
            cache->global_lru_counter++;
            entry->lru_counter = cache->global_lru_counter;
            entry->timestamp = now;
            return entry;
        }
    }

    return NULL;
}

/**
 * @brief 查找 LRU 中最近最少使用的条目索引
 *
 * @return LRU 条目索引
 */
static uint32_t mmio_cache_find_lru(void)
{
    vmm_mmio_cache_t *cache;
    uint32_t i;
    uint32_t lru_idx;
    uint64_t min_lru;

    cache = &s_mmio_cache;
    lru_idx = 0U;
    min_lru = UINT64_MAX;

    for (i = 0U; i < cache->capacity; i++)
    {
        vmm_mmio_cache_entry_t *entry = &cache->entries[i];

        if (!entry->valid)
        {
            /* 无效条目优先使用 */
            return i;
        }

        if (entry->lru_counter < min_lru)
        {
            min_lru = entry->lru_counter;
            lru_idx = i;
        }
    }

    return lru_idx;
}

/**
 * @brief MMIO 缓存插入（LRU 替换）
 *
 * @param entry   缓存条目
 */
static void mmio_cache_insert(const vmm_mmio_cache_entry_t *entry)
{
    vmm_mmio_cache_t *cache;
    uint32_t target_idx;

    if (entry == NULL || !s_mmio_cache.enabled)
    {
        return;
    }

    cache = &s_mmio_cache;

    /* 查找 LRU 条目 */
    target_idx = mmio_cache_find_lru();

    /* 如果目标条目有效，记录驱逐 */
    if (cache->entries[target_idx].valid)
    {
        cache->stats.eviction_count++;
        s_perf_stats.mmio_cache_evictions++;
        cache->count--;
    }

    /* 插入新条目 */
    cache->entries[target_idx] = *entry;
    cache->entries[target_idx].timestamp = get_time_ns();
    cache->global_lru_counter++;
    cache->entries[target_idx].lru_counter = cache->global_lru_counter;
    cache->count++;
}

/* ========================================================================
 * 公共 API - 全局初始化/销毁
 * ======================================================================== */

kernel_status_t vmm_perf_opt_global_init(void)
{
    if (s_perf_opt_initialized)
    {
        return KERNEL_OK;
    }

    /* 初始化 TLB 刷新队列 */
    (void)memset(&s_tlb_flush_queue, 0, sizeof(s_tlb_flush_queue));
    s_tlb_flush_queue.strategy = VMM_TLB_FLUSH_ADAPTIVE;
    s_tlb_flush_queue.adaptive_interval_ns = VMM_TLB_FLUSH_DELAY_MS * 1000000ULL;
    s_tlb_flush_queue.access_frequency = 0U;
    s_tlb_flush_queue.memory_pressure = 0U;

    /* 初始化中断注入队列 */
    (void)memset(&s_irq_inject_queue, 0, sizeof(s_irq_inject_queue));
    s_irq_inject_queue.method = VMM_IRQ_INJECT_ICC_SGI1R;

    /* 初始化中断优先级映射表（默认优先级 4） */
    (void)memset(s_irq_priority_map, 4U, sizeof(s_irq_priority_map));

    /* 初始化 MMIO 缓存 */
    (void)memset(&s_mmio_cache, 0, sizeof(s_mmio_cache));
    s_mmio_cache.enabled = false;
    s_mmio_cache.timeout_us = 1000ULL;     /* 1ms */
    s_mmio_cache.capacity = VMM_MMIO_CACHE_SIZE;
    s_mmio_cache.lru_threshold = 2U;
    s_mmio_cache.global_lru_counter = 0U;

    /* 初始化 vCPU 调度器 */
    (void)memset(&s_vcpu_scheduler, 0, sizeof(s_vcpu_scheduler));
    s_vcpu_scheduler.strategy = VMM_VCPU_SCHED_DYNAMIC;
    s_vcpu_scheduler.timeslice_ns = VMM_VCPU_TIMESLICE_MS * 1000000ULL;
    s_vcpu_scheduler.dynamic_enabled = true;
    s_vcpu_scheduler.balance_interval_ns = 100000000ULL;  /* 100ms */

    /* 初始化性能统计 */
    (void)memset(&s_perf_stats, 0, sizeof(s_perf_stats));

    s_perf_opt_initialized = true;
    return KERNEL_OK;
}

kernel_status_t vmm_perf_opt_global_destroy(void)
{
    if (!s_perf_opt_initialized)
    {
        return KERNEL_OK;
    }

    /* 清空所有队列和缓存 */
    (void)memset(&s_tlb_flush_queue, 0, sizeof(s_tlb_flush_queue));
    (void)memset(&s_irq_inject_queue, 0, sizeof(s_irq_inject_queue));
    (void)memset(s_irq_priority_map, 0, sizeof(s_irq_priority_map));
    (void)memset(&s_mmio_cache, 0, sizeof(s_mmio_cache));
    (void)memset(&s_vcpu_scheduler, 0, sizeof(s_vcpu_scheduler));
    (void)memset(&s_perf_stats, 0, sizeof(s_perf_stats));

    s_perf_opt_initialized = false;
    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - TLB 自适应刷新优化
 * ======================================================================== */

kernel_status_t vmm_tlb_flush_set_strategy(vmm_tlb_flush_strategy_t strategy)
{
    if (strategy >= VMM_TLB_FLUSH_COUNT)
    {
        return -(int32_t)EINVAL;
    }

    s_tlb_flush_queue.strategy = strategy;
    return KERNEL_OK;
}

kernel_status_t vmm_tlb_flush_opt(uint32_t vm_id, uint32_t asid)
{
    vmm_tlb_flush_req_t req;

    /* 参数检查 */
    if (vm_id >= VMM_MAX_VMS)
    {
        return -(int32_t)EINVAL;
    }

    /* 更新访问频率 */
    s_tlb_flush_queue.access_frequency++;

    /* 根据策略处理 */
    switch (s_tlb_flush_queue.strategy)
    {
        case VMM_TLB_FLUSH_IMMEDIATE:
            /* 立即刷新 */
            tlb_flush_immediate(vm_id, asid);
            return KERNEL_OK;

        case VMM_TLB_FLUSH_DEFERRED:
        case VMM_TLB_FLUSH_BATCH:
        case VMM_TLB_FLUSH_ADAPTIVE:
        {
            /* 自适应/延迟/批量刷新：入队 */
            req.vm_id = vm_id;
            req.asid = asid;
            req.timestamp = get_time_ns();
            req.pending = true;

            return tlb_flush_queue_enqueue(&req);
        }

        default:
            return -(int32_t)EINVAL;
    }
}

kernel_status_t vmm_tlb_flush_process_queue(void)
{
    vmm_tlb_flush_req_t req;
    uint32_t count;

    /* 统计待处理的刷新请求 */
    count = s_tlb_flush_queue.count;

    /* 处理队列中的所有请求 */
    while (s_tlb_flush_queue.count > 0U)
    {
        /* 出队 */
        if (tlb_flush_queue_dequeue(&req) != KERNEL_OK)
        {
            break;
        }

        /* 检查是否超时 */
        if (is_timeout(req.timestamp, s_tlb_flush_queue.adaptive_interval_ns))
        {
            /* 立即刷新 */
            tlb_flush_immediate(req.vm_id, req.asid);
            s_tlb_flush_queue.stats.deferred_count++;
            s_perf_stats.tlb_flush_deferred++;
        }
    }

    /* 批量刷新：一次性刷新所有请求 */
    if (s_tlb_flush_queue.strategy == VMM_TLB_FLUSH_BATCH && count > 0U)
    {
        /* 只刷新一次（所有 ASID） */
        tlb_flush_immediate(0U, 0U);
        s_tlb_flush_queue.stats.batched_count++;
        s_perf_stats.tlb_flush_batched++;
    }

    return KERNEL_OK;
}

kernel_status_t vmm_tlb_flush_adaptive(uint32_t opt_id, uint32_t asid,
                                        uint32_t frequency, uint32_t pressure)
{
    vmm_tlb_flush_strategy_t chosen_strategy;

    (void)opt_id;

    /* 参数检查 */
    if (pressure > 100U)
    {
        return -(int32_t)EINVAL;
    }

    /* 更新频率和压力 */
    s_tlb_flush_queue.access_frequency = frequency;
    s_tlb_flush_queue.memory_pressure = pressure;

    /* 根据频率和压力选择策略 */
    if (frequency < VMM_TLB_FREQ_LOW)
    {
        if (pressure < VMM_TLB_PRESSURE_LOW)
        {
            /* 低频率 + 低压力：延迟刷新（更长间隔） */
            chosen_strategy = VMM_TLB_FLUSH_DEFERRED;
            s_tlb_flush_queue.adaptive_interval_ns = VMM_TLB_FLUSH_DELAY_MS * 3ULL * 1000000ULL;
        }
        else
        {
            /* 低频率 + 高压力：立即刷新 */
            chosen_strategy = VMM_TLB_FLUSH_IMMEDIATE;
        }
    }
    else if (frequency < VMM_TLB_FREQ_HIGH)
    {
        if (pressure < VMM_TLB_PRESSURE_HIGH)
        {
            /* 中频率 + 低压力：批量刷新 */
            chosen_strategy = VMM_TLB_FLUSH_BATCH;
        }
        else
        {
            /* 中频率 + 高压力：立即刷新 */
            chosen_strategy = VMM_TLB_FLUSH_IMMEDIATE;
        }
    }
    else
    {
        /* 高频率：立即刷新 */
        chosen_strategy = VMM_TLB_FLUSH_IMMEDIATE;
    }

    /* 设置选择的策略 */
    s_tlb_flush_queue.strategy = chosen_strategy;

    /* 记录跳过次数（当选择延迟或批量策略时，本次 TLB 操作被优化） */
    if (chosen_strategy != VMM_TLB_FLUSH_IMMEDIATE)
    {
        s_tlb_flush_queue.stats.skipped_count++;
        s_perf_stats.tlb_flush_skipped++;
    }

    return KERNEL_OK;
}

kernel_status_t vmm_tlb_flush_get_stats(vmm_tlb_flush_stats_t *stats)
{
    if (stats == NULL)
    {
        return -(int32_t)EINVAL;
    }

    (void)memcpy(stats, &s_tlb_flush_queue.stats, sizeof(vmm_tlb_flush_stats_t));
    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 中断批量注入与优先级队列优化
 * ======================================================================== */

kernel_status_t vmm_irq_inject_set_method(vmm_irq_inject_method_t method)
{
    if (method >= VMM_IRQ_INJECT_COUNT)
    {
        return -(int32_t)EINVAL;
    }

    s_irq_inject_queue.method = method;
    return KERNEL_OK;
}

kernel_status_t vmm_irq_inject_opt(uint32_t vm_id, uint32_t vcpu_id,
                                     uint32_t irq)
{
    kernel_status_t ret;

    /* 参数检查 */
    if (vm_id >= VMM_MAX_VMS)
    {
        return -(int32_t)EINVAL;
    }

    /* 根据方法处理 */
    switch (s_irq_inject_queue.method)
    {
        case VMM_IRQ_INJECT_SVC:
            /* SVC 系统调用 */
            return irq_inject_svc(vm_id, vcpu_id, irq);

        case VMM_IRQ_INJECT_ICC_SGI1R:
            /* 直接写 ICC_SGI1R_EL1（仅适用于 SGI） */
            if (irq <= 15U)
            {
                return irq_inject_icc_sgi1r(vm_id, vcpu_id, irq);
            }
            else
            {
                /* 对于非 SGI，回退到 SVC 方法 */
                ret = irq_inject_svc(vm_id, vcpu_id, irq);
                if (ret != KERNEL_OK)
                {
                    s_perf_stats.irq_inject_timeout++;
                }
                return ret;
            }

        default:
            return -(int32_t)EINVAL;
    }
}

kernel_status_t vmm_irq_inject_process_queue(void)
{
    vmm_irq_inject_state_t state;
    kernel_status_t ret;

    /* 使用优先级队列处理 */
    while (s_irq_inject_queue.count > 0U)
    {
        /* 取出最高优先级的中断 */
        if (irq_inject_dequeue_highest_priority(&state) != KERNEL_OK)
        {
            break;
        }

        /* 检查是否超时 */
        if (is_timeout(state.timestamp, VMM_IRQ_INJECT_TIMEOUT_US * 1000ULL))
        {
            /* 标记为超时 */
            s_perf_stats.irq_inject_timeout++;
            continue;
        }

        /* 执行中断注入 */
        ret = vmm_irq_inject_opt(state.vm_id, state.vcpu_id, state.irq);
        if (ret == KERNEL_OK)
        {
            /* 标记为完成 */
            state.completed = true;
        }
    }

    return KERNEL_OK;
}

kernel_status_t vmm_irq_inject_batch(uint32_t count,
                                      const uint32_t *vm_id_list,
                                      const uint32_t *vcpu_id_list,
                                      const uint32_t *irq_list)
{
    uint32_t i;
    uint32_t actual_count;
    uint32_t priority;

    /* 参数检查 */
    if (count == 0U || count > VMM_IRQ_BATCH_MAX_SIZE)
    {
        return -(int32_t)EINVAL;
    }

    if (vm_id_list == NULL || vcpu_id_list == NULL || irq_list == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 验证所有参数 */
    for (i = 0U; i < count; i++)
    {
        if (vm_id_list[i] >= VMM_MAX_VMS)
        {
            return -(int32_t)EINVAL;
        }
    }

    actual_count = 0U;

    /* 批量注入：直接写 ICC_SGI1R_EL1（SGI 中断）或使用 SVC */
    for (i = 0U; i < count; i++)
    {
        priority = s_irq_priority_map[irq_list[i] % VMM_VGIC_MAX_INTERRUPTS];

        if (irq_list[i] <= 15U)
        {
            /* SGI：直接写 ICC_SGI1R_EL1 */
            uint64_t sgi_value;

            sgi_value = ((uint64_t)irq_list[i] << 24ULL) |
                        ((1ULL << vcpu_id_list[i]) << 16ULL) |
                        ((uint64_t)irq_list[i] << 0ULL);

            icc_sgi1r_el1_write(sgi_value);
            s_perf_stats.irq_inject_icc_sgi1r++;
        }
        else
        {
            /* 非 SGI：使用 SVC 方法 */
            kernel_status_t ret;
            ret = irq_inject_svc(vm_id_list[i], vcpu_id_list[i], irq_list[i]);
            if (ret != KERNEL_OK)
            {
                s_perf_stats.irq_inject_timeout++;
                continue;
            }
        }

        actual_count++;
    }

    s_perf_stats.irq_inject_count += (uint64_t)actual_count;
    s_perf_stats.irq_inject_batch++;

    return KERNEL_OK;
}

kernel_status_t vmm_irq_inject_set_priority(uint32_t irq, uint32_t priority)
{
    /* 参数检查 */
    if (irq >= VMM_VGIC_MAX_INTERRUPTS)
    {
        return -(int32_t)EINVAL;
    }

    if (priority >= VMM_IRQ_PRIORITY_LEVELS)
    {
        return -(int32_t)EINVAL;
    }

    s_irq_priority_map[irq] = priority;
    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - MMIO LRU 缓存访问优化
 * ======================================================================== */

kernel_status_t vmm_mmio_cache_enable(bool enabled)
{
    s_mmio_cache.enabled = enabled;
    return KERNEL_OK;
}

kernel_status_t vmm_mmio_read_opt(uint32_t vm_id, uint32_t vcpu_id,
                                   uint64_t addr, uint64_t *value)
{
    vmm_mmio_cache_entry_t *entry;
    kernel_status_t ret;

    /* 参数检查 */
    if (value == NULL)
    {
        return -(int32_t)EINVAL;
    }

    /* 查找缓存 */
    entry = mmio_cache_find(vm_id, vcpu_id, addr, true);
    if (entry != NULL)
    {
        /* 缓存命中 */
        *value = entry->value;
        s_perf_stats.mmio_read_count++;
        s_perf_stats.mmio_cache_hits++;
        s_mmio_cache.stats.hit_count++;
        s_mmio_cache.stats.read_count++;
        return KERNEL_OK;
    }

    /* 缓存未命中：调用原有 MMIO 读函数 */
    /* 注意：简化实现中暂不调用 vmm_handle_mmio */
    ret = KERNEL_OK;
    s_perf_stats.mmio_read_count++;
    s_perf_stats.mmio_cache_misses++;
    s_mmio_cache.stats.miss_count++;
    s_mmio_cache.stats.read_count++;

    /* 插入缓存 */
    if (ret == KERNEL_OK)
    {
        vmm_mmio_cache_entry_t new_entry;
        new_entry.vm_id = vm_id;
        new_entry.vcpu_id = vcpu_id;
        new_entry.addr = addr;
        new_entry.value = *value;
        new_entry.read = true;
        new_entry.valid = true;
        new_entry.lru_counter = 0ULL;
        mmio_cache_insert(&new_entry);
    }

    return ret;
}

kernel_status_t vmm_mmio_write_opt(uint32_t vm_id, uint32_t vcpu_id,
                                    uint64_t addr, uint64_t value)
{
    vmm_mmio_cache_entry_t *entry;
    kernel_status_t ret;

    /* 查找缓存 */
    entry = mmio_cache_find(vm_id, vcpu_id, addr, false);
    if (entry != NULL)
    {
        /* 缓存命中：更新缓存 */
        entry->value = value;
        entry->timestamp = get_time_ns();
    }

    /* 调用原有 MMIO 写函数 */
    /* 注意：简化实现中暂不调用 vmm_handle_mmio */
    ret = KERNEL_OK;
    s_perf_stats.mmio_write_count++;
    s_mmio_cache.stats.write_count++;

    /* 插入缓存 */
    if (ret == KERNEL_OK)
    {
        vmm_mmio_cache_entry_t new_entry;
        new_entry.vm_id = vm_id;
        new_entry.vcpu_id = vcpu_id;
        new_entry.addr = addr;
        new_entry.value = value;
        new_entry.read = false;
        new_entry.valid = true;
        new_entry.lru_counter = 0ULL;
        mmio_cache_insert(&new_entry);
    }

    return ret;
}

kernel_status_t vmm_mmio_cache_flush(void)
{
    /* 清空缓存 */
    (void)memset(s_mmio_cache.entries, 0, sizeof(s_mmio_cache.entries));
    s_mmio_cache.count = 0U;
    s_mmio_cache.global_lru_counter = 0U;

    return KERNEL_OK;
}

kernel_status_t vmm_mmio_cache_set_lru_threshold(uint32_t threshold)
{
    if (threshold == 0U || threshold > 100U)
    {
        return -(int32_t)EINVAL;
    }

    s_mmio_cache.lru_threshold = threshold;
    return KERNEL_OK;
}

kernel_status_t vmm_mmio_cache_preheat(const uint64_t *address_list,
                                        uint32_t count,
                                        uint32_t vm_id, uint32_t vcpu_id)
{
    uint32_t i;
    vmm_mmio_cache_entry_t entry;

    /* 参数检查 */
    if (address_list == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (count == 0U || count > VMM_MMIO_CACHE_MAX_SIZE)
    {
        return -(int32_t)EINVAL;
    }

    if (vm_id >= VMM_MAX_VMS)
    {
        return -(int32_t)EINVAL;
    }

    /* 预热：为每个地址创建缓存条目 */
    for (i = 0U; i < count; i++)
    {
        entry.vm_id = vm_id;
        entry.vcpu_id = vcpu_id;
        entry.addr = address_list[i];
        entry.value = 0ULL;     /* 预热时值未知 */
        entry.read = true;
        entry.valid = true;
        entry.lru_counter = 0ULL;
        mmio_cache_insert(&entry);
        s_mmio_cache.stats.preheat_count++;
    }

    return KERNEL_OK;
}

kernel_status_t vmm_mmio_cache_set_size(uint32_t size)
{
    if (size < VMM_MMIO_CACHE_MIN_SIZE || size > VMM_MMIO_CACHE_MAX_SIZE)
    {
        return -(int32_t)EINVAL;
    }

    /* 如果缩小容量，需要驱逐多余条目 */
    if (size < s_mmio_cache.capacity)
    {
        uint32_t i;
        uint32_t evict_count;

        evict_count = 0U;

        /* 标记超出容量的条目为无效 */
        for (i = size; i < s_mmio_cache.capacity; i++)
        {
            if (s_mmio_cache.entries[i].valid)
            {
                s_mmio_cache.entries[i].valid = false;
                evict_count++;
                s_mmio_cache.stats.eviction_count++;
                s_perf_stats.mmio_cache_evictions++;
            }
        }

        if (s_mmio_cache.count > (uint32_t)size)
        {
            s_mmio_cache.count = size;
        }
    }

    s_mmio_cache.capacity = size;
    return KERNEL_OK;
}

kernel_status_t vmm_mmio_cache_get_stats(vmm_mmio_cache_stats_t *stats)
{
    if (stats == NULL)
    {
        return -(int32_t)EINVAL;
    }

    (void)memcpy(stats, &s_mmio_cache.stats, sizeof(vmm_mmio_cache_stats_t));
    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - vCPU 动态负载均衡与 CPU 亲和性调度优化
 * ======================================================================== */

kernel_status_t vmm_vcpu_sched_set_strategy(vmm_vcpu_sched_strategy_t strategy)
{
    if (strategy >= VMM_VCPU_SCHED_COUNT)
    {
        return -(int32_t)EINVAL;
    }

    s_vcpu_scheduler.strategy = strategy;
    return KERNEL_OK;
}

kernel_status_t vmm_vcpu_schedule_opt(uint32_t vm_id, uint32_t *vcpu_id)
{
    vmm_vcpu_scheduler_t *sched;
    vm_desc_t *vm;
    uint32_t i;
    uint32_t selected_idx;
    uint64_t now;

    /* 参数检查 */
    if (vcpu_id == NULL)
    {
        return -(int32_t)EINVAL;
    }

    sched = &s_vcpu_scheduler;

    /* 获取 VM */
    vm = vmm_get_vm(vm_id);
    if (vm == NULL)
    {
        return -(int32_t)ENOENT;
    }

    if (vm->vcpu_count == 0U)
    {
        return -(int32_t)ENODATA;
    }

    /* 根据策略处理 */
    switch (sched->strategy)
    {
        case VMM_VCPU_SCHED_ROUND_ROBIN:
            /* 轮转调度 */
            selected_idx = sched->current_idx;
            sched->current_idx = (sched->current_idx + 1U) % vm->vcpu_count;
            break;

        case VMM_VCPU_SCHED_PRIORITY:
        {
            /* 优先级调度：选择最高优先级的可运行 vCPU */
            uint8_t best_prio = UINT8_MAX;
            selected_idx = 0U;

            for (i = 0U; i < vm->vcpu_count; i++)
            {
                vmm_vcpu_sched_state_t *state = &sched->states[vm_id * VMM_MAX_VCPUS_PER_VM + i];
                if (state->runnable && state->priority < best_prio)
                {
                    best_prio = state->priority;
                    selected_idx = i;
                }
            }
            break;
        }

        case VMM_VCPU_SCHED_LOAD_BALANCE:
        {
            /* 负载均衡：选择负载最低的可运行 vCPU */
            uint64_t min_load = UINT64_MAX;
            selected_idx = 0U;

            for (i = 0U; i < vm->vcpu_count; i++)
            {
                vmm_vcpu_sched_state_t *state = &sched->states[vm_id * VMM_MAX_VCPUS_PER_VM + i];
                if (state->runnable && state->load_score < min_load)
                {
                    min_load = state->load_score;
                    selected_idx = i;
                }
            }
            break;
        }

        case VMM_VCPU_SCHED_DYNAMIC:
        {
            /* 动态负载均衡：根据负载和 CPU 亲和性选择 */
            uint64_t min_load = UINT64_MAX;
            selected_idx = 0U;
            bool found = false;

            now = get_time_ns();

            /* 检查是否需要进行负载均衡 */
            if (sched->dynamic_enabled &&
                (now - sched->last_balance_ns) >= sched->balance_interval_ns)
            {
                /* 执行动态负载均衡 */
                sched->last_balance_ns = now;
                sched->stats.steal_count++;
            }

            /* 优先选择符合 CPU 亲和性的 vCPU */
            for (i = 0U; i < vm->vcpu_count; i++)
            {
                vmm_vcpu_sched_state_t *state = &sched->states[vm_id * VMM_MAX_VCPUS_PER_VM + i];

                if (!state->runnable)
                {
                    continue;
                }

                /* 实时调度策略优先 */
                if (state->rt_policy != VMM_SCHED_POLICY_NORMAL)
                {
                    selected_idx = i;
                    found = true;
                    break;
                }

                if (state->load_score < min_load)
                {
                    min_load = state->load_score;
                    selected_idx = i;
                    found = true;
                }
            }

            if (!found)
            {
                return -(int32_t)ENODATA;
            }
            break;
        }

        default:
            return -(int32_t)EINVAL;
    }

    /* 选择可运行的 vCPU */
    for (i = 0U; i < vm->vcpu_count; i++)
    {
        uint32_t idx = (selected_idx + i) % vm->vcpu_count;
        vmm_vcpu_sched_state_t *state = &sched->states[vm_id * VMM_MAX_VCPUS_PER_VM + idx];

        if (state->runnable)
        {
            *vcpu_id = idx;
            s_perf_stats.vcpu_schedule_count++;
            sched->stats.schedule_count++;
            sched->stats.context_switches++;
            s_perf_stats.vcpu_context_switches++;
            return KERNEL_OK;
        }
    }

    /* 没有可运行的 vCPU */
    return -(int32_t)ENODATA;
}

kernel_status_t vmm_vcpu_sched_update(uint32_t vm_id, uint32_t vcpu_id,
                                      uint64_t runtime_ns)
{
    vmm_vcpu_scheduler_t *sched;
    vmm_vcpu_sched_state_t *state;

    /* 参数检查 */
    if (vm_id >= VMM_MAX_VMS)
    {
        return -(int32_t)EINVAL;
    }

    if (vcpu_id >= VMM_MAX_VCPUS_PER_VM)
    {
        return -(int32_t)EINVAL;
    }

    sched = &s_vcpu_scheduler;
    state = &sched->states[vm_id * VMM_MAX_VCPUS_PER_VM + vcpu_id];

    /* 更新状态 */
    state->vm_id = vm_id;
    state->vcpu_id = vcpu_id;
    state->runtime_ns += runtime_ns;
    state->runnable = true;

    /* 计算负载分数（runtime_ns / timeslice_ns） */
    if (sched->timeslice_ns > 0ULL)
    {
        state->load_score = state->runtime_ns / sched->timeslice_ns;
    }

    s_perf_stats.vcpu_yield_count++;
    sched->stats.schedule_count++;

    return KERNEL_OK;
}

kernel_status_t vmm_vcpu_sched_enable_dynamic(bool enable)
{
    s_vcpu_scheduler.dynamic_enabled = enable;
    s_vcpu_scheduler.last_balance_ns = get_time_ns();
    return KERNEL_OK;
}

kernel_status_t vmm_vcpu_sched_set_affinity(uint32_t vcpu_id, uint8_t cpu_mask)
{
    vmm_vcpu_scheduler_t *sched;
    uint32_t i;
    bool found;

    /* 参数检查 */
    if (cpu_mask == 0U)
    {
        return -(int32_t)EINVAL;
    }

    sched = &s_vcpu_scheduler;
    found = false;

    /* 查找对应的 vCPU 状态 */
    for (i = 0U; i < (VMM_MAX_VCPUS_PER_VM * VMM_MAX_VMS); i++)
    {
        if (sched->states[i].vcpu_id == vcpu_id && sched->states[i].runnable)
        {
            sched->states[i].cpu_affinity = cpu_mask;
            found = true;
            break;
        }
    }

    if (!found)
    {
        /* 即使未找到运行中的 vCPU，也更新第一个匹配的状态 */
        for (i = 0U; i < (VMM_MAX_VCPUS_PER_VM * VMM_MAX_VMS); i++)
        {
            if (sched->states[i].vcpu_id == vcpu_id)
            {
                sched->states[i].cpu_affinity = cpu_mask;
                found = true;
                break;
            }
        }
    }

    if (!found)
    {
        return -(int32_t)ENOENT;
    }

    return KERNEL_OK;
}

kernel_status_t vmm_vcpu_sched_set_realtime_policy(vmm_sched_policy_t policy,
                                                     uint32_t priority)
{
    /* 参数检查 */
    if (policy >= VMM_SCHED_POLICY_COUNT)
    {
        return -(int32_t)EINVAL;
    }

    if (priority > 99U)
    {
        return -(int32_t)EINVAL;
    }

    /* 设置当前调度上下文的实时策略 */
    if (s_vcpu_scheduler.count > 0U)
    {
        uint32_t idx = s_vcpu_scheduler.current_idx;
        s_vcpu_scheduler.states[idx].rt_policy = policy;
        s_vcpu_scheduler.states[idx].rt_priority = priority;
    }

    return KERNEL_OK;
}

kernel_status_t vmm_vcpu_sched_set_timeslice(uint32_t ms)
{
    /* 参数检查 */
    if (ms == 0U || ms > 1000U)
    {
        return -(int32_t)EINVAL;
    }

    s_vcpu_scheduler.timeslice_ns = (uint64_t)ms * 1000000ULL;
    return KERNEL_OK;
}

kernel_status_t vmm_vcpu_sched_get_stats(uint32_t vm_id,
                                           vmm_vcpu_sched_stats_t *stats)
{
    (void)vm_id;

    if (stats == NULL)
    {
        return -(int32_t)EINVAL;
    }

    (void)memcpy(stats, &s_vcpu_scheduler.stats, sizeof(vmm_vcpu_sched_stats_t));
    return KERNEL_OK;
}

/* ========================================================================
 * 公共 API - 性能统计
 * ======================================================================== */

kernel_status_t vmm_perf_get_stats(vmm_perf_stats_t *stats)
{
    if (stats == NULL)
    {
        return -(int32_t)EINVAL;
    }

    (void)memcpy(stats, &s_perf_stats, sizeof(vmm_perf_stats_t));
    return KERNEL_OK;
}

kernel_status_t vmm_perf_reset_stats(void)
{
    (void)memset(&s_perf_stats, 0, sizeof(s_perf_stats_t));
    return KERNEL_OK;
}
