# LLD-001: Task Scheduler Low-Level Design

**Document ID**: LLD-001
**Version**: 1.0
**Date**: 2025-01-09
**Author**: AISafe64 Team
**Status**: Draft
**Parent**: HLD-003 (Kernel Module Design)

---

## 1. Module Overview

### 1.1 Purpose
The Task Scheduler module provides O(1) priority-based preemptive scheduling for up to 256 concurrent tasks across 1-8 CPU cores. It supports 256 priority levels, task migration, load balancing, and real-time scheduling constraints.

### 1.2 Scope
This document describes the low-level design of:
- 256-level priority bitmap scheduler
- Multi-core task dispatching and migration
- Context switching implementation
- Sleep/wake management
- Load balancing algorithm

### 1.3 References
- ARMv8-A Architecture Reference Manual
- MISRA-C:2012 Guidelines
- HLD-003: Kernel Module Design
- plan.md Section 4.3 (Scheduling Algorithm)

---

## 2. Data Structure Design

### 2.1 Task Control Block (TCB)

```c
/**
 * @brief Task Control Block
 * @note MISRA-C:2012 compliant
 * @note Cache-line aligned for SMP performance
 */
typedef struct TaskControlBlock
{
    /* Task Identification */
    uint64_t            task_id;             /**< Unique task ID */
    char                name[16];            /**< Task name (null-terminated) */

    /* Priority Management (256 levels) */
    uint8_t             priority;            /**< Current priority (0-255) */
    uint8_t             base_priority;       /**< Base priority (before donation) */
    uint8_t             state;               /**< Task state (see TaskState_t) */
    uint8_t             cpu_affinity;        /**< Preferred CPU (0-7) */

    /* Stack Management */
    uint64_t           *stack_ptr;           /**< Current stack pointer */
    uint64_t           *stack_base;          /**< Stack bottom address */
    uint32_t            stack_size;          /**< Stack size in bytes */
    uint32_t            stack_watermark;     /**< Stack usage watermark */

    /* Timing Information */
    uint64_t            runtime;             /**< Total runtime (ns) */
    uint64_t            last_wake_time;      /**< Last wake timestamp (ns) */
    uint64_t            timeslice;           /**< Time slice (ns) */
    uint64_t            sleep_deadline;      /**< Sleep deadline (ns) */
    uint64_t            sleep_start;         /**< Sleep start time (ns) */

    /* SMP Support */
    uint32_t            cpu_id;              /**< Currently running CPU */
    uint32_t            migrate_target;      /**< Migration target CPU */

    /* Synchronization */
    struct TaskControlBlock *next;           /**< Linked list pointer */
    struct TaskControlBlock *prev;           /**< Linked list pointer */
    uint16_t            lock_count;          /**< Number of held locks */

    /* Safety & Security */
    uint32_t            error_count;         /**< Error counter */
    uint64_t            page_table;          /**< Page table base address */

    /* Address Space Isolation */
    uint32_t            isolation_mode;      /**< 0=shared, 1=independent, 2=mixed */
    uint32_t            address_space_id;    /**< Address space group ID */

    /* CPU Context */
    uint64_t            context[32];         /**< Register save area */

} TCB_t;

/* Compile-time validation */
STATIC_ASSERT(sizeof(TCB_t) <= 1024U, TCB_size_exceeds_limit);
STATIC_ASSERT((offsetof(TCB_t, context) & 0xFU) == 0U, context_misaligned);
```

### 2.2 Task State Enumeration

```c
/**
 * @brief Task State Enumeration
 * @note MISRA-C:2012 compliant
 */
typedef enum
{
    TASK_READY = 0U,        /**< Ready: Waiting for CPU */
    TASK_RUNNING,           /**< Running: Currently executing */
    TASK_BLOCKED,           /**< Blocked: Waiting for resource */
    TASK_SLEEPING,          /**< Sleeping: Delayed wait */
    TASK_SUSPENDED          /**< Suspended: Explicitly suspended */
} TaskState_t;
```

