# AISafe64 优先级 P0 项目详细实施方案（续）

<a name="4"></a>
## 4. Capability 系统

### 4.1 项目概述

| 属性 | 值 |
|------|-----|
| **优先级** | P0 |
| **工期** | 8周 |
| **价值** | 极高 |
| **成本** | 高 |
| **风险** | 中 |
| **参考** | seL4 |
| **MISRA** | 完全合规 |

### 4.2 Capability 模型

#### 核心概念
```
Capability = 权限 + 对象引用
- 谁持有 Capability 谁就有权限
- 无 Capability 则无权限
- Capability 可转让（受控）
- Capability 可撤销
```

#### Capability 类型
```c
/* src/kernel/capability.h */

typedef enum {
    CAP_NULL = 0,

    /* 同步原语 */
    CAP_MUTEX,
    CAP_SEMAPHORE,

    /* 内存 */
    CAP_MEMORY,
    CAP_MAPPING,

    /* 任务 */
    CAP_THREAD_CONTROL,
    CAP_THREAD,

    /* IPC */
    CAP_ENDPOINT,
    CAP_NOTIFICATION,

    /* 中断 */
    CAP_IRQ_CONTROL,

    /* 设备 */
    CAP_DEVICE,

    /* 域 */
    CAP_CNODE,
    CAP_PD,

    CAP_TYPE_MAX
} CapabilityType_t;
```

#### Capability 结构
```c
/* 64字节对齐（缓存行）*/
typedef struct Capability {
    /* 头部（32字节）*/
    uint64_t cap_id;           /* 全局唯一 ID */
    CapabilityType_t type;     /* 类型 */
    uint32_t rights;           /* 权限位 */
    uint32_t guard;            /* 守卫值（防篡改）*/

    /* 数据（32字节）*/
    void *object_ptr;          /* 对象指针 */
    uint64_t object_size;      /* 对象大小 */
    uint32_t badge;            /* 徽章（IPC）*/
    uint32_t padding;

    /* 元数据 */
    uint64_t creation_time;
    uint32_t ref_count;
    bool persistent;           /* 持久化（不自动撤销）*/
} __attribute__((aligned(64))) Capability_t;
```

#### 权限定义
```c
#define CAP_RIGHT_READ    (1U << 0)
#define CAP_RIGHT_WRITE   (1U << 1)
#define CAP_RIGHT_EXEC    (1U << 2)
#define CAP_RIGHT_GRANT   (1U << 3)  /* 转让权限 */
#define CAP_RIGHT_DELETE  (1U << 4)  /* 删除对象 */
#define CAP_RIGHT_REVOKE  (1U << 5)  /* 撤销子 capability */
#define CAP_RIGHT_ALL     (0x3FU)
```

### 4.3 CNode（Capability 节点）

```c
/* CNode: 存储 capabilities 的容器 */
typedef struct CNode {
    Capability_t slots[256];     /* 256 个槽位 */
    uint32_t guard_bits;         /* 守卫位数 */
    uint32_t guard;              /* 守卫值 */
    uint32_t used_slots;         /* 已用槽位 */
    spinlock_t lock;             /* 保护锁 */
} CNode_t;

/* CNode 查找路径 */
typedef struct CapLookup {
    CNode_t *root_cnode;
    uint32_t cnode_depth;        /* 深度（最多 4 级）*/
    uint32_t path[4];            /* 查找路径 */
} CapLookup_t;
```

### 4.4 Capability 空间

```c
/* 每个 CSpace 一个 */
typedef struct CapSpace {
    CNode_t *root_cnode;         /* 根 CNode */
    uint64_t next_cap_id;        /* 下一个可用 ID */
    uint32_t total_caps;         /* 总数 */

    /* 撤销跟踪 */
    struct list_head revoke_list;

    spinlock_t lock;
} CapSpace_t;
```

### 4.5 核心 API

