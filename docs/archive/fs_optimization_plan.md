# 文件系统性能优化实施计划

**日期**: 2026-05-08
**目标**: 将文件系统性能提升 3-8 倍
**预计时间**: 15-22 天
**负责人**: AISafe64 编程助手

---

## 1. 优化路线图

```
Phase 1: 索引优化 (Week 1-2)
   ↓
Phase 2: 缓存机制 (Week 3-5)
   ↓
Phase 3: 锁优化 (Week 6-7)
   ↓
Phase 4: 内存访问优化 (Week 8-9)
   ↓
Phase 5: Inode 管理优化 (Week 10-11)
   ↓
验收测试 (Week 12-13)
```

---

## 2. Phase 1: 索引优化 (Week 1-2)

### 2.1 任务清单

- [ ] **Task 1.1**: 实现 ramfs 文件名哈希索引
  - 创建 `ramfs_hash.c` + `ramfs_hash.h`
  - 实现 `ramfs_hash_init()`, `ramfs_hash_add()`, `ramfs_hash_find()`
  - 集成到 `ramfs_lookup()`, `ramfs_create()`, `ramfs_unlink()`
  - 测试: 1000 次文件查找性能测试

- [ ] **Task 1.2**: 实现 file lock 哈希索引
  - 创建 `fs_lock_hash.c` + `fs_lock_hash.h`
  - 实现 `fs_lock_hash_init()`, `fs_lock_hash_add()`, `fs_lock_hash_find()`
  - 集成到 `fs_flock()`, `fs_lock_cleanup()`
  - 测试: 多线程锁竞争性能测试

- [ ] **Task 1.3**: 实现 mount point 哈希索引
  - 创建 `fs_mount_hash.c` + `fs_mount_hash.h`
  - 实现 `fs_mount_hash_init()`, `fs_mount_hash_add()`, `fs_mount_hash_find()`
  - 集成到 `fs_mount()`, `fs_unmount()`, `find_mount_for_path()`
  - 测试: 1000 次路径解析性能测试

### 2.2 技术方案

#### ramfs 文件名哈希索引

```c
/* ramfs_hash.h */
#define RAMFS_HASH_SIZE  128U

typedef struct
{
    char            name[64];
    uint32_t        ino;
    ramfs_file_t   *file;
} ramfs_name_entry_t;

typedef struct
{
    ramfs_name_entry_t entries[RAMFS_HASH_SIZE];
    bool initialized;
    uint32_t next_ino;
} ramfs_hash_t;

/* API */
void ramfs_hash_init(ramfs_hash_t *hash);
int32_t ramfs_hash_add(ramfs_hash_t *hash, ramfs_file_t *file);
ramfs_file_t *ramfs_hash_find(ramfs_hash_t *hash, const char *name);
void ramfs_hash_remove(ramfs_hash_t *hash, ramfs_file_t *file);
```

#### 文件锁哈希索引

```c
/* fs_lock_hash.h */
#define FS_LOCK_HASH_SIZE  64U

typedef struct
{
    uint32_t        mount_id;
    uint32_t        ino;
    fs_lock_entry_t *lock;
    uint32_t        hash;
} fs_lock_hash_entry_t;

typedef struct
{
    fs_lock_hash_entry_t entries[FS_LOCK_HASH_SIZE];
    bool initialized;
} fs_lock_hash_t;

/* API */
void fs_lock_hash_init(fs_lock_hash_t *hash);
int32_t fs_lock_hash_add(fs_lock_hash_t *hash, fs_lock_entry_t *lock);
fs_lock_entry_t *fs_lock_hash_find(fs_lock_hash_t *hash, uint32_t mount_id, uint32_t ino);
void fs_lock_hash_remove(fs_lock_hash_t *hash, uint32_t mount_id, uint32_t ino);
```

### 2.3 验收标准

- [ ] 文件查找性能 > 250,000 ops/s
- [ ] 文件锁查找性能 > 100,000 ops/s
- [ ] 路径解析性能 > 200,000 ops/s
- [ ] MISRA C:2012 合规
- [ ] 单元测试覆盖率 > 90%

---

## 3. Phase 2: 缓存机制 (Week 3-5)

### 3.1 任务清单

