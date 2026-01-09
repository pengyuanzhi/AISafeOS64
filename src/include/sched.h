/**
 * @file sched.h
 * @brief AISafe64 Scheduler Header
 *
 * @details Multi-core scheduler with scheduling class support
 *
 * @note MISRA-C:2012 compliant
 * @note ISO 26262 ASIL-D compliant
 *
 * @version 1.0
 * @date 2025-01-08
 *
 * @author AISafe64 Team
 */

#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Configuration Constants
 */

#define MAX_CPUS 8U          /**< Maximum number of CPUs */
#define MAX_TASKS 256U       /**< Maximum number of tasks */
#define PRIORITY_LEVELS 256U /**< Number of priority levels (0-255) */

/*
 * Task States
 */

typedef enum
{
    TASK_READY = 0, /**< Task is ready to run */
    TASK_RUNNING,   /**< Task is currently running */
    TASK_BLOCKED,   /**< Task is blocked (waiting for resource) */
    TASK_SLEEPING,  /**< Task is sleeping (timed wait) */
    TASK_ZOMBIE,    /**< Task has exited */
    TASK_STOPPED    /**< Task is stopped */
} TaskState_t;

/*
 * Scheduling Policy
 */

typedef enum
{
    SCHED_FIFO = 0, /**< First-In-First-Out (real-time) */
    SCHED_EDF,      /**< Earliest Deadline First (real-time) */
    SCHED_CFS,      /**< Completely Fair Scheduler */
    SCHED_RR,       /**< Round Robin */
    SCHED_IDLE      /**< Idle scheduler */
} SchedPolicy_t;

/*
 * Scheduling Class Flags
 */

#define SCHED_CLASS_FLAG_REALTIME (0x01U) /**< Real-time scheduler */
#define SCHED_CLASS_FLAG_FAIR (0x02U)     /**< Fair scheduler */
#define SCHED_CLASS_FLAG_IDLE (0x04U)     /**< Idle scheduler */
#define SCHED_CLASS_FLAG_PREEMPT (0x08U)  /**< Supports preemption */

/*
 * Forward Declarations
 */

struct SchedClass;
struct rq;
struct TCB_t;

/*
 * Scheduling Class Interface (Function Pointer Table)
 */

typedef struct SchedClass
{
    const char *name;        /**< Scheduler name (debug) */
    uint32_t priority;       /**< Class priority (lower is higher) */
    uint32_t flags;          /**< Scheduler flags */
    uint32_t id;             /**< Scheduler ID (0-4) */
    struct SchedClass *next; /**< Next scheduler in linked list */

    /*
     * Core Operations (must be implemented)
     */

    /**
     * @brief Initialize run queue
     * @param rq Run queue pointer
     * @return 0 on success, negative error code on failure
     */
    int (*init)(struct rq *rq);

    /**
     * @brief Enqueue task to ready queue
     * @param rq Run queue pointer
     * @param task Task control block pointer
     */
    void (*enqueue)(struct rq *rq, struct TCB_t *task);

    /**
     * @brief Dequeue task from ready queue
     * @param rq Run queue pointer
     * @param task Task control block pointer
     */
    void (*dequeue)(struct rq *rq, struct TCB_t *task);

    /**
     * @brief Pick next task to run
     * @param rq Run queue pointer
     * @return Task control block pointer, NULL if no task
     */
    struct TCB_t *(*pick_next)(struct rq *rq);

    /**
     * @brief Periodic tick handler
     * @param rq Run queue pointer
     * @param task Current running task
     */
    void (*task_tick)(struct rq *rq, struct TCB_t *task);

    /**
     * @brief Update current task runtime
     * @param rq Run queue pointer
     */
    void (*update_curr)(struct rq *rq);

    /*
     * Optional Operations (can be NULL)
     */

    /**
     * @brief Task yield CPU
     * @param rq Run queue pointer
     * @param task Task control block pointer
     */
    void (*yield)(struct rq *rq, struct TCB_t *task);

    /**
     * @brief Check if task can be preempted
     * @param rq Run queue pointer
     * @param task Task control block pointer
     * @return 1 if can preempt, 0 otherwise
     */
    int (*can_preempt)(const struct rq *rq, const TCB_t *task);

    /**
     * @brief Switch to different scheduling class
     * @param rq Run queue pointer
     * @param task Task control block pointer
     * @param new_class New scheduling class
     * @return 0 on success, negative error code on failure
     */
    int (*switch_to)(struct rq *rq, struct TCB_t *task, const struct SchedClass *new_class);

    /**
     * @brief Get scheduler statistics
     * @param rq Run queue pointer
     * @param stats Output: statistics
     * @return 0 on success, negative error code on failure
     */
    int (*get_stats)(const struct rq *rq, void *stats);

} SchedClass_t;