#### 创建 Capability
```c
/* src/kernel/capability.c */

int cap_create(CapSpace_t *cs,
              CapabilityType_t type,
              uint32_t rights,
              void *object_ptr,
              uint64_t object_size,
              Capability_t **cap_out) {
    /* 1. 参数验证 */
    if (cs == NULL || object_ptr == NULL || cap_out == NULL) {
        return -EINVAL;
    }

    if (type >= CAP_TYPE_MAX) {
        return -EINVAL;
    }

    /* 2. 分配 ID */
    uint64_t cap_id = __atomic_fetch_add(&cs->next_cap_id, 1,
                                        __ATOMIC_SEQ_CST);

    /* 3. 在根 CNode 中查找空闲槽位 */
    spin_lock(&cs->root_cnode->lock);

    uint32_t slot = find_free_slot(cs->root_cnode);
    if (slot == 0xFFFFFFFFU) {
        spin_unlock(&cs->root_cnode->lock);
        return -ENOSPC;
    }

    /* 4. 初始化 Capability */
    Capability_t *cap = &cs->root_cnode->slots[slot];

    cap->cap_id = cap_id;
    cap->type = type;
    cap->rights = rights;
    cap->object_ptr = object_ptr;
    cap->object_size = object_size;
    cap->guard = generate_guard();
    cap->creation_time = sched_clock();
    cap->ref_count = 1U;
    cap->persistent = false;

    cs->root_cnode->used_slots++;
    spin_unlock(&cs->root_cnode->lock);

    /* 5. 返回 */
    *cap_out = cap;

    printk("Created capability %llu (type=%u, rights=0x%x)\n",
           cap_id, type, rights);

    return 0;
}
```

#### 复制 Capability
```c
int cap_copy(Capability_t *src,
            Capability_t **dst_out,
            uint32_t new_rights) {
    /* 1. 验证源 capability */
    if (src == NULL || dst_out == NULL) {
        return -EINVAL;
    }

    /* 检查守卫值 */
    if (src->guard != compute_guard(src)) {
        return -EINVAL;
    }

    /* 2. 检查转让权限 */
    if (!(src->rights & CAP_RIGHT_GRANT)) {
        return -EPERM;
    }

    /* 3. 权限不能超过源 */
    if ((new_rights & src->rights) != new_rights) {
        return -EPERM;
    }

    /* 4. 创建副本 */
    Capability_t *dst = (Capability_t *)kmalloc(sizeof(Capability_t));
    if (dst == NULL) {
        return -ENOMEM;
    }

    /* 复制内容 */
    dst->cap_id = __atomic_fetch_add(&global_cap_id, 1,
                                     __ATOMIC_SEQ_CST);
    dst->type = src->type;
    dst->rights = new_rights;
    dst->object_ptr = src->object_ptr;
    dst->object_size = src->object_size;
    dst->guard = generate_guard();
    dst->creation_time = sched_clock();
    dst->ref_count = 1U;

    /* 5. 增加源引用计数 */
    __atomic_fetch_add(&src->ref_count, 1, __ATOMIC_SEQ_CST);

    *dst_out = dst;

    return 0;
}
```

#### 撤销 Capability
```c
int cap_revoke(Capability_t *cap) {
    /* 1. 验证 */
    if (cap == NULL) {
        return -EINVAL;
    }

    /* 2. 检查守卫值 */
    if (cap->guard != compute_guard(cap)) {
        return -EINVAL;
    }

    /* 3. 检查撤销权限 */
    if (!(cap->rights & CAP_RIGHT_REVOKE)) {
        return -EPERM;
    }

    /* 4. 递归撤销子 capabilities */
    if (cap->type == CAP_CNODE) {
        CNode_t *cnode = (CNode_t *)cap->object_ptr;

        for (uint32_t i = 0U; i < 256U; i++) {
            if (cnode->slots[i].type != CAP_NULL) {
                cap_revoke(&cnode->slots[i]);
            }
        }
    }

    /* 5. 删除对象（如果引用计数为 0）*/
    uint32_t ref_count = __atomic_sub_fetch(&cap->ref_count, 1,
                                           __ATOMIC_SEQ_CST);

    if (ref_count == 0U && !cap->persistent) {
        /* 释放对象 */
        if (cap->type == CAP_MEMORY) {
            kfree(cap->object_ptr);
        } else if (cap->type == CAP_MUTEX) {
            mutex_destroy((mutex_t *)cap->object_ptr);
        }
    }

    /* 6. 标记为无效 */
    cap->type = CAP_NULL;
    cap->guard = 0U;

    return 0;
}
```

