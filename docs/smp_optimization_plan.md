# SMP 优化 - 分片锁、缓存一致性、IPI 优化方案

## 优化目标

1. **锁竞争优化（分片锁 + RCU）**
   - 将全局锁改为分片锁，减少锁竞争
   - 添加 RCU（Read-Copy-Update）支持，提高读操作性能
   - 优化锁的粒度，减少临界区

2. **缓存一致性优化**
   - 添加缓存一致性协议（MESI）
   - 优化缓存一致性维护时机
   - 添加缓存一致性指令的合理使用

3. **IPI 优化**
   - 优化 IPI 发送和接收机制
   - 添加 IPI Coalescing（批处理），减少 IPI 数量
   - 优化 IPI 延迟和吞吐量

---

## 1. 锁竞争优化（分片锁 + RCU）

### 1.1 分片锁设计

**场景**：当前某些数据结构使用全局锁（如 s_vma_pool_lock），在多核环境下容易成为性能瓶颈。

**解决方案**：将全局锁改为分片锁，每个分片独立管理一部分数据，减少锁竞争。

**分片锁接口**：
```c
typedef struct {
    uint32_t lock_num;           /* 分片锁数量 */
    TicketLock_t *shards;       /* 分片锁数组 */
} ShardedLock_t;

/* 初始化分片锁 */
void sharded_lock_init(ShardedLock_t *slock, uint32_t num_shards);

/* 销毁分片锁 */
void sharded_lock_destroy(ShardedLock_t *slock);

/* 根据键值选择分片 */
uint32_t sharded_lock_select(const ShardedLock_t *slock, uint64_t key);

/* 获取指定分片的锁 */
void sharded_lock_acquire(ShardedLock_t *slock, uint32_t shard_id);

/* 释放指定分片的锁 */
void sharded_lock_release(ShardedLock_t *slock, uint32_t shard_id);
```

**应用场景**：
- VMA 池锁：分片锁管理，每个分片管理一部分 VMA
- 能力表锁：分片锁管理，每个分片管理一部分能力
- 线程池锁：分片锁管理，每个分片管理一部分线程

### 1.2 RCU（Read-Copy-Update）设计

**场景**：当前读操作也需要获取锁，在多读少写场景下性能不佳。

**解决方案**：添加 RCU 机制，读操作无锁访问，写操作通过 Grace Period 机制回收旧版本。

**RCU 接口**：
```c
typedef struct {
    volatile uint32_t epoch;     /* 当前 epoch */
    void *data;                    /* 数据指针 */
    uint32_t readers[CONFIG_MAX_CPUS]; /* 每个 CPU 的读者 epoch */
} rcu_data_t;

/* RCU 读侧：注册读者 */
void rcu_read_lock(void);

/* RCU 读侧：注销读者 */
void rcu_read_unlock(void);

/* RCU 写侧：开始更新 */
void rcu_write_lock(void);

/* RCU 写侧：完成更新 */
void rcu_write_unlock(void);

/* RCU 写侧：同步所有读者 */
void rcu_synchronize(void);
```

**应用场景**：
- 调度器队列：读操作频繁，写操作稀少
- VMA 查找：读操作频繁，修改稀少
- 能力系统：读操作频繁，修改稀少

---

## 2. 缓存一致性优化

### 2.1 缓存一致性协议（MESI）

**场景**：当前没有明确的缓存一致性协议，可能导致数据不一致。

**解决方案**：实现 MESI（Modified, Exclusive, Shared, Invalid）缓存一致性协议，确保多核环境下的数据一致性。

**MESI 接口**：
```c
/* 缓存行状态 */
typedef enum {
    CACHE_STATE_MODIFIED,
    CACHE_STATE_EXCLUSIVE,
    CACHE_STATE_SHARED,
    CACHE_STATE_INVALID
} cache_state_t;

/* 缓存一致性操作 */
void cache_coherence_sync_before_write(void *addr, uint64_t size);
void cache_coherence_sync_after_write(void *addr, uint64_t size);
void cache_coherence_invalidate(void *addr, uint64_t size);
```