/*
 * Task Control Block (TCB)
 */

typedef struct TCB_t
{
    /* Task identification */
    uint32_t tid;  /**< Task ID */
    char name[32]; /**< Task name */

    /* Task state */
    TaskState_t state;   /**< Task state */
    uint8_t prio;        /**< Priority (0-255, 255 = highest) */
    uint8_t static_prio; /**< Static priority */
    uint8_t normal_prio; /**< Normal priority */

    /* Scheduling class */
    const SchedClass_t *sched_class; /**< Scheduling class */
    struct rq *rq;                   /**< Run queue */

    /* Scheduling data */
    struct list_head run_list; /**< Run list node */
    struct rb_node run_node;   /**< Red-black tree node (EDF/CFS) */

    /* Runtime statistics */
    uint64_t vruntime;         /**< Virtual runtime (CFS) */
    uint64_t exec_start;       /**< Execution start time */
    uint64_t sum_exec_runtime; /**< Total execution time */
    uint64_t deadline;         /**< Absolute deadline (EDF) */
    uint32_t time_slice;       /**< Time slice (RR) */

    /* CPU affinity */
    uint32_t cpu_affinity; /**< CPU affinity mask */

    /* Stack information */
    uint64_t stack_ptr;  /**< Stack pointer */
    uint64_t stack_base; /**< Stack base address */
    uint32_t stack_size; /**< Stack size */

    /* Context */
    uint64_t context[32]; /**< CPU context */

    /* Resources */
    uint32_t capabilities; /**< Capability mask */

    /* List pointers */
    struct list_head tasks;   /**< Tasks list */
    struct list_head rq_list; /**< Run queue list */

    /* Semaphore wait node (embedded, avoids dynamic allocation) */
    struct semaphore_wait_node sem_wait_node; /**< Semaphore wait node */

    /* Parent-child relationship (for resource management) */
    struct TCB_t *parent;      /**< Parent task (NULL = root task) */
    struct list_head children; /**< List of child tasks */
    struct list_head sibling;  /**< Sibling node in parent's children list */
    int exit_code;             /**< Task exit code */
    uint8_t zombie_flag;       /**< Zombie flag (1 = waiting for parent to reap) */

} TCB_t;

/*
 * Run Queue (Per-CPU)
 */

typedef struct rq
{
    /* CPU identification */
    uint32_t cpu; /**< CPU ID */

    /* Lock */
    spinlock_t lock; /**< Run queue lock */

    /* Current task */
    TCB_t *curr; /**< Currently running task */
    TCB_t *idle; /**< Idle task */

    /* Scheduling statistics */
    uint32_t nr_running[5]; /**< Number of running tasks per class */
    uint64_t nr_switches;   /**< Number of context switches */
    uint64_t nr_migrations; /**< Number of task migrations */

    /* Rescheduling flag */
    uint32_t need_resched; /**< Need reschedule flag */

    /* Priority bitmap */
    uint64_t priority_bitmap[4]; /**< 256-bit priority bitmap */

    /* Scheduler-specific data */
    void *fifo_rq; /**< FIFO run queue */
    void *edf_rq;  /**< EDF run queue */
    void *cfs_rq;  /**< CFS run queue */
    void *rr_rq;   /**< RR run queue */
    void *idle_rq; /**< Idle run queue */

    /* Load tracking */
    uint64_t nr_load_updates; /**< Number of load updates */
    uint64_t load_weight;     /**< Load weight */

    /* Time tracking */
    uint64_t idle_time;  /**< Total idle time (ticks) */
    uint64_t total_time; /**< Total time (ticks) */

} rq_t;