#### 验证 Capability
```c
static inline int cap_validate(const Capability_t *cap,
                              uint32_t required_rights) {
    /* 1. NULL 检查 */
    if (cap == NULL) {
        return -EINVAL;
    }

    /* 2. 守卫值检查（防篡改）*/
    if (cap->guard != compute_guard(cap)) {
        printk("Capability guard mismatch\n");
        return -EINVAL;
    }

    /* 3. 类型检查 */
    if (cap->type == CAP_NULL) {
        return -EINVAL;
    }

    /* 4. 权限检查 */
    if ((cap->rights & required_rights) != required_rights) {
        printk("Permission denied: need 0x%x, have 0x%x\n",
               required_rights, cap->rights);
        return -EPERM;
    }

    return 0;
}
```

### 4.6 资源改造示例

#### 互斥锁 → Capability
```c
/* 使用前（不安全）*/
int mutex_lock(mutex_t *mutex) {
    /* 任何有指针的代码都能调用 */
    /* 无权限检查 */
}

/* 使用后（安全）*/
int mutex_lock_with_cap(Capability_t *mutex_cap) {
    /* 1. 验证 capability */
    int ret = cap_validate(mutex_cap, CAP_RIGHT_WRITE);
    if (ret != 0) {
        return ret;
    }

    /* 2. 检查类型 */
    if (mutex_cap->type != CAP_MUTEX) {
        return -EINVAL;
    }

    /* 3. 获取互斥锁 */
    mutex_t *mutex = (mutex_t *)mutex_cap->object_ptr;
    if (mutex == NULL) {
        return -EINVAL;
    }

    /* 4. 调用原始函数 */
    return mutex_lock(mutex);
}
```

#### 内存分配 → Capability
```c
/* 创建内存 capability */
int cap_alloc_memory(CapSpace_t *cs,
                    uint64_t size,
                    uint32_t rights,
                    Capability_t **cap_out) {
    /* 1. 分配内存 */
    void *ptr = kmalloc(size, GFP_KERNEL);
    if (ptr == NULL) {
        return -ENOMEM;
    }

    /* 2. 创建 capability */
    int ret = cap_create(cs, CAP_MEMORY, rights, ptr, size, cap_out);
    if (ret != 0) {
        kfree(ptr);
        return ret;
    }

    return 0;
}

/* 使用内存 capability */
void *cap_memory_access(Capability_t *mem_cap,
                       uint64_t offset,
                       uint32_t rights) {
    /* 1. 验证 */
    int ret = cap_validate(mem_cap, rights);
    if (ret != 0) {
        return NULL;
    }

    /* 2. 类型检查 */
    if (mem_cap->type != CAP_MEMORY) {
        return NULL;
    }

    /* 3. 边界检查 */
    if (offset >= mem_cap->object_size) {
        return NULL;
    }

    /* 4. 返回地址 */
    return (uint8_t *)mem_cap->object_ptr + offset;
}
```

### 4.7 集成到任务管理

```c
/* src/include/task.h - 修改 */
typedef struct TCB_t {
    /* ... 现有字段 ... */

    /* Capability 空间 */
    CapSpace_t *cap_space;

} TCB_t;

/* src/kernel/task.c - 修改 */
uint32_t task_create(...) {
    /* ... 现有代码 ... */

    /* 创建 CSpace */
    ret = cap_space_create(&task->cap_space);
    if (ret != 0) {
        /* 清理 */
        return ret;
    }

    /* 返回 capability（而非直接指针）*/
    return task_cap_id;
}
```