### 2.2 缓存一致性指令优化

**场景**：当前缓存一致性指令的使用可能过于频繁或不充分。

**解决方案**：优化缓存一致性指令的使用时机，在必要时才使用，减少性能开销。

**优化策略**：
```c
/* 写前缓存同步：仅用于跨核共享数据 */
#define COHERENCE_WRITE_BEFORE(addr, size) \
    do { \
        if (is_shared_data(addr)) { \
            hal_dcache_clean((uint64_t)(addr), (size)); \
        } \
    } while (0)

/* 写后缓存同步：仅用于跨核共享数据 */
#define COHERENCE_WRITE_AFTER(addr, size) \
    do { \
        if (is_shared_data(addr)) { \
            hal_dcache_clean_and_invalidate((uint64_t)(addr), (size)); \
        } \
    } while (0)

/* 读取缓存同步：不需要 */
#define COHERENCE_READ(addr, size) \
    do { \
        /* 硬件自动处理 MESI 协议 */ \
    } while (0)
```

### 2.3 TLB 一致性优化

**场景**：当前 TLB 刷新可能过于频繁，导致性能下降。

**解决方案**：使用 ASID（Address Space ID）减少 TLB 刷新，只在必要时刷新 TLB。

**优化策略**：
```c
/* 地址空间切换时刷新 TLB */
void tlb_flush_on_asid_switch(asid_t old_asid, asid_t new_asid)
{
    /* 只刷新与旧 ASID 相关的 TLB 条目 */
    hal_tlb_invalidate_asid(old_asid);
    
    /* 使用 ISB 指令确保刷新完成 */
    hal_isb();
}

/* 映射修改时选择性刷新 TLB */
void tlb_flush_on_mapping_change(void)
{
    /* 只刷新相关的页表条目 */
    /* 不需要全局 TLB 刷新 */
}
```

---

## 3. IPI 优化

### 3.1 IPI Coalescing（批处理）

**场景**：当前每次 IPI 都立即发送，可能导致 IPI 洪水，降低性能。

**解决方案**：实现 IPI Coalescing，将多个 IPI 合并为一次发送，减少 IPI 数量和开销。

**IPI Coalescing 接口**：
```c
typedef struct {
    volatile uint32_t pending[CONFIG_MAX_CPUS]; /* 待处理 IPI 位图 */
    volatile uint32_t coalesce_timeout;         /* Coalesce 超时 */
    uint32_t coalesce_interval;                 /* Coalesce 间隔 */
} ipi_coalesce_t;

/* IPI Coalescing 初始化 */
void ipi_coalesce_init(void);

/* IPI 发送（带 Coalescing） */
void ipi_send_coalesced(uint32_t target_cpu, ipi_type_t type);

/* IPI Coalescing 定时器处理 */
void ipi_coalesce_timer_handler(void);

/* IPI Coalescing 立即发送 */
void ipi_send_immediate(uint32_t target_cpu, ipi_type_t type);
```

### 3.2 IPI 延迟优化

**场景**：当前 IPI 延迟可能较高，影响 SMP 性能。

**解决方案**：优化 IPI 发送和接收路径，减少 IPI 延迟。

**优化策略**：
```c
/* 优化 IPI 发送路径 */
void ipi_send_optimized(uint32_t target_cpu, ipi_type_t type)
{
    /* 直接写入 GIC 寄存器，减少中间层 */
    gic_sgi_send(target_cpu, type);
    
    /* 使用 DSB 确保写入完成 */
    hal_dsb_sy();
}

/* 优化 IPI 接收路径 */
void ipi_recv_optimized(uint32_t source_cpu, ipi_type_t type)
{
    /* 快速路径：直接调用处理函数 */
    ipi_handler_t handler = s_ipi_handlers[type];
    if (handler != NULL)
    {
        handler(s_ipi_args[type]);
    }
}
```

---

## 4. 实现优先级

### P0 - 高优先级（立即实现）

1. **分片锁实现**
   - 实现分片锁数据结构
   - 实现 VMA 池分片锁
   - 实现能力表分片锁

