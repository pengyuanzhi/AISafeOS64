## 6. 并发和同步规范

### 6.1 锁使用规范

#### 6.1.1 自旋锁
```c
/* ✅ 正确: 使用ticket lock */
TicketLock_t lock = {0};

void critical_section(void) {
    ticket_lock_acquire(&lock);

    /* 临界区代码 */
    protected_variable++;

    ticket_lock_release(&lock);
}

/* ❌ 错误: 忘记释放锁 */
void critical_section(void) {
    ticket_lock_acquire(&lock);

    if (error) {
        return;  /* 忘记释放锁 */
    }

    ticket_lock_release(&lock);
}

/* ✅ 正确: 确保锁释放 */
void critical_section(void) {
    ticket_lock_acquire(&lock);

    if (error) {
        ticket_lock_release(&lock);
        return;
    }

    ticket_lock_release(&lock);
}
```

#### 6.1.2 互斥锁（任务上下文）
```c
/* ✅ 正确: 在任务上下文使用互斥锁 */
Mutex_t mutex;
mutex_init(&mutex);

void task_function(void) {
    mutex_lock(&mutex);

    /* 临界区代码 */

    mutex_unlock(&mutex);
}

/* ❌ 错误: 在中断中使用互斥锁 */
void irq_handler(void) {
    mutex_lock(&mutex);  /* 可能死锁 */
    /* ... */
}
```

#### 6.1.3 锁顺序规范
```c
/* 定义全局锁顺序 */
enum LockOrder {
    LOCK_ORDER_SCHEDULER = 0,
    LOCK_ORDER_MEMORY,
    LOCK_ORDER_SYNC,
    LOCK_ORDER_MAX
};

/* 始终按照相同顺序获取锁 */
void multi_lock_function(void) {
    /* 先获取scheduler锁 */
    ticket_lock_acquire(&scheduler.lock);

    /* 再获取memory锁 */
    ticket_lock_acquire(&memory.lock);

    /* 临界区代码 */

    /* 按相反顺序释放 */
    ticket_lock_release(&memory.lock);
    ticket_lock_release(&scheduler.lock);
}
```

### 6.2 无锁编程

#### 6.2.1 单生产者单消费者（SPSC）队列
```c
typedef struct {
    uint32_t buffer[256];
    uint32_t head;
    uint32_t tail;
} SPSCQueue_t;

void spsc_enqueue(SPSCQueue_t *queue, uint32_t value) {
    uint32_t next_head = (queue->head + 1U) & 0xFFU;

    if (next_head == queue->tail) {
        return;  /* 队列满 */
    }

    queue->buffer[queue->head] = value;
    barrier_store();  /* 确保数据先写入 */
    queue->head = next_head;
}

bool spsc_dequeue(SPSCQueue_t *queue, uint32_t *value) {
    if (queue->tail == queue->head) {
        return false;  /* 队列空 */
    }

    *value = queue->buffer[queue->tail];
    barrier_load();  /* 确保数据先读取 */
    queue->tail = (queue->tail + 1U) & 0xFFU;

    return true;
}
```

#### 6.2.2 无锁栈
```c
typedef struct StackNode {
    uint32_t value;
    struct StackNode *next;
} StackNode_t;

typedef struct {
    StackNode_t *top;
} LockFreeStack_t;

void stack_push(LockFreeStack_t *stack, uint32_t value) {
    StackNode_t *node = malloc(sizeof(StackNode_t));
    if (node == NULL) {
        return;
    }

    node->value = value;

    do {
        node->next = stack->top;
    } while (!atomic_compare_exchange_weak(
        &stack->top,
        &node->next,
        node
    ));
}

bool stack_pop(LockFreeStack_t *stack, uint32_t *value) {
    StackNode_t *old_top;
    StackNode_t *new_top;

    do {
        old_top = stack->top;
        if (old_top == NULL) {
            return false;
        }
        new_top = old_top->next;
    } while (!atomic_compare_exchange_weak(
        &stack->top,
        &old_top,
        new_top
    ));

    *value = old_top->value;
    free(old_top);

    return true;
}
```

### 6.3 死锁预防

#### 6.3.1 超时机制
```c
bool mutex_lock_timeout(Mutex_t *mutex, uint32_t timeout_ms) {
    uint64_t start = get_system_time_ms();

    while (!mutex_try_lock(mutex)) {
        if ((get_system_time_ms() - start) >= timeout_ms) {
            return false;  /* 超时 */
        }
    }

    return true;
}
```

#### 6.3.2 死锁检测
```c
/* 构建资源分配图 */
typedef struct {
    uint8_t wait_graph[256][256];  /* 任务等待矩阵 */
} DeadlockDetector_t;

bool detect_deadlock(void) {
    /* 使用DFS检测环路 */
    for (uint8_t i = 0U; i < 256U; i++) {
        if (dfs_detect_cycle(i)) {
            return true;
        }
    }
    return false;
}
```

---