### 4.8 实施计划

| 周次 | 任务 | 交付物 |
|------|------|--------|
| Week 1-2 | 数据结构设计 | capability.h, cnode.h |
| Week 3-4 | 核心功能 | cap_create, cap_copy, cap_revoke |
| Week 5-6 | 资源改造 | 互斥锁、内存、任务 |
| Week 7 | 集成测试 | 测试套件 |
| Week 8 | 文档与优化 | MISRA 检查、性能优化 |

### 4.9 测试

```c
/* tests/test_capability.c */

void test_cap_create(void) {
    CapSpace_t *cs;
    cap_space_create(&cs);

    mutex_t *mutex = kmalloc(sizeof(mutex_t));
    mutex_init(mutex);

    Capability_t *cap;
    int ret = cap_create(cs, CAP_MUTEX, CAP_RIGHT_READ | CAP_RIGHT_WRITE,
                       mutex, sizeof(*mutex), &cap);

    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NOT_NULL(cap);
    TEST_ASSERT_EQUAL(CAP_MUTEX, cap->type);
}

void test_cap_copy_reduced_rights(void) {
    Capability_t *src, *dst;

    /* 创建全权限 */
    cap_create(cs, CAP_MEMORY, CAP_RIGHT_ALL, ptr, size, &src);

    /* 复制时减少权限 */
    int ret = cap_copy(src, &dst, CAP_RIGHT_READ);

    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(CAP_RIGHT_READ, dst->rights);
}

void test_cap_validate_fail(void) {
    Capability_t *cap;
    cap_create(cs, CAP_MUTEX, CAP_RIGHT_READ, mutex, sizeof(*mutex), &cap);

    /* 要求写权限，但只有读权限 */
    int ret = cap_validate(cap, CAP_RIGHT_WRITE);

    TEST_ASSERT_EQUAL(-EPERM, ret);
}

void test_cap_revoke(void) {
    Capability_t *cap;
    void *ptr = kmalloc(1024);

    cap_create(cs, CAP_MEMORY, CAP_RIGHT_ALL, ptr, 1024, &cap);

    /* 撤销 */
    cap_revoke(cap);

    /* 应该被标记为无效 */
    TEST_ASSERT_EQUAL(CAP_NULL, cap->type);
}
```

### 4.10 验收标准

- [ ] 15/15 单元测试通过
- [ ] Capability 泄漏为 0
- [ ] 性能开销 < 10%
- [ ] MISRA-C:2012 零警告
- [ ] 代码覆盖率 > 95%

---

<a name="5"></a>
## 5. Fast IPC

### 5.1 项目概述

| 属性 | 值 |
|------|-----|
| **优先级** | P0 |
| **工期** | 4周 |
| **价值** | 高 |
| **成本** | 中 |
| **风险** | 中 |
| **参考** | seL4, QNX |
| **MISRA** | 完全合规 |

### 5.2 设计目标

| 指标 | 当前 | 目标 | 参考 |
|------|------|------|------|
| IPC 延迟 | ~500ns | <100ns | seL4: 50ns |
| 吞吐量 | ~1M msg/s | >5M msg/s | QNX: 10M msg/s |
| 内存开销 | 1KB/msg | 64B/msg | seL4: 64B |

### 5.3 消息格式

#### 快速 IPC（基于寄存器）
```c
/* src/include/ipc_fast.h */

/* 消息寄存器（使用 x0-x7）*/
typedef struct {
    uint64_t mr[8];  /* 消息寄存器 */
    uint64_t label;  /* 消息标签 */
} FastIPC_Msg_t;

/* 消息标签定义 */
#define IPC_LABEL_SYNC   (0ULL << 60)  /* 同步调用 */
#define IPC_LABEL_ASYNC  (1ULL << 60)  /* 异步发送 */
#define IPC_LABEL_REPLY  (2ULL << 60)  /* 回复 */
```