### 2.3 Per-CPU Ready Queue

```c
/**
 * @brief Task List Head
 */
typedef struct
{
    TCB_t *head;            /**< List head */
    TCB_t *tail;            /**< List tail */
    uint32_t count;         /**< Task count */
} TaskList_t;

/**
 * @brief Per-CPU Ready Queue
 * @note 64-byte cache-line aligned
 */
typedef struct __attribute__((aligned(64)))
{
    uint64_t            bitmap[4];           /**< 256-bit priority map (4×64) */
    TaskList_t          queues[256];         /**< 256 priority queues */
    atomic_uint_fast32_t lock;               /**< Spinlock for queue access */
    uint32_t            task_count;          /**< Total task count */
} PerCPUReadyQueue_t;
```

### 2.4 Scheduler Core Structure

```c
/**
 * @brief Global Scheduler Structure
 * @note Access only via scheduler_lock()
 */
typedef struct
{
    /* Current Tasks */
    TCB_t              *current_task[MAX_CPUS];

    /* Ready Queues (per-CPU) */
    PerCPUReadyQueue_t  ready_queues[MAX_CPUS];

    /* Sleep Queue (sorted by deadline) */
    TaskList_t          sleep_queue;
    atomic_uint_fast32_t sleep_queue_lock;

    /* Blocked Queue (waiting for resources) */
    TaskList_t          blocked_queue;
    atomic_uint_fast32_t blocked_queue_lock;

    /* Scheduler State */
    atomic_uint_fast32_t cpu_mask;           /**< Active CPU mask */
    volatile uint64_t   lock_count[MAX_CPUS]; /**< Scheduler lock nesting */
    volatile uint8_t    scheduler_running;   /**< Running flag */

    /* System Time */
    volatile uint64_t   system_ticks;        /**< Tick counter */
    volatile uint64_t   system_time_ns;      /**< System time (ns) */

    /* Statistics */
    uint64_t            task_switches[MAX_CPUS];
    uint64_t            cpu_idle_ticks[MAX_CPUS];

    /* Load Balancing */
    uint32_t            load_balance_threshold;

} Scheduler_t;

/* Global scheduler instance */
extern Scheduler_t g_scheduler;
```

---

## 3. API Interface Definition

### 3.1 Scheduler Initialization

```c
/**
 * @brief Initialize the task scheduler
 * @return 0 on success, negative error code on failure
 *
 * @note Must be called before any other scheduler function
 * @note Not thread-safe
 * @warning Must be called before scheduler_start()
 */
int32_t scheduler_init(void);
```

### 3.2 Scheduler Control

```c
/**
 * @brief Start the scheduler
 * @return Does not return
 *
 * @note Enables interrupts and begins task scheduling
 * @warning Never returns
 */
void scheduler_start(void) __attribute__((noreturn));

/**
 * @brief Trigger task rescheduling
 * @note May be called from task context or ISR
 * @warning Must not be called with scheduler locked
 */
void schedule(void);

/**
 * @brief Lock the scheduler (disable task switching)
 * @note Nestable: multiple calls require matching unlocks
 * @warning Must unlock before calling blocking functions
 */
void scheduler_lock(void);

/**
 * @brief Unlock the scheduler
 * @note Decrements lock count; triggers schedule if count == 0
 */
void scheduler_unlock(void);
```

### 3.3 Task Management

```c
/**
 * @brief Create a new task
 * @param entry Task entry function (must not return)
 * @param priority Task priority (0-255)
 * @param stack_size Stack size in bytes (min 4096)
 * @param name Task name (max 16 chars)
 * @return Task ID (>0) on success, 0 on failure
 *
 * @note Task starts in READY state
 * @warning entry function must not return
 */
uint32_t task_create(void (*entry)(void),
                     uint8_t priority,
                     uint32_t stack_size,
                     const char *name);

/**
 * @brief Delete a task
 * @param task_id Task ID to delete
 * @return 0 on success, negative error code on failure
 *
 * @note Cannot delete current task or idle task
 * @warning Frees task resources including stack
 */
int32_t task_delete(uint32_t task_id);

/**
 * @brief Yield CPU to next ready task
 * @note Only affects tasks of same priority
 */
void task_yield(void);

/**
 * @brief Suspend a task
 * @param task_id Task ID to suspend
 * @return 0 on success, negative error code on failure
 *
 * @note Suspended task does not execute
 */
int32_t task_suspend(uint32_t task_id);

/**
 * @brief Resume a suspended task
 * @param task_id Task ID to resume
 * @return 0 on success, negative error code on failure
 */
int32_t task_resume(uint32_t task_id);
```