/*
 * Scheduler Statistics
 */

typedef struct SchedStats
{
    uint32_t nr_running;    /**< Number of running tasks */
    uint64_t nr_switches;   /**< Number of context switches */
    uint64_t nr_migrations; /**< Number of task migrations */
    uint64_t load_weight;   /**< Load weight */
    uint64_t avg_runtime;   /**< Average runtime */
    uint64_t idle_time;     /**< Total idle time */
    uint64_t total_time;    /**< Total time */
} SchedStats_t;

/*
 * Core Scheduler API
 */

/**
 * @brief Initialize multi-core scheduler
 * @return 0 on success, negative error code on failure
 *
 * @note Must be called once during system initialization
 * @note Must be called before other scheduler functions
 */
int scheduler_init(void);

/**
 * @brief Start scheduler on current CPU
 * @return Never returns (switches to first task)
 */
void scheduler_start(void) __attribute__((noreturn));

/**
 * @brief Pick next task to run
 * @param rq Run queue pointer
 * @return Task control block pointer, NULL if no task
 *
 * @note Core scheduler function
 * @note Calls pick_next for each scheduling class
 */
TCB_t *pick_next_task(struct rq *rq);

/**
 * @brief Enqueue task to ready queue
 * @param task Task control block pointer
 *
 * @note Calls task's scheduling class enqueue function
 * @note Updates priority bitmap
 */
void enqueue_task(TCB_t *task);

/**
 * @brief Dequeue task from ready queue
 * @param task Task control block pointer
 *
 * @note Calls task's scheduling class dequeue function
 * @note Updates priority bitmap
 */
void dequeue_task(TCB_t *task);

/**
 * @brief Context switch to next task
 * @param prev Previous task
 * @param next Next task
 *
 * @note Saves previous task context
 * @note Restores next task context
 */
void context_switch(TCB_t *prev, TCB_t *next);

/**
 * @brief Schedule (pick next task and switch)
 *
 * @note Called when need_resched is set
 * @note Implements the scheduler main loop
 */
void schedule(void);

/**
 * @brief Scheduler tick handler
 * @param cpu CPU ID
 *
 * @note Called every timer tick (1ms)
 * @note Updates current task runtime
 * @note Checks time slice expiration
 */
void scheduler_tick(uint32_t cpu);

/**
 * @brief Yield CPU to other tasks
 *
 * @note Moves current task to end of queue
 * @note Sets need_resched flag
 */
void yield(void);

/**
 * @brief Register scheduling class
 * @param class Scheduling class pointer
 * @return 0 on success, negative error code on failure
 *
 * @note Called during scheduler initialization
 */
int register_sched_class(const SchedClass_t *class);

/*
 * Per-CPU Run Queue Access
 */

/**
 * @brief Get run queue for CPU
 * @param cpu CPU ID
 * @return Run queue pointer
 */
struct rq *cpu_rq(uint32_t cpu);

/**
 * @brief Get current CPU's run queue
 * @return Run queue pointer
 */
struct rq *this_rq(void);

/**
 * @brief Get current CPU ID
 * @return CPU ID
 */
uint32_t smp_processor_id(void);

/*
 * Task Management API
 */