#### 端点
```c
/* IPC 端点（Capability 包装）*/
typedef struct {
    Capability_t cap;

    /* 等待队列 */
    struct list_head send_queue;
    struct list_head recv_queue;

    /* 状态 */
    spinlock_t lock;
    uint32_t badge_counter;
} IPC_Endpoint_t;
```

### 5.4 发送端（客户端）

```c
/* 客户端发送 */
static inline int ipc_call(Capability_t *endpoint,
                           const FastIPC_Msg_t *msg,
                           FastIPC_Msg_t *reply) {
    /* 1. 验证 endpoint capability */
    int ret = cap_validate(endpoint, CAP_RIGHT_WRITE);
    if (ret != 0) return ret;

    /* 2. 准备系统调用参数 */
    register uint64_t x0 __asm("x0") = msg->mr[0];
    register uint64_t x1 __asm("x1") = msg->mr[1];
    register uint64_t x2 __asm("x2") = msg->mr[2];
    register uint64_t x3 __asm("x3") = msg->mr[3];
    register uint64_t x4 __asm("x4") = msg->mr[4];
    register uint64_t x5 __asm("x5") = msg->mr[5];
    register uint64_t x6 __asm("x6") = msg->mr[6];
    register uint64_t x7 __asm("x7") = msg->mr[7];
    register uint64_t x8 __asm("x8") = msg->label;
    register uint64_t x9 __asm("x9") = endpoint->cap_id;

    /* 3. 触发系统调用（SVC #2）*/
    __asm__ volatile(
        "svc #2"
        : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
          "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7),
          "+r"(x8), "+r"(x9)
        :
        : "memory"
    );

    /* 4. 读取回复 */
    reply->mr[0] = x0;
    reply->mr[1] = x1;
    reply->mr[2] = x2;
    reply->mr[3] = x3;
    reply->mr[4] = x4;
    reply->mr[5] = x5;
    reply->mr[6] = x6;
    reply->mr[7] = x7;
    reply->label = x8;

    /* x9 包含错误码 */
    return (x9 >> 32) & 0xFFFFFFFF;
}
```

### 5.5 系统调用处理

```c
/* src/kernel/ipc.c */

void sys_ipc_handler(void) {
    TCB_t *caller = current;
    FastIPC_Msg_t msg;
    uint64_t endpoint_id;

    /* 1. 从寄存器读取消息 */
    __asm__ volatile(
        "mov %0, x0\n"
        "mov %1, x1\n"
        "mov %2, x2\n"
        "mov %3, x3\n"
        "mov %4, x4\n"
        "mov %5, x5\n"
        "mov %6, x6\n"
        "mov %7, x7\n"
        "mov %8, x8\n"
        "mov %9, x9\n"
        : "=r"(msg.mr[0]), "=r"(msg.mr[1]), "=r"(msg.mr[2]),
          "=r"(msg.mr[3]), "=r"(msg.mr[4]), "=r"(msg.mr[5]),
          "=r"(msg.mr[6]), "=r"(msg.mr[7]),
          "=r"(msg.label), "=r"(endpoint_id)
    );

    /* 2. 查找 endpoint capability */
    Capability_t *endpoint = cap_lookup_by_id(endpoint_id);
    if (endpoint == NULL || endpoint->type != CAP_ENDPOINT) {
        caller->regs[9] = (-EINVAL << 32);
        return;
    }

    /* 3. 获取端点 */
    IPC_Endpoint_t *ep = endpoint->object_ptr;

    /* 4. 检查是否有等待的服务线程 */
    spin_lock(&ep->lock);

    if (!list_empty(&ep->recv_queue)) {
        /* 有等待的服务线程 */
        TCB_t *server = list_first_entry(&ep->recv_queue,
                                        TCB_t, ipc_list);

        /* 直接传递消息（无复制）*/
        server->ipc_msg = msg;
        server->ipc_from = caller;
        server->ipc_state = IPC_STATE_READY;

        /* 唤醒服务线程 */
        list_del_init(&server->ipc_list);
        task_ready(server);

        spin_unlock(&ep->lock);

        /* 阻塞客户端，等待回复 */
        caller->ipc_state = IPC_STATE_WAITING;
        task_block(caller);
    } else {
        /* 无等待的服务线程 */
        spin_unlock(&ep->lock);

        /* 将客户端加入发送队列 */
        spin_lock(&ep->lock);
        list_add_tail(&caller->ipc_list, &ep->send_queue);
        caller->ipc_msg = msg;
        caller->ipc_state = IPC_STATE_WAITING;
        task_block(caller);
        spin_unlock(&ep->lock);
    }

    /* 调度其他任务 */
    schedule();
}
```

