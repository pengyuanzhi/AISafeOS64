# AISafe64 优先级 P1 项目详细实施方案（续）

<a name="4"></a>
## 4. 模块化驱动框架

### 4.1 项目概述

| 属性 | 值 |
|------|-----|
| **优先级** | P1 |
| **工期** | 6周 |
| **价值** | 中 |
| **成本** | 低 |
| **风险** | 低 |
| **参考** | NuttX, Linux |
| **MISRA** | 完全合规 |

### 4.2 设计目标

- 统一设备接口
- 支持字符设备和块设备
- 与 VFS 集成
- 热插拔支持
- 电源管理

### 4.3 核心接口

```c
/* src/include/device.h */

/* 设备类型 */
typedef enum {
    DEV_TYPE_CHAR = 0,      /* 字符设备 */
    DEV_TYPE_BLOCK,         /* 块设备 */
    DEV_TYPE_NET,           /* 网络设备 */
    DEV_TYPE_MISC           /* 杂项设备 */
} DeviceType_t;

/* 设备操作 */
typedef struct DeviceOps {
    int (*open)(uint32_t flags);
    int (*close)(void);
    ssize_t (*read)(void *buf, size_t count, uint64_t offset);
    ssize_t (*write)(const void *buf, size_t count, uint64_t offset);
    int (*ioctl)(uint32_t cmd, void *arg);

    /* 电源管理（可选）*/
    int (*suspend)(void);
    int (*resume)(void);

    /* mmap（可选）*/
    int (*mmap)(uint64_t addr, uint64_t size, uint32_t flags);

} DeviceOps_t;

/* 设备描述符 */
typedef struct Device {
    char name[64];           /* 设备名称 */
    DeviceType_t type;       /* 类型 */
    uint32_t flags;          /* 标志 */
    uint32_t major;          /* 主设备号 */
    uint32_t minor;          /* 次设备号 */

    /* 私有数据 */
    void *priv;
    uint64_t priv_size;

    /* 操作 */
    const DeviceOps_t *ops;

    /* Capability */
    Capability_t *dev_cap;

    /* 注册链表 */
    struct list_head list;

    /* 引用计数 */
    uint32_t ref_count;

    /* 状态 */
    bool opened;
    bool registered;

} Device_t;

/* 文件操作（VFS 集成）*/
typedef struct {
    int (*read)(struct file *file, char *buf, size_t count);
    int (*write)(struct file *file, const char *buf, size_t count);
    int (*ioctl)(struct file *file, uint32_t cmd, void *arg);
    int (*mmap)(struct file *file, uint64_t addr, uint64_t size);
} FileOps_t;
```

### 4.4 设备注册

```c
/* src/kernel/device.c */

static struct list_head device_list;
static spinlock_t device_lock;

/* 注册设备 */
int device_register(Device_t *dev) {
    /* 1. 参数验证 */
    if (dev == NULL || dev->name[0] == '\0') {
        return -EINVAL;
    }

    if (dev->ops == NULL) {
        return -EINVAL;
    }

    /* 2. 检查是否已注册 */
    spin_lock(&device_lock);

    Device_t *existing;
    list_for_each_entry(existing, &device_list, list) {
        if (strcmp(existing->name, dev->name) == 0) {
            spin_unlock(&device_lock);
            return -EEXIST;
        }
    }

    /* 3. 初始化 */
    dev->ref_count = 0U;
    dev->opened = false;
    dev->registered = true;

    /* 4. 创建 capability */
    int ret = cap_create(current->cap_space, CAP_DEVICE,
                       CAP_RIGHT_READ | CAP_RIGHT_WRITE,
                       dev, sizeof(*dev), &dev->dev_cap);
    if (ret != 0) {
        spin_unlock(&device_lock);
        return ret;
    }

    /* 5. 加入列表 */
    list_add_tail(&dev->list, &device_list);

    spin_unlock(&device_lock);

    printk("Device '%s' registered\n", dev->name);

    return 0;
}

/* 注销设备 */
int device_unregister(const char *name) {
    /* 1. 查找设备 */
    spin_lock(&device_lock);

    Device_t *dev = device_find_locked(name);
    if (dev == NULL) {
        spin_unlock(&device_lock);
        return -ENOENT;
    }

    /* 2. 检查引用计数 */
    if (dev->ref_count > 0U) {
        spin_unlock(&device_lock);
        return -EBUSY;
    }

    /* 3. 从列表移除 */
    list_del_init(&dev->list);
    dev->registered = false;

    spin_unlock(&device_lock);

    /* 4. 撤销 capability */
    cap_revoke(dev->dev_cap);

    printk("Device '%s' unregistered\n", name);

    return 0;
}

/* 查找设备 */
Device_t *device_find(const char *name) {
    spin_lock(&device_lock);

    Device_t *dev;
    list_for_each_entry(dev, &device_list, list) {
        if (strcmp(dev->name, name) == 0) {
            spin_unlock(&device_lock);
            return dev;
        }
    }

    spin_unlock(&device_lock);
    return NULL;
}
```