- [ ] **Task 2.1**: 实现 inode 缓存
  - 创建 `fs_inode_cache.c` + `fs_inode_cache.h`
  - 实现 `inode_cache_init()`, `inode_cache_get()`, `inode_cache_put()`, `inode_cache_flush()`
  - 集成到所有文件系统操作
  - 测试: 缓存命中率统计

- [ ] **Task 2.2**: 实现页缓存
  - 创建 `fs_page_cache.c` + `fs_page_cache.h`
  - 实现 `page_cache_init()`, `page_cache_read()`, `page_cache_write()`, `page_cache_flush()`
  - 实现 LRU 淘汰策略
  - 集成到 ramfs read/write
  - 测试: 顺序/随机读取性能

- [ ] **Task 2.3**: 实现缓存一致性协议
  - 实现 writeback 机制（延迟写回）
  - 实现 flush 操作（同步写回）
  - 实现 dirty 标记和回收
  - 测试: 并发读写一致性

### 3.2 技术方案

#### Inode 缓存

```c
/* fs_inode_cache.h */
#define FS_INODE_CACHE_SIZE  32U

typedef struct
{
    uint32_t        ino;
    fs_inode_t      inode;
    uint64_t        last_access;
    bool            in_use;
    bool            dirty;
} inode_cache_entry_t;

typedef struct
{
    inode_cache_entry_t entries[FS_INODE_CACHE_SIZE];
    bool initialized;
    uint32_t access_count;
} inode_cache_t;

/* API */
void inode_cache_init(inode_cache_t *cache);
fs_inode_t *inode_cache_get(inode_cache_t *cache, uint32_t ino);
int32_t inode_cache_put(inode_cache_t *cache, const fs_inode_t *inode);
void inode_cache_flush(inode_cache_t *cache);
void inode_cache_clear(inode_cache_t *cache);
```

#### 页缓存

```c
/* fs_page_cache.h */
#define FS_PAGE_CACHE_SIZE  8192U
#define FS_PAGE_SIZE        4096U

typedef struct
{
    uint32_t        ino;
    uint64_t        offset;
    uint8_t         data[FS_PAGE_SIZE];
    bool            dirty;
    bool            in_use;
    uint64_t        last_access;
    struct page_cache_entry_t *prev;
    struct page_cache_entry_t *next;
} page_cache_entry_t;

typedef struct
{
    page_cache_entry_t entries[FS_PAGE_CACHE_SIZE];
    bool initialized;
} page_cache_t;

/* API */
void page_cache_init(page_cache_t *cache);
uint8_t *page_cache_read(page_cache_t *cache, uint32_t ino, uint64_t offset);
int32_t page_cache_write(page_cache_t *cache, uint32_t ino, uint64_t offset,
                         const uint8_t *data, uint64_t size);
void page_cache_flush(page_cache_t *cache);
void page_cache_clear(page_cache_t *cache);
```

### 3.3 验收标准

- [ ] 缓存命中率 > 60%
- [ ] 顺序读取吞吐量 > 100 MB/s
- [ ] 随机读取吞吐量 > 150,000 ops/s
- [ ] MISRA C:2012 合规
- [ ] 单元测试覆盖率 > 90%
- [ ] QEMU 测试全部通过

---

## 4. Phase 3: 锁优化 (Week 6-7)

### 4.1 任务清单

- [ ] **Task 3.1**: 替换 Mutex 为 Ticket Lock
  - 实现 `ticket_spinlock_t` 替代 `fs_file_lock_t`
  - 在 `fs_ops.c` 中替换全局锁
  - 测试: 多线程锁竞争性能

- [ ] **Task 3.2**: 实现按挂载点的细粒度锁
  - 创建 `fs_mount_lock.c` + `fs_mount_lock.h`
  - 为每个挂载点维护独立的锁
  - 集成到所有文件操作
  - 测试: 多挂载点并发性能

- [ ] **Task 3.3**: 优化文件锁查找（使用 Phase 1 的哈希索引）
  - 使用 `fs_lock_hash` 替代线性搜索
  - 测试: 高并发锁操作性能

### 4.2 技术方案

#### Ticket Lock

```c
/* ticket_lock.h */
typedef struct
{
    uint32_t          next_ticket;
    uint32_t          serving_ticket;
} ticket_spinlock_t;

/* API */
static inline void ticket_spinlock_lock(ticket_spinlock_t *lock);
static inline void ticket_spinlock_unlock(ticket_spinlock_t *lock);
```