### 5.6 接收端（服务）

```c
/* 服务端接收 */
static inline int ipc_reply_wait(Capability_t *endpoint,
                                 const FastIPC_Msg_t *reply,
                                 FastIPC_Msg_t *request) {
    TCB_t *server = current;

    /* 1. 验证 endpoint capability */
    int ret = cap_validate(endpoint, CAP_RIGHT_READ);
    if (ret != 0) return ret;

    IPC_Endpoint_t *ep = endpoint->object_ptr;

    /* 2. 检查是否有待处理的消息 */
    spin_lock(&ep->lock);

    if (!list_empty(&ep->send_queue)) {
        /* 有待处理的消息 */
        TCB_t *client = list_first_entry(&ep->send_queue,
                                        TCB_t, ipc_list);

        /* 复制消息 */
        *request = client->ipc_msg;
        server->ipc_client = client;

        /* 移除客户端 */
        list_del_init(&client->ipc_list);

        spin_unlock(&ep->lock);

        return 0;
    }

    /* 3. 无消息，等待 */
    list_add_tail(&server->ipc_list, &ep->recv_queue);
    server->ipc_state = IPC_STATE_WAITING;

    spin_unlock(&ep->lock);

    /* 阻塞 */
    task_block(server);
    schedule();

    /* 被唤醒后，有消息可用 */
    *request = server->ipc_msg;
    return 0;
}
```

### 5.7 回复消息

```c
/* 发送回复 */
static inline int ipc_reply(const FastIPC_Msg_t *reply) {
    TCB_t *server = current;
    TCB_t *client = server->ipc_client;

    if (client == NULL) {
        return -EINVAL;
    }

    /* 1. 复制回复到客户端寄存器 */
    client->regs[0] = reply->mr[0];
    client->regs[1] = reply->mr[1];
    client->regs[2] = reply->mr[2];
    client->regs[3] = reply->mr[3];
    client->regs[4] = reply->mr[4];
    client->regs[5] = reply->mr[5];
    client->regs[6] = reply->mr[6];
    client->regs[7] = reply->mr[7];
    client->regs[8] = reply->label | IPC_LABEL_REPLY;

    /* 2. 唤醒客户端 */
    task_ready(client);

    /* 3. 清理 */
    server->ipc_client = NULL;

    return 0;
}
```

### 5.8 使用示例

#### 客户端代码
```c
/* 客户端：调用文件系统服务 */
Capability_t *fs_endpoint;  /* 从 CSpace 获取 */

FastIPC_Msg_t msg, reply;
msg.mr[0] = 0x01;  /* OPEN 命令 */
msg.mr[1] = (uint64_t)"/etc/config.txt";
msg.mr[2] = 0x00;  /* 标志 */
msg.label = IPC_LABEL_SYNC;

int ret = ipc_call(fs_endpoint, &msg, &reply);

if (ret == 0) {
    int fd = reply.mr[0];
    /* 使用 fd ... */
}
```