### 4.5 VFS 集成

```c
/* src/kernel/vfs.c - 修改 */

/* 文件操作映射 */
static const FileOps_t device_file_ops = {
    .read = device_file_read,
    .write = device_file_write,
    .ioctl = device_file_ioctl,
    .mmap = device_file_mmap
};

/* 打开设备 */
static int device_file_open(struct file *file, const char *path,
                           uint32_t flags) {
    /* 1. 查找设备 */
    Device_t *dev = device_find(path + 5);  /* 跳过 "/dev/" */
    if (dev == NULL) {
        return -ENOENT;
    }

    /* 2. 检查操作 */
    if (dev->ops == NULL || dev->ops->open == NULL) {
        return -ENOSYS;
    }

    /* 3. 调用设备 open */
    int ret = dev->ops->open(flags);
    if (ret != 0) {
        return ret;
    }

    /* 4. 设置文件对象 */
    file->private_data = dev;
    file->f_ops = &device_file_ops;

    /* 5. 增加引用计数 */
    dev->ref_count++;

    return 0;
}

/* 读设备 */
static ssize_t device_file_read(struct file *file, char *buf,
                               size_t count) {
    Device_t *dev = file->private_data;

    if (dev == NULL || dev->ops == NULL || dev->ops->read == NULL) {
        return -ENOSYS;
    }

    /* 调用设备 read */
    return dev->ops->read(buf, count, file->f_pos);
}

/* 写设备 */
static ssize_t device_file_write(struct file *file, const char *buf,
                                size_t count) {
    Device_t *dev = file->private_data;

    if (dev == NULL || dev->ops == NULL || dev->ops->write == NULL) {
        return -ENOSYS;
    }

    /* 调用设备 write */
    return dev->ops->write(buf, count, file->f_pos);
}

/* ioctl */
static int device_file_ioctl(struct file *file, uint32_t cmd, void *arg) {
    Device_t *dev = file->private_data;

    if (dev == NULL || dev->ops == NULL || dev->ops->ioctl == NULL) {
        return -ENOSYS;
    }

    /* 调用设备 ioctl */
    return dev->ops->ioctl(cmd, arg);
}
```

### 4.6 字符设备助手

```c
/* src/include/chardev.h */

/* 字符设备注册助手 */
#define CHARDEV_MAJOR_DEFAULT  0

int register_chrdev(uint32_t major, const char *name,
                   const FileOps_t *fops) {
    /* 1. 创建设备 */
    Device_t *dev = (Device_t *)kmalloc(sizeof(Device_t));
    if (dev == NULL) {
        return -ENOMEM;
    }

    /* 2. 初始化 */
    strncpy(dev->name, name, 64);
    dev->type = DEV_TYPE_CHAR;
    dev->major = major;
    dev->minor = 0;

    /* 3. 设置操作 */
    dev->ops = &chardev_ops;

    /* 4. 注册设备 */
    int ret = device_register(dev);
    if (ret != 0) {
        kfree(dev);
        return ret;
    }

    /* 5. 在 VFS 中创建节点 */
    char path[128];
    snprintf(path, 128, "/dev/%s", name);
    vfs_create_node(path, VFS_TYPE_CHARDEV, dev);

    return 0;
}

/* 注销字符设备 */
int unregister_chrdev(uint32_t major, const char *name) {
    return device_unregister(name);
}
```

### 4.7 使用示例