### 3.4 Task Sleep API

```c
/**
 * @brief Sleep current task for specified milliseconds
 * @param delay_ms Delay in milliseconds
 *
 * @note Task enters SLEEPING state
 * @note Relative sleep (from current time)
 * @warning Only callable from task context
 */
void task_sleep(uint32_t delay_ms);

/**
 * @brief Sleep until absolute deadline
 * @param deadline_ns Absolute deadline in nanoseconds
 * @return 0 on success, negative error code on failure
 *
 * @note Absolute sleep (to specific time)
 * @warning Only callable from task context
 */
int32_t task_delay_until(uint64_t deadline_ns);

/**
 * @brief Periodic task sleep
 * @param period_ns Period in nanoseconds
 * @param last_wake_time Pointer to last wake time
 *
 * @note Calculates next wake time automatically
 * @note Compensates for execution time drift
 * @warning Only callable from task context
 */
void task_sleep_periodic(uint64_t period_ns, uint64_t *last_wake_time);
```

### 3.5 Task Query API

```c
/**
 * @brief Get current task ID
 * @return Current task ID (>0), 0 if no current task
 */
uint32_t task_get_current(void);

/**
 * @brief Get task priority
 * @param task_id Task ID
 * @return Priority (0-255), or 255 if task not found
 */
uint8_t task_get_priority(uint32_t task_id);

/**
 * @brief Set task priority
 * @param task_id Task ID
 * @param new_priority New priority (0-255)
 * @return 0 on success, negative error code on failure
 *
 * @note May trigger immediate reschedule if priority raised
 */
int32_t task_set_priority(uint32_t task_id, uint8_t new_priority);

/**
 * @brief Get task state
 * @param task_id Task ID
 * @return Task state, or TASK_SUSPENDED if task not found
 */
TaskState_t task_get_state(uint32_t task_id);
```

---

## 4. Algorithm Implementation Details

### 4.1 256-Level Priority Lookup (O(1))

```c
/**
 * @brief Find highest priority in bitmap
 * @param bitmap 256-bit bitmap (array of 4×uint64_t)
 * @return Highest priority (0-255), or 255 if bitmap empty
 *
 * @note Uses ARM64 CLZ instruction for O(1) lookup
 * @note Priority 0 is highest, 255 is lowest
 * @note Caller must hold ready_queue lock
 */
static inline uint8_t find_highest_priority(uint64_t *bitmap)
{
    /* bitmap[0]: priorities 0-63 (most significant) */
    if (bitmap[0] != 0U)
    {
        return (uint8_t)__builtin_clzll(bitmap[0]);
    }

    /* bitmap[1]: priorities 64-127 */
    if (bitmap[1] != 0U)
    {
        return (uint8_t)(64U + __builtin_clzll(bitmap[1]));
    }

    /* bitmap[2]: priorities 128-191 */
    if (bitmap[2] != 0U)
    {
        return (uint8_t)(128U + __builtin_clzll(bitmap[2]));
    }

    /* bitmap[3]: priorities 192-255 (least significant) */
    if (bitmap[3] != 0U)
    {
        return (uint8_t)(192U + __builtin_clzll(bitmap[3]));
    }

    /* No ready tasks */
    return 255U;
}
```

### 4.2 Bitmap Manipulation