#### 服务端代码
```c
/* 服务端：文件系统服务 */
void fs_server_thread(void) {
    Capability_t *server_endpoint;

    /* 创建 endpoint */
    cap_create_endpoint(&server_endpoint);

    FastIPC_Msg_t request, reply;

    while (1) {
        /* 等待请求 */
        int ret = ipc_reply_wait(server_endpoint, NULL, &request);

        if (ret != 0) continue;

        /* 处理请求 */
        switch (request.mr[0]) {
            case 0x01:  /* OPEN */
                reply.mr[0] = fs_open((const char *)request.mr[1],
                                     request.mr[2]);
                break;
            /* ... */
        }

        /* 回复 */
        ipc_reply(&reply);
    }
}
```

### 5.9 性能优化

#### 优化 1: 避免内存复制
```c
/* 不好的方式：复制消息 */
int ipc_send_bad(void *data, size_t len) {
    void *copy = kmalloc(len);
    memcpy(copy, data, len);
    /* 发送 copy ... */
    kfree(copy);
}

/* 优化的方式：直接传递寄存器 */
int ipc_send_optimized(const FastIPC_Msg_t *msg) {
    /* 消息已经在寄存器中，零拷贝 */
    __asm__ volatile("svc #2");
}
```

#### 优化 2: 批量处理
```c
/* 批量 IPC */
int ipc_call_batch(Capability_t **endpoints,
                  FastIPC_Msg_t *msgs,
                  FastIPC_Msg_t *replies,
                  uint32_t count) {
    /* 预取所有 endpoint capabilities */
    for (uint32_t i = 0U; i < count; i++) {
        __builtin_prefetch(endpoints[i]);
    }

    /* 批量调用 */
    for (uint32_t i = 0U; i < count; i++) {
        ipc_call(endpoints[i], &msgs[i], &replies[i]);
    }
}
```

### 5.10 测试

```c
/* tests/test_ipc_fast.c */

void test_ipc_ping_pong(void) {
    /* 创建 endpoint */
    Capability_t *ep;
    cap_create_endpoint(&ep);

    /* 客户端发送 */
    FastIPC_Msg_t msg = { .label = IPC_LABEL_SYNC };
    msg.mr[0] = 0x42;

    /* 服务端接收 */
    FastIPC_Msg_t recv;
    ipc_reply_wait(ep, NULL, &recv);

    TEST_ASSERT_EQUAL(0x42ULL, recv.mr[0]);
}

void test_ipc_latency(void) {
    uint64_t start, end;
    uint64_t total = 0;
    uint32_t iterations = 1000;

    for (uint32_t i = 0; i < iterations; i++) {
        start = sched_clock();

        ipc_call(ep, &msg, &reply);

        end = sched_clock();
        total += (end - start);
    }

    uint64_t avg_ns = total / iterations;

    /* 目标: <100ns */
    TEST_ASSERT_LESS_THAN(100ULL, avg_ns);
}
```

### 5.11 验收标准

- [ ] 10/10 单元测试通过
- [ ] IPC 延迟 <100ns
- [ ] 吞吐量 >5M msg/s
- [ ] MISRA-C:2012 零警告
- [ ] 代码覆盖率 > 95%

---

## 总结

本阶段（优先级 P0）的 5 个项目将在 **2-8周内**完成，显著提升 AISafe64 的安全性：

1. **栈溢出保护** (2周) - 立即提升内存安全
2. **MPU/MMU 抽象** (3周) - 硬件强制隔离
3. **安全钩子框架** (2周) - 可扩展安全策略
4. **Capability 系统** (8周) - 形式化验证友好
5. **Fast IPC** (4周) - 微内核化基础

**总工期**: 8-10周（可并行）
**风险**: 低-中
**价值**: 极高

下一阶段将实施优先级 P1 的项目...

---

**文档版本**: 1.0
**最后更新**: 2025-01-08
**作者**: AISafe64 Team