#### 挂载点锁

```c
/* fs_mount_lock.h */
#define FS_PER_MOUNT_LOCKS  32U

typedef struct
{
    uint32_t        mount_id;
    ticket_spinlock_t lock;
} fs_mount_lock_t;

typedef struct
{
    fs_mount_lock_t locks[FS_MAX_MOUNTS];
    bool initialized;
} fs_mount_lock_mgr_t;

/* API */
void fs_mount_lock_mgr_init(fs_mount_lock_mgr_t *mgr);
void fs_mount_lock_mgr_lock(fs_mount_lock_mgr_t *mgr, uint32_t mount_id);
void fs_mount_lock_mgr_unlock(fs_mount_lock_mgr_t *mgr, uint32_t mount_id);
```

### 4.3 验收标准

- [ ] 多线程吞吐量 > 200,000 ops/s
- [ ] 锁竞争减少 > 60%
- [ ] 延迟降低 > 70%
- [ ] MISRA C:2012 合规
- [ ] 单元测试覆盖率 > 90%

---

## 5. Phase 4: 内存访问优化 (Week 8-9)

### 5.1 任务清单

- [ ] **Task 4.1**: 优化小数据读写（直接指针访问）
  - 在 `ramfs_read()`, `ramfs_write()` 中添加优化路径
  - 对 <= 64 字节的数据使用直接指针访问
  - 测试: 小文件读写性能

- [ ] **Task 4.2**: 实现批量操作接口
  - 创建 `fs_ops_batch.c` + `fs_ops_batch.h`
  - 实现 `fs_read_batch()`, `fs_write_batch()`
  - 集成到用户态服务
  - 测试: 大文件吞吐量

### 5.2 技术方案

#### 批量读写优化

```c
/* fs_ops_batch.h */
int64_t fs_read_batch(uint32_t mount_id, uint32_t ino,
                      uint64_t offset, void *buf, uint64_t size);
int64_t fs_write_batch(uint32_t mount_id, uint32_t ino,
                       uint64_t offset, const void *buf, uint64_t size);
```

### 5.3 验收标准

- [ ] 小文件读写性能提升 > 20%
- [ ] 大文件吞吐量提升 > 15%
- [ ] MISRA C:2012 合规
- [ ] 单元测试覆盖率 > 90%

---

## 6. Phase 5: Inode 管理优化 (Week 10-11)

### 6.1 任务清单

- [ ] **Task 5.1**: 实现 Slab 分配器管理 inode
  - 创建 `fs_inode_slab.c` + `fs_inode_slab.h`
  - 集成到 ramfs inode 管理
  - 测试: 内存分配性能

- [ ] **Task 5.2**: 实现 Inode 回收机制
  - 创建 `fs_inode_reclaim.c` + `fs_inode_reclaim.h`
  - 实现 `inode_reclaim()`, `inode_reclaim_scan()`
  - 集成到文件删除操作
  - 测试: 内存利用率

### 6.2 技术方案

#### Inode 回收

```c
/* fs_inode_reclaim.h */
typedef struct
{
    uint32_t        ino;
    uint32_t        ref_count;
} inode_reclaim_entry_t;

typedef struct
{
    inode_reclaim_entry_t list[256U];
    bool initialized;
    uint32_t next_reclaim;
} inode_reclaim_t;

/* API */
void inode_reclaim_init(inode_reclaim_t *reclaim);
void inode_reclaim_add(inode_reclaim_t *reclaim, uint32_t ino);
void inode_reclaim_remove(inode_reclaim_t *reclaim, uint32_t ino);
uint32_t inode_reclaim_next(inode_reclaim_t *reclaim);
```

### 6.3 验收标准

- [ ] 内存利用率 > 95%
- [ ] Inode 分配性能提升 > 10%
- [ ] MISRA C:2012 合规
- [ ] 单元测试覆盖率 > 90%

---

## 7. 验收测试 (Week 12-13)

### 7.1 性能测试套件

创建 `tests/test_perf_fs_*` 测试用例：

- [ ] `test_perf_fs_simple.c` - 基础性能测试
  - 文件查找性能
  - 顺序读取性能
  - 顺序写入性能
  - 随机读写性能