```c
/**
 * @brief Set priority bit in bitmap
 * @param bitmap 256-bit bitmap
 * @param priority Priority (0-255)
 *
 * @note Caller must hold ready_queue lock
 */
static inline void bitmap_set(uint64_t *bitmap, uint8_t priority)
{
    uint32_t index = (uint32_t)(priority >> 6U);
    uint64_t mask = 1ULL << (63U - (priority & 0x3FU));
    bitmap[index] |= mask;
}

/**
 * @brief Clear priority bit in bitmap
 * @param bitmap 256-bit bitmap
 * @param priority Priority (0-255)
 *
 * @note Caller must hold ready_queue lock
 */
static inline void bitmap_clear(uint64_t *bitmap, uint8_t priority)
{
    uint32_t index = (uint32_t)(priority >> 6U);
    uint64_t mask = ~(1ULL << (63U - (priority & 0x3FU)));
    bitmap[index] &= mask;
}
```

### 4.3 Task Enqueue/Dequeue

```c
/**
 * @brief Add task to ready queue
 * @param task Task to enqueue
 *
 * @note Caller must hold ready_queue lock
 */
static void task_enqueue(TCB_t *task)
{
    uint32_t cpu_id = task->cpu_affinity;
    PerCPUReadyQueue_t *queue = &g_scheduler.ready_queues[cpu_id];
    uint8_t prio = task->priority;

    /* Add to priority queue tail */
    if (queue->queues[prio].tail == NULL)
    {
        queue->queues[prio].head = task;
        queue->queues[prio].tail = task;
        task->next = NULL;
        task->prev = NULL;
    }
    else
    {
        task->next = NULL;
        task->prev = queue->queues[prio].tail;
        queue->queues[prio].tail->next = task;
        queue->queues[prio].tail = task;
    }

    /* Set bitmap bit */
    bitmap_set(queue->bitmap, prio);
    queue->task_count++;

    /* Update task state */
    task->state = TASK_READY;
}

/**
 * @brief Remove highest priority task from ready queue
 * @param cpu_id CPU ID
 * @return Task pointer, or NULL if queue empty
 *
 * @note Caller must hold ready_queue lock
 */
static TCB_t* task_dequeue(uint32_t cpu_id)
{
    PerCPUReadyQueue_t *queue = &g_scheduler.ready_queues[cpu_id];
    TCB_t *task;

    /* Find highest priority */
    uint8_t prio = find_highest_priority(queue->bitmap);
    if (prio == 255U)
    {
        return NULL;  /* Queue empty */
    }

    /* Remove from queue head */
    task = queue->queues[prio].head;
    if (task == NULL)
    {
        return NULL;
    }

    /* Update list pointers */
    queue->queues[prio].head = task->next;
    if (task->next != NULL)
    {
        task->next->prev = NULL;
    }
    else
    {
        /* Queue empty, clear bitmap bit */
        bitmap_clear(queue->bitmap, prio);
        queue->queues[prio].tail = NULL;
    }

    task->next = NULL;
    task->prev = NULL;
    queue->task_count--;

    return task;
}
```

### 4.4 Core Scheduling Algorithm

```c
/**
 * @brief Core scheduling function
 *
 * @note Called from schedule(), timer ISR, yield()
 * @note Must be called with scheduler lock held
 */
static void schedule_internal(void)
{
    uint32_t cpu_id = get_cpu_id();
    TCB_t *current_task = g_scheduler.current_task[cpu_id];
    TCB_t *next_task;

    /* 1. Wake sleeping tasks (check sleep queue) */
    wake_sleeping_tasks();

    /* 2. Select next task from local ready queue */
    atomic_lock(&g_scheduler.ready_queues[cpu_id].lock);
    next_task = task_dequeue(cpu_id);

    /* 3. Load balancing if no local tasks */
    if (next_task == NULL)
    {
        next_task = steal_task(cpu_id);
    }

    atomic_unlock(&g_scheduler.ready_queues[cpu_id].lock);

    /* 4. No runnable task, run idle task */
    if (next_task == NULL)
    {
        next_task = g_idle_task[cpu_id];
    }

    /* 5. Context switch if needed */
    if (next_task != current_task)
    {
        g_scheduler.task_switches[cpu_id]++;
        context_switch(current_task, next_task);
    }
}
```