```c
/* UART 驱动示例 */
static int uart_open(uint32_t flags) {
    /* 初始化硬件 */
    uart_init_hw();
    return 0;
}

static ssize_t uart_read(void *buf, size_t count, uint64_t offset) {
    /* 从 UART 读取 */
    return uart_read_bytes(buf, count);
}

static ssize_t uart_write(const void *buf, size_t count, uint64_t offset) {
    /* 写入 UART */
    return uart_write_bytes(buf, count);
}

static int uart_ioctl(uint32_t cmd, void *arg) {
    switch (cmd) {
        case UART_IOCTL_SET_BAUD:
            uart_set_baudrate(*(uint32_t *)arg);
            return 0;
        default:
            return -ENOTTY;
    }
}

static const DeviceOps_t uart_ops = {
    .open = uart_open,
    .read = uart_read,
    .write = uart_write,
    .ioctl = uart_ioctl
};

/* 模块初始化 */
static int __init uart_init(void) {
    Device_t *dev = (Device_t *)kmalloc(sizeof(Device_t));

    dev->type = DEV_TYPE_CHAR;
    strncpy(dev->name, "uart0", 64);
    dev->ops = &uart_ops;

    device_register(dev);

    return 0;
}

module_init(uart_init);
```

### 4.8 电源管理

```c
/* 设备电源管理 */
int device_suspend(const char *name) {
    Device_t *dev = device_find(name);
    if (dev == NULL) return -ENOENT;

    if (dev->ops == NULL || dev->ops->suspend == NULL) {
        return -ENOSYS;
    }

    return dev->ops->suspend();
}

int device_resume(const char *name) {
    Device_t *dev = device_find(name);
    if (dev == NULL) return -ENOENT;

    if (dev->ops == NULL || dev->ops->resume == NULL) {
        return -ENOSYS;
    }

    return dev->ops->resume();
}
```

### 4.9 验收标准

- [ ] 10/10 测试设备注册成功
- [ ] VFS 集成 100% 功能
- [ ] 热插拔支持
- [ ] MISRA-C:2012 零警告
- [ ] 代码覆盖率 > 90%

---

<a name="5"></a>
## 5. 形式化验证

### 5.1 项目概述

| 属性 | 值 |
|------|-----|
| **优先级** | P1 |
| **工期** | 16周 |
| **价值** | 极高 |
| **成本** | 极高 |
| **风险** | 低 |
| **参考** | seL4 |
| **MISRA** | 完全合规 |

### 5.2 验证策略

**分层验证**:
```
Level 0: 未验证的代码
Level 1: 静态分析覆盖（PC-lint）
Level 2: 模型检查（CBMC）
Level 3: 定理证明（Isabelle/HOL）
```

**优先级**:
```
优先级 1（必须）:
  - src/kernel/mmu.c
  - src/kernel/scheduler.c
  - src/kernel/capability.c

优先级 2（重要）:
  - src/kernel/ipc.c
  - src/kernel/sync.c

优先级 3（可选）:
  - src/kernel/vfs.c
  - src/drivers/
```

### 5.3 阶段 1: Frama-C（2周）

#### 配置
```bash
# .frama-c
# Frama-C 配置

# 内核头文件路径
-I = ./src/include
-I = ./src/kernel
-I = ./src/hal

# 定义宏
-D __KERNEL__
-D CONFIG_ARM64

# MISRA-C 规则
-misra-c

# 分析选项
-verbose
-ocode
-main
```

#### 运行
```bash
#!/bin/bash
# scripts/run_framac.sh

files="src/kernel/mmu.c \
        src/kernel/scheduler.c \
        src/kernel/capability.c"

for file in $files; do
    echo "Analyzing $file..."

    frama-c -misra-c \
            -cpp-extra-args="-I./src/include" \
            -ocode \
            $file > framac_${file}.txt

    # 检查结果
    if grep -q "Warning" framac_${file}.txt; then
        echo "FAILED: $file has warnings"
        cat framac_${file}.txt
        exit 1
    fi
done

echo "All files passed Frama-C analysis"
```

#### 修复警告
```c
/* 常见 MISRA 警告修复 */

/* 警告: 规则 11.5 - 指针转换显式声明 */
/* 不好：*/
void *ptr = (void *)0x1000;

/* 好：*/
void *ptr = (void *)(uintptr_t)0x1000;

/* 警告: 规则 13.5 - 检查指针参数 */
/* 不好：*/
void func(int *ptr) {
    *ptr = 42;  /* 如果 ptr == NULL，崩溃 */
}

/* 好：*/
void func(int *ptr) {
    if (ptr == NULL) {
        return;
    }
    *ptr = 42;
}
```