- [ ] `test_perf_fs_multithread.c` - 多线程性能测试
  - 多线程文件读写
  - 多挂载点并发
  - 锁竞争测试

- [ ] `test_perf_fs_cache.c` - 缓存性能测试
  - 缓存命中率统计
  - 缓存一致性测试

- [ ] `test_perf_fs_stress.c` - 压力测试
  - 10,000 个文件
  - 1,000,000 次操作
  - 内存压力测试

### 7.2 验收标准

| 指标 | 优化前 | 目标 | 实际 | 状态 |
|------|--------|------|------|------|
| 文件查找性能 | ~31,000 ops/s | >250,000 | ___ | ___ |
| 顺序读取吞吐量 | ~35 MB/s | >100 MB/s | ___ | ___ |
| 顺序写入吞吐量 | ~35 MB/s | >100 MB/s | ___ | ___ |
| 随机读取吞吐量 | ~20,000 ops/s | >150,000 | ___ | ___ |
| 多线程吞吐量 | ~30,000 ops/s | >200,000 | ___ | ___ |
| 路径解析性能 | ~40,000 ops/s | >200,000 | ___ | ___ |
| 缓存命中率 | 0% | >60% | ___ | ___ |

### 7.3 验证清单

- [ ] 所有性能测试通过
- [ ] QEMU 集成测试通过
- [ ] 单元测试覆盖率 > 90%
- [ ] MISRA C:2012 合规（零偏差）
- [ ] 代码风格符合规范（Allman 括号 + 4 空格缩进）
- [ ] 文档完整（API 文档、设计文档）

---

## 8. 代码统计

### 8.1 文件清单

| Phase | 新增文件 | 修改文件 | 新增行数 | 修改行数 |
|-------|---------|---------|---------|---------|
| Phase 1 | 6 | 3 | ~800 | ~400 |
| Phase 2 | 4 | 3 | ~1,200 | ~300 |
| Phase 3 | 3 | 2 | ~500 | ~200 |
| Phase 4 | 2 | 2 | ~300 | ~100 |
| Phase 5 | 2 | 2 | ~400 | ~150 |
| 验收测试 | 4 | 0 | ~800 | 0 |
| **总计** | **21** | **12** | **~4,000** | **~1,150** |

### 8.2 工作量估算

- **开发时间**: 15-22 天
- **测试时间**: 5-7 天
- **文档时间**: 2-3 天
- **总计**: 22-32 天

---

## 9. 风险管理

### 9.1 技术风险

| 风险 | 可能性 | 影响 | 缓解措施 |
|------|-------|------|---------|
| 哈希冲突率高 | 中 | 中 | 使用二次哈希 + 开放寻址 |
| 缓存一致性问题 | 低 | 高 | 严格的 writeback 机制 |
| 线程安全风险 | 中 | 高 | 详细的并发测试 |
| 性能不达预期 | 低 | 中 | 保留回退方案 |

### 9.2 项目风险

| 风险 | 可能性 | 影响 | 缓解措施 |
|------|-------|------|---------|
| 需求变更 | 中 | 中 | 分阶段交付，增量验证 |
| 资源不足 | 低 | 高 | 优先保证核心性能 |
| 调试困难 | 中 | 中 | 详细日志、单元测试 |

---

## 10. 交付物清单

### 10.1 代码交付

- [ ] Phase 1-5 完整实现
- [ ] 单元测试套件（覆盖率 > 90%）
- [ ] 性能测试套件
- [ ] QEMU 验证通过

### 10.2 文档交付

- [ ] API 文档（Doxygen）
- [ ] 设计文档（fs_performance_analysis.md）
- [ ] 验收报告（fs_optimization_report.md）
- [ ] 代码注释（中文）

### 10.3 配置交付

- [ ] CMake 配置
- [ ] Makefile 目标
- [ ] 性能测试脚本

---

## 11. 下一步行动

1. **立即开始**: Phase 1.1 - 实现 ramfs 文件名哈希索引
2. **创建开发分支**: `feature/fs-performance-optimization`
3. **设置性能基准**: 运行当前的性能测试
4. **每周回顾**: 检查进度和性能提升

---

**完成时间**: 2026-05-08
**负责人**: AISafe64 编程助手
**状态**: ✅ 计划完成，待实施