### 4.5 Sleep Queue Management

```c
/**
 * @brief Insert task into sleep queue (sorted by deadline)
 * @param task Task to insert
 *
 * @note Sleep queue is ordered ascending by sleep_deadline
 * @note Caller must hold sleep_queue lock
 */
static void sleep_queue_insert(TCB_t *task)
{
    TCB_t *prev = NULL;
    TCB_t *curr = g_scheduler.sleep_queue.head;

    /* Find insertion position (maintain sorted order) */
    while ((curr != NULL) && (curr->sleep_deadline < task->sleep_deadline))
    {
        prev = curr;
        curr = curr->next;
    }

    /* Insert task */
    if (prev == NULL)
    {
        /* Insert at head */
        task->next = g_scheduler.sleep_queue.head;
        task->prev = NULL;
        if (g_scheduler.sleep_queue.head != NULL)
        {
            g_scheduler.sleep_queue.head->prev = task;
        }
        g_scheduler.sleep_queue.head = task;
    }
    else
    {
        /* Insert in middle or at tail */
        task->next = prev->next;
        task->prev = prev;
        if (prev->next != NULL)
        {
            prev->next->prev = task;
        }
        else
        {
            g_scheduler.sleep_queue.tail = task;
        }
        prev->next = task;
    }
}

/**
 * @brief Wake tasks whose sleep deadline has expired
 *
 * @note Called from schedule() and timer ISR
 * @note Moves tasks from sleep queue to ready queue
 */
static void wake_sleeping_tasks(void)
{
    uint64_t current_time = get_system_time_ns();
    TCB_t *task;
    TCB_t *next;

    atomic_lock(&g_scheduler.sleep_queue_lock);

    task = g_scheduler.sleep_queue.head;
    while ((task != NULL) && (task->sleep_deadline <= current_time))
    {
        next = task->next;

        /* Remove from sleep queue */
        if (task->prev != NULL)
        {
            task->prev->next = task->next;
        }
        else
        {
            g_scheduler.sleep_queue.head = task->next;
        }
        if (task->next != NULL)
        {
            task->next->prev = task->prev;
        }
        else
        {
            g_scheduler.sleep_queue.tail = task->prev;
        }

        /* Change state to READY */
        task->state = TASK_READY;

        /* Add to ready queue */
        uint32_t cpu_id = task->cpu_affinity;
        atomic_lock(&g_scheduler.ready_queues[cpu_id].lock);
        task_enqueue(task);
        atomic_unlock(&g_scheduler.ready_queues[cpu_id].lock);

        task = next;
    }

    atomic_unlock(&g_scheduler.sleep_queue_lock);
}
```

### 4.6 Load Balancing Algorithm

```c
/**
 * @brief Steal task from another CPU
 * @param cpu_id Current CPU ID
 * @return Stolen task, or NULL if no stealable task
 *
 * @note Implements work-stealing load balancing
 * @note Prefers stealing from most-loaded CPU
 */
static TCB_t* steal_task(uint32_t cpu_id)
{
    uint32_t src_cpu;
    uint32_t max_load = 0U;
    TCB_t *task = NULL;

    /* Find most-loaded CPU */
    for (uint32_t i = 0U; i < MAX_CPUS; i++)
    {
        if (i == cpu_id)
        {
            continue;
        }

        uint32_t load = g_scheduler.ready_queues[i].task_count;
        if (load > max_load)
        {
            max_load = load;
            src_cpu = i;
        }
    }

    /* Skip if no CPU has significant load */
    if (max_load < 2U)
    {
        return NULL;
    }

    /* Try to steal from source CPU */
    atomic_lock(&g_scheduler.ready_queues[src_cpu].lock);

    /* Find lowest priority task (least important) */
    for (int32_t prio = 255; prio >= 0; prio--)
    {
        TaskList_t *queue = &g_scheduler.ready_queues[src_cpu].queues[prio];
        if (queue->tail != NULL)
        {
            /* Steal from tail (least recently used) */
            task = queue->tail;

            /* Remove from source queue */
            queue->tail = task->prev;
            if (task->prev != NULL)
            {
                task->prev->next = NULL;
            }
            else
            {
                queue->head = NULL;
                bitmap_clear(g_scheduler.ready_queues[src_cpu].bitmap, (uint8_t)prio);
            }

            g_scheduler.ready_queues[src_cpu].task_count--;
            break;
        }
    }

    atomic_unlock(&g_scheduler.ready_queues[src_cpu].lock);

    /* Update task affinity */
    if (task != NULL)
    {
        task->cpu_affinity = cpu_id;
        task->cpu_id = cpu_id;
        task->next = NULL;
        task->prev = NULL;
    }

    return task;
}
```