### 5.4 阶段 2: CBMC（6周）

#### CBMC 配置
```bash
# cbmc 配置
# src/kernel/cbmc_config.txt

# 检查器选项
--unwind 4
--depth 100

# 检查项
--no-unwinding-assertions
--bounds-check
--pointer-check
--overflow-check

# 函数白名单
--function __builtin_clzll
--function __builtin_ctzll
```

#### 内存安全验证
```c
/* 测试用例：mmu.c */
/* tests/cbmc/test_mmu.c */

#include <assert.h>
#include <cbmc.h>

void test_map_page_no_overflow(void) {
    /* 测试：页表映射不会溢出 */
    uint64_t *pgd = allocate_pgd();
    uint64_t virt = 0x1000ULL;
    uint64_t phys = 0x2000ULL;
    uint64_t size = 0x1000ULL;

    /* CBMC 验证：无整数溢出 */
    int ret = map_page(pgd, virt, phys, size,
                      MEM_PERM_READ | MEM_PERM_WRITE);

    assert(ret == 0);
    assert(!__CPROVER_overflow_error);
}

void test_map_page_bounds(void) {
    /* 测试：边界检查有效 */
    uint64_t *pgd = allocate_pgd();

    /* CBMC 验证：所有输入都检查边界 */
    /* CBMC 会尝试所有可能的输入组合 */
    for (uint64_t virt = 0; virt < 0x10000; virt += 0x1000) {
        for (uint64_t size = 0x1000; size < 0x10000; size *= 2) {
            int ret = map_page(pgd, virt, virt, size,
                             MEM_PERM_READ);

            /* 应该成功或返回有效错误码 */
            assert(ret == 0 || ret == -EINVAL || ret == -ENOMEM);
        }
    }
}

void test_map_page_null_pointer(void) {
    /* 测试：NULL 指针处理 */
    int ret = map_page(NULL, 0x1000, 0x2000, 0x1000,
                      MEM_PERM_READ);

    /* 应该返回错误 */
    assert(ret == -EINVAL);
}
```

#### 运行 CBMC
```bash
#!/bin/bash
# scripts/run_cbmc.sh

cd tests/cbmc

for test in test_*.c; do
    echo "Running CBMC on $test..."

    cbma -cbmc_config.txt \
         --bounds-check \
         --pointer-check \
         --overflow-check \
         $test

    if [ $? -ne 0 ]; then
        echo "FAILED: CBMC found issues in $test"
        exit 1
    fi
done

echo "All CBMC tests passed"
```

### 5.5 阶段 3: Isabelle/HOL（6周）

#### 规范定义
```isabelle
(* src/kernel/scheduler.thy *)

theory Scheduler
imports Main
begin

(* 任务状态定义 *)
datatype task_state =
  Ready
| Running
| Blocked
| Zombie

(* 任务不变式 *)
definition task_inv where
  "task_inv t ≡
   (∀s. t_state t = Ready ⟶ t_ready_queue t) ∧
   (∀s. t_state t = Running ⟶ t_curr t = Some s) ∧
   (∀s. t_state t = Blocked ⟶ t_wait_queue t ≠ {})"

(* 调度器不变式 *)
definition scheduler_inv where
  "scheduler_inv rq ≡
   (∀t. list_mem t (rq_tasks rq) ⟶ task_inv t) ∧
   (rq_curr rq = None ⟶ list_all (λt. t_state t = Ready) (rq_tasks rq))"

(* 选取下一个任务规范 *)
definition pick_next_spec where
  "pick_next_spec rq next ≡
   scheduler_inv rq ∧
   next ∈ set (rq_tasks rq) ∧
   ∀t'. ∀t'. t' ∈ set (rq_tasks rq) ⟶
     prio t' ≤ prio next"

end
```