2. **IPI Coalescing**
   - 实现 IPI Coalescing 机制
   - 优化 IPI 发送路径
   - 添加 IPI Coalescing 定时器

3. **缓存一致性指令优化**
   - 优化当前缓存一致性指令的使用
   - 添加数据共享检测
   - 减少不必要的缓存操作

### P1 - 中优先级（近期实现）

1. **RCU 机制**
   - 实现 RCU 数据结构
   - 实现 RCU 读侧 API
   - 实现 RCU 写侧 API
   - 应用到调度器队列

2. **TLB 一致性优化**
   - 优化地址空间切换
   - 优化映射修改时的 TLB 刷新
   - 减少 TLB 刷新次数

3. **IPI 延迟优化**
   - 优化 IPI 接收路径
   - 优化 IPI 处理函数
   - 减少中断处理延迟

### P2 - 低优先级（长期优化）

1. **MESI 缓存一致性协议**
   - 实现 MESI 状态机
   - 实现缓存一致性操作
   - 实现缓存一致性验证

2. **高级 IPI 优化**
   - 实现 IPI 优先级
   - 实现 IPI 批处理
   - 实现 IPI 统计和监控

---

## 5. 性能指标

### 5.1 锁竞争优化指标

| 指标 | 优化前 | 目标 | 测量方法 |
|------|--------|------|-----------|
| 锁竞争率 | 高 | 低 | 统计锁等待时间 |
| 锁吞吐量 | 低 | 高 | 统计锁获取/释放次数 |
| 锁公平性 | 公平 | 公平 | Ticket Lock 保证 |
| 锁扩展性 | 低 | 高 | 分片锁动态调整 |

### 5.2 缓存一致性优化指标

| 指标 | 优化前 | 目标 | 测量方法 |
|------|--------|------|-----------|
| 缓存一致性开销 | 高 | 低 | 统计缓存指令执行次数 |
| 数据一致性概率 | 中 | 高 | 测试多核并发访问 |
| 缓存命中率 | 中 | 高 | 使用性能计数器 |
| TLB 刷新次数 | 高 | 低 | 统计 TLB 刷新调用次数 |

### 5.3 IPI 优化指标

| 指标 | 优化前 | 目标 | 测量方法 |
|------|--------|------|-----------|
| IPI 发送延迟 | 高 | 低 | 使用时间戳测量 |
| IPI 吞吐量 | 低 | 高 | 统计 IPI 发送次数 |
| IPI 洪水 | 高 | 低 | 统计 IPI 累积次数 |
| IPI Coalescing 效率 | 无 | 高 | 统计 Coalescing 成功率 |

---

## 6. 测试验证

### 6.1 功能测试

- 分片锁正确性测试
- RCU 机制正确性测试
- IPI Coalescing 正确性测试
- 缓存一致性正确性测试

### 6.2 性能测试

- SMP 性能基准测试（多核并发）
- 缓存一致性性能测试
- IPI 延迟和吞吐量测试
- 负载均衡性能测试

### 6.3 稳定性测试

- 长时间 SMP 稳定性测试
- 高并发压力测试
- 异常情况处理测试
- 死锁和活锁检测

---

## 7. 风险评估

### 7.1 技术风险

- **复杂性风险**：分片锁和 RCU 机制增加系统复杂性
- **正确性风险**：缓存一致性协议容易出错，需要充分测试
- **性能风险**：优化可能带来意外的性能下降

### 7.2 缓解措施

- **渐进式实现**：分阶段实现，逐步验证
- **充分测试**：编写完整的测试用例，覆盖所有场景
- **性能监控**：添加性能监控，及时发现性能问题
- **回退机制**：保留原有实现，必要时可以回退

---

## 8. 参考资料

- 《多核编程的艺术》- 锁竞争优化
- 《Linux 内核 SMP 机制》- RCU 和 IPI 优化
- 《ARMv8-A 架构手册》- 缓存一致性协议
- 《操作系统概念》- MESI 协议
- 《现代操作系统》- 多核调度和同步