---

## 5. Performance Requirements

### 5.1 Timing Constraints

| Operation | Maximum Latency |
|-----------|-----------------|
| **Task Creation** | 50 μs |
| **Task Deletion** | 100 μs |
| **Context Switch** | 5 μs |
| **Schedule() Call** | 10 μs |
| **Priority Lookup** | 200 ns (O(1)) |
| **Task Enqueue** | 500 ns |
| **Task Dequeue** | 500 ns |

### 5.2 Memory Constraints

| Resource | Limit |
|----------|-------|
| **TCB Size** | ≤ 1024 bytes |
| **Ready Queue Memory** | ≤ 64 KB (256 queues × 4 CPUs) |
| **Total Scheduler Memory** | ≤ 512 KB |

### 5.3 Scalability

- **Maximum Tasks**: 256
- **Maximum CPUs**: 8
- **Priority Levels**: 256
- **Task Switches/Second**: > 100,000

---

## 6. MISRA-C:2012 Compliance

### 6.1 Compliance Strategy

All scheduler code shall comply with MISRA-C:2012 rules. Key deviations and justifications:

| Rule | Deviation | Justification |
|------|-----------|---------------|
| Rule 11.5 | Void pointer to TCB_t* | Type-safe via opaque pattern |
| Rule 21.1 | Include paths | Resolved via build system |

### 6.2 Static Analysis

- **Tool**: PC-lint Plus / Coverity
- **Compliance Target**: Zero warnings
- **Frequency**: Every commit

### 6.3 Runtime Checks

```c
/* Compile-time assertions */
STATIC_ASSERT(sizeof(TCB_t) <= 1024U, TCB_too_large);
STATIC_ASSERT(256 == MAX_PRIORITY_LEVELS, priority_levels_fixed);

/* Runtime assertions */
ASSERT(task_id < MAX_TASKS);
ASSERT(priority <= 255);
```

---

## 7. Testing Strategy

### 7.1 Unit Tests

| Test Case | Description |
|-----------|-------------|
| **TC-SCH-001** | Basic task creation and deletion |
| **TC-SCH-002** | Priority ordering (0 highest) |
| **TC-SCH-003** | Round-robin within same priority |
| **TC-SCH-004** | Preemption by higher priority |
| **TC-SCH-005** | Task sleep and wake |
| **TC-SCH-006** | Scheduler lock nesting |
| **TC-SCH-007** | CPU affinity |
| **TC-SCH-008** | Load balancing |
| **TC-SCH-009** | Context switch integrity |
| **TC-SCH-010** | Edge cases (256 tasks, 8 CPUs) |

### 7.2 Integration Tests

| Test Case | Description |
|-----------|-------------|
| **TC-SCH-INT-001** | Scheduler with timer module |
| **TC-SCH-INT-002** | Scheduler with sync primitives |
| **TC-SCH-INT-003** | Multi-core stress test |
| **TC-SCH-INT-004** | Priority inversion prevention |

### 7.3 Performance Tests

| Test Case | Metric | Target |
|-----------|--------|--------|
| **TC-SCH-PERF-001** | Context switch time | < 5 μs |
| **TC-SCH-PERF-002** | Schedule latency | < 10 μs |
| **TC-SCH-PERF-003** | Max throughput | > 100k switches/sec |