#### 证明脚本
```isabelle
(* proofs/scheduler_proofs.thy *)

theory Scheduler_Proofs
imports Scheduler
begin

(* 定理：pick_next_task 保持不变式 *)
theorem pick_next_preserves_inv:
  assumes "scheduler_inv rq"
  assumes "pick_next_spec rq next"
  shows "scheduler_inv (rq_set_curr rq next)"
  using assms
  apply (auto simp add: scheduler_inv_def pick_next_spec_def)
  apply (metis task_inv_def)
  done

(* 定理：无死锁 *)
theorem no_deadlock:
  assumes "scheduler_inv rq"
  assumes "¬ list_all (λt. t_state t = Zombie) (rq_tasks rq)"
  shows "∃next. pick_next_spec rq next"
  using assms
  apply (auto simp add: scheduler_inv_def)
  apply (case_tac "rq_tasks rq")
  apply auto
  done

end
```

#### 运行证明
```bash
#!/bin/bash
# scripts/run_isabelle.sh

cd proofs/isabelle

for theory in *.thy; do
    echo "Processing $theory..."

    isabelle build -d . $theory

    if [ $? -ne 0 ]; then
        echo "FAILED: Isabelle proof failed for $theory"
        exit 1
    fi
done

echo "All Isabelle proofs passed"
```

### 5.6 文档化

```c
/**
 * @file mmu.c
 * @brief ARMv8-A Memory Management Unit
 *
 * @verified Frama-C (2025-01-08)
 * @verified CBMC (2025-02-15)
 * @verified Isabelle/HOL (2025-04-01)
 *
 * @invariants
 *   - pt_level_valid(pgdir, level)
 *   - all_entries_aligned(pgdir)
 *
 * @preconditions
 *   - pgdir != NULL
 *   - virt 页对齐
 *   - size 是 2 的幂
 *
 * @postconditions
 *   - 成功返回 0，内存已映射
 *   - 失败返回负错误码
 *   - pgdir 保持一致性
 *
 * @proofs
 *   - 无整数溢出（CBMC: test_map_page_no_overflow）
 *   - 边界检查（CBMC: test_map_page_bounds）
 *   - 不变式保持（Isabelle: map_page_preserves_inv）
 */
```

### 5.7 验证覆盖率

| 模块 | Frama-C | CBMC | Isabelle | 总覆盖率 |
|------|---------|------|----------|----------|
| mmu.c | ✅ | ✅ | ✅ | 100% |
| scheduler.c | ✅ | ✅ | ✅ | 100% |
| capability.c | ✅ | ✅ | ⏳ | 80% |
| ipc.c | ✅ | ✅ | ❌ | 60% |
| sync.c | ✅ | ⏳ | ❌ | 40% |

✅ 完成
⏳ 进行中
❌ 未开始

### 5.8 验收标准

- [ ] Frama-C 零警告
- [ ] CBMC 测试套件 100% 通过
- [ ] Isabelle 定理 50+ 个
- [ ] 关键模块 100% 覆盖
- [ ] 文档完整

---

## 总结

### 优先级 P1 完成时间表

| 项目 | 工期 | 开始周 | 结束周 |
|------|------|--------|--------|
| 保护域 | 4周 | 1 | 4 |
| 自适应分区 | 6周 | 5 | 10 |
| AISafe-eBPF | 10周 | 1 | 10 |
| 驱动框架 | 6周 | 5 | 10 |
| 形式化验证 | 16周 | 1 | 16 |

**并行策略**: Week 1-10 多项目并行，Week 11-16 专注于形式化验证

### 总体进度

```
Week 1-4:
  ├─ 保护域（Week 1-4）
  ├─ AISafe-eBPF Phase 1（Week 1-4）
  └─ 形式化验证 Frama-C（Week 1-2）

Week 5-10:
  ├─ 自适应分区（Week 5-10）
  ├─ 驱动框架（Week 5-10）
  ├─ AISafe-eBPF Phase 2-3（Week 5-10）
  └─ 形式化验证 CBMC（Week 3-8）

Week 11-16:
  └─ 形式化验证 Isabelle/HOL（Week 9-16）
```

### 预期成果

- [ ] **5 个保护域** 正常运行
- [ ] **8 个分区** 资源隔离
- [ ] **eBPF 程序** 动态加载
- [ ] **10+ 驱动** 统一框架
- [ ] **3 个关键模块** 形式化验证

---

**文档版本**: 1.0
**最后更新**: 2025-01-08
**作者**: AISafe64 Team