/**
 * @brief Create new task
 * @param name Task name
 * @param prio Priority (0-255)
 * @param stack_size Stack size
 * @param entry Entry point function
 * @param policy Scheduling policy
 * @return Task ID on success, 0 on failure
 */
uint32_t task_create(const char *name, uint8_t prio, uint32_t stack_size, void (*entry)(void),
                     SchedPolicy_t policy);

/**
 * @brief Exit current task
 * @param code Exit code
 */
void task_exit(int code) __attribute__((noreturn));

/**
 * @brief Wait for child task to exit
 * @param child Child task control block pointer
 * @param status Output: child exit code (can be NULL)
 * @return 0 on success, negative error code on failure
 *
 * @details Parent task waits for child to become zombie and reaps it
 *          - Releases child's TCB and stack
 *          - Retrieves child's exit code
 *          - Blocks until child exits
 *
 * @note Parent must not exit before calling task_wait for all children
 */
int task_wait(TCB_t *child, int *status);

/**
 * @brief Wake up blocked task
 * @param task Task control block pointer
 * @return 0 on success, negative error code on failure
 */
int wake_up_task(TCB_t *task);

/**
 * @brief Set task priority
 * @param task Task control block pointer
 * @param prio New priority
 * @return 0 on success, negative error code on failure
 */
int set_task_priority(TCB_t *task, uint8_t prio);

/**
 * @brief Get current task
 * @return Current task's TCB pointer, NULL if not found
 */
TCB_t *get_current_task(void);

/**
 * @brief Put current task to sleep
 * @param ms Sleep time in milliseconds
 * @return 0 on success, negative error code on failure
 *
 * @details Blocks current task for specified time
 *          - Sets task state to TASK_SLEEPING
 *          - Adds task to sleep queue
 *          - Triggers scheduler
 */
int task_sleep(uint64_t ms);

/**
 * @brief Wake up sleeping task (timer callback)
 * @param task Task control block pointer
 *
 * @details Internal function called by timer
 *          - Sets task state to TASK_READY
 *          - Adds task to ready queue
 */
void task_wakeup_timer(TCB_t *task);

/**
 * @brief Check and wake up sleeping tasks
 *
 * @details Called by timer_tick()
 *          - Checks sleep queue for expired tasks
 *          - Wakes up tasks whose sleep time has elapsed
 */
void check_sleeping_tasks(void);

/*
 * Scheduling Class Specific API
 */

/**
 * @brief Switch task to different scheduling class
 * @param task Task control block pointer
 * @param new_policy New scheduling policy
 * @return 0 on success, negative error code on failure
 */
int sched_switch_class(TCB_t *task, SchedPolicy_t new_policy);

/**
 * @brief Get scheduler statistics
 * @param cpu CPU ID
 * @param stats Output: statistics
 * @return 0 on success, negative error code on failure
 */
int sched_get_stats(uint32_t cpu, SchedStats_t *stats);

/*
 * Load Balancing API
 */

/**
 * @brief Trigger load balancing
 * @param cpu CPU ID
 *
 * @note Called periodically or on load imbalance
 */
void load_balance(uint32_t cpu);

/**
 * @brief Migrate task to different CPU
 * @param task Task control block pointer
 * @param dest_cpu Destination CPU ID
 * @return 0 on success, negative error code on failure
 */
int migrate_task(TCB_t *task, uint32_t dest_cpu);

/*
 * Utility Functions
 */

/**
 * @brief Find highest set bit in bitmap
 * @param bitmap 256-bit bitmap (4 x 64-bit words)
 * @return Priority level (0-255), 255 if empty
 */
uint32_t find_highest_priority(const uint64_t *bitmap);

/**
 * @brief Convert priority to weight
 * @param prio Priority level
 * @return Weight value
 */
uint32_t prio_to_weight(uint32_t prio);

/**
 * @brief Get scheduler clock
 * @return Current time in nanoseconds
 */
uint64_t sched_clock(void);

#endif /* SCHED_H */