### 7.4 Coverage Requirements

- **Statement Coverage**: > 95%
- **Branch Coverage**: > 90%
- **MC/DC Coverage**: > 85% (critical functions)

---

## 8. Configuration Options

### 8.1 MenuConfig Options

```kconfig
config SCHEDULER
    bool "Task Scheduler"
    default y

config MAX_TASKS
    int "Maximum number of tasks"
    range 1 256
    default 32
    depends on SCHEDULER

config PRIORITY_LEVELS
    int "Number of priority levels"
    range 1 256
    default 256
    depends on SCHEDULER

config TIME_SLICE_NS
    int "Default time slice (nanoseconds)"
    range 1000 10000000
    default 10000
    depends on SCHEDULER

config LOAD_BALANCE_INTERVAL_MS
    int "Load balance interval (milliseconds)"
    range 1 1000
    default 100
    depends on SCHEDULER && SMP
```

---

## 9. Error Handling

### 9.1 Error Codes

| Error Code | Description |
|------------|-------------|
| `ERROR_INVALID_TASK_ID` | Task ID does not exist |
| `ERROR_INVALID_PRIORITY` | Priority out of range |
| `ERROR_TASK_RUNNING` | Cannot delete running task |
| `ERROR_OUT_OF_MEMORY` | TCB allocation failed |
| `ERROR_STACK_TOO_SMALL` | Stack size below minimum |

### 9.2 Error Recovery

- **Task Creation Failure**: Return 0 (caller checks)
- **Out of Memory**: Trigger system panic (no recovery)
- **Stack Overflow**: Trigger core dump and task restart
- **Context Switch Failure**: Trigger system panic (fatal)

---

## 10. Traceability

### 10.1 Requirements Traceability

| LLD Section | HLD Section | Plan.md Section |
|-------------|-------------|-----------------|
| Data Structures | 4.2.1 TCB | 4.2.1 |
| Scheduling Algorithm | 4.3 Scheduling | 4.3 |
| Context Switch | 4.4 Context Switch | 4.4 |
| Sleep Management | 4.6 Sleep | 4.6 |

### 10.2 Test Coverage Traceability

| Test Case | Requirement |
|-----------|-------------|
| TC-SCH-001 | SCH-001: Task creation |
| TC-SCH-004 | SCH-002: Preemption |
| TC-SCH-PERF-001 | NFR-001: Context switch < 5μs |

---

## Appendix A: Context Switch Assembly

```assembly
/**
 * @file context_switch.S
 * @brief ARM64 context switch implementation
 */

.global context_switch
context_switch:
    /* x0: current TCB
     * x1: current SP
     * x2: next SP
     */

    /* Save current context */
    stp     x29, x30, [x1, #-16]!
    stp     x27, x28, [x1, #-16]!
    stp     x25, x26, [x1, #-16]!
    stp     x23, x24, [x1, #-16]!
    stp     x21, x22, [x1, #-16]!
    stp     x19, x20, [x1, #-16]!

    /* Save SPSR and ELR */
    mrs     x16, spsr_el1
    mrs     x17, elr_el1
    stp     x16, x17, [x1, #-16]!

    /* Save SP and TCB pointer */
    mov     x16, sp
    stp     x16, x0, [x1, #-16]!

    /* Memory barrier */
    dmb     ish

    /* Restore next context */
    ldp     x16, x0, [x2], #16
    mov     sp, x16

    ldp     x16, x17, [x2], #16
    msr     spsr_el1, x16
    msr     elr_el1, x17

    ldp     x19, x20, [x2], #16
    ldp     x21, x22, [x2], #16
    ldp     x23, x24, [x2], #16
    ldp     x25, x26, [x2], #16
    ldp     x27, x28, [x2], #16
    ldp     x29, x30, [x2], #16

    /* Memory barrier */
    dmb     ish

    /* Return to next task */
    ret
```

---

**Document End**
