## 3. ARM64特定编码规范

### 3.1 数据类型规范

#### 3.1.1 标准数据类型
```c
/* 固定宽度整数类型（必须使用） */
#include <stdint.h>

int8_t    i8;    /* 8位有符号整数 */
uint8_t   u8;    /* 8位无符号整数 */
int16_t   i16;   /* 16位有符号整数 */
uint16_t  u16;   /* 16位无符号整数 */
int32_t   i32;   /* 32位有符号整数 */
uint32_t  u32;   /* 32位无符号整数 */
int64_t   i64;   /* 64位有符号整数 */
uint64_t  u64;   /* 64位无符号整数 */

/* 指针大小整数类型 */
uintptr_t  ptr_value;  /* 可存放指针的无符号整数 */
intptr_t   ptr_signed; /* 可存放指针的有符号整数 */

/* 最大/最小宽度整数类型 */
intmax_t   max_int;
uintmax_t  max_uint;
```

#### 3.1.2 类型定义命名规范
```c
/* 结构体和联合体必须使用typedef */
typedef struct TaskControlBlock TCB_t;
typedef struct Mutex Mutex_t;
typedef struct PageTable PageTable_t;

/* 函数指针类型定义 */
typedef void (*TaskEntry_t)(void);
typedef uint32_t (*ErrorHandler_t)(uint32_t error_code);

/* 枚举类型定义 */
typedef enum 
{
    TASK_READY = 0U,      /* 就绪态：等待CPU调度 */
    TASK_RUNNING,         /* 运行态：正在执行 */
    TASK_BLOCKED,         /* 阻塞态：等待资源（信号量、消息队列） */
    TASK_SLEEPING,        /* 休眠态：延时等待，超时自动唤醒 */
    TASK_SUSPENDED        /* 挂起态：被挂起，需要显式恢复 */
} TaskState_t;
```

### 3.2 对齐规范

#### 3.2.1 数据对齐
```c
/* 16字节对齐（SIMD优化） */
typedef struct 
{
    uint64_t data[2];
} __attribute__((aligned(16))) SIMDData_t;

/* 缓存行对齐（64字节，多核共享数据） */
typedef struct 
{
    atomic_uint64_t lock;
    uint64_t data[7];
} __attribute__((aligned(64))) CacheLine_t;

/* 页对齐（4KB） */
typedef struct 
{
    uint64_t entries[512];
} __attribute__((aligned(4096))) PageTable_t;
```

#### 3.2.2 栈对齐
```c
/* 函数入口必须16字节对齐（ARM64 ABI要求） */
void task_entry(void) 
{
    /* 栈指针保证16字节对齐 */
}

/* 分配栈时确保16字节对齐 */
uint64_t *stack_alloc(uint32_t size) 
{
    uint64_t *stack = malloc(size + 15U);
    if (stack != NULL) 
    {
        stack = (uint64_t *)(((uintptr_t)stack + 15U) & ~0xFU);
    }
    return stack;
}
```

### 3.3 内联汇编规范

#### 3.3.1 基本内联汇编
```c
/* 使用volatile关键字防止优化 */
static inline void memory_barrier(void) 
{
    __asm__ volatile("dmb ish" ::: "memory");
}

/* 带输入输出的内联汇编 */
static inline uint64_t get_cycle_count(void) 
{
    uint64_t count;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(count));
    return count;
}

/* 带约束的内联汇编 */
static inline void set_page_table(uint64_t ttbr0) 
{
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(ttbr0));
    __asm__ volatile("isb");
}
```

#### 3.3.2 内存屏障宏定义
```c
/* 数据同步屏障 */
#define barrier() \
    __asm__ volatile("dmb ish" ::: "memory")

/* 数据同步屏障（轻量级，仅加载） */
#define barrier_load() \
    __asm__ volatile("dmb ishld" ::: "memory")

/* 数据同步屏障（轻量级，仅存储） */
#define barrier_store() \
    __asm__ volatile("dmb ishst" ::: "memory")

/* 指令同步屏障 */
#define barrier_inst() \
    __asm__ volatile("isb")

/* 完整屏障（数据 + 指令） */
#define full_barrier() \
    do { \
        __asm__ volatile("dmb ish" ::: "memory"); \
        __asm__ volatile("isb"); \
    } while (0)
```

### 3.4 原子操作规范

#### 3.4.1 C11原子操作
```c
#include <stdatomic.h>

/* 原子加载 */
static inline uint32_t atomic_load_acquire(atomic_uint *ptr) {
    return atomic_load_explicit(ptr, memory_order_acquire);
}

/* 原子存储 */
static inline void atomic_store_release(atomic_uint *ptr, uint32_t value) {
    atomic_store_explicit(ptr, value, memory_order_release);
}

/* 原子加法（返回旧值） */
static inline uint32_t atomic_fetch_add(atomic_uint *ptr, uint32_t value) {
    return atomic_fetch_add_explicit(ptr, value, memory_order_acq_rel);
}

/* 原子比较交换 */
static inline bool atomic_cas(atomic_uint *ptr,
                               uint32_t *expected,
                               uint32_t desired) {
    return atomic_compare_exchange_strong_explicit(
        ptr, expected, desired,
        memory_order_acq_rel,
        memory_order_acquire
    );
}
```

#### 3.4.2 ARM64特定的原子操作
```c
/* LL/SC（Load-Linked/Store-Conditional）模式 */
static inline uint32_t atomic_inc(volatile uint32_t *ptr) {
    uint32_t old;
    uint32_t new;

    do {
        old = *ptr;
        new = old + 1U;
        __asm__ volatile("": ::: "memory");  /* 编译器屏障 */
    } while (!__builtin_expect(
        __sync_bool_compare_and_swap(ptr, old, new), 1
    ));

    return old;
}

/* ARM64 LDXR/STXR指令 */
static inline bool atomic_cas_arm64(uint64_t *ptr,
                                    uint64_t expected,
                                    uint64_t new) {
    uint64_t tmp;
    int result;

    __asm__ volatile(
        "   ldxr %0, [%2]\n"
        "   cmp %0, %3\n"
        "   b.ne 1f\n"
        "   stxr %w1, %4, [%2]\n"
        "1:\n"
        : "=&r"(tmp), "=&r"(result)
        : "r"(ptr), "r"(expected), "r"(new)
        : "cc", "memory"
    );

    return (result == 0);
}
```

### 3.5 多核编程规范

#### 3.5.1 自旋锁模式
```c
/* Ticket Lock（公平自旋锁） */
typedef struct 
{
    atomic_uint16_t next_ticket;
    atomic_uint16_t serving_ticket;
} TicketLock_t;

static inline void ticket_lock_acquire(TicketLock_t *lock) 
{
    uint16_t my_ticket = atomic_fetch_add(&lock->next_ticket, 1U);

    while (atomic_load(&lock->serving_ticket) != my_ticket) 
    {
        /* 使用wfe指令降低功耗 */
        __asm__ volatile("wfe");
    }

    /* 获取锁后的内存屏障 */
    barrier();
}

static inline void ticket_lock_release(TicketLock_t *lock) 
{
    /* 释放锁前的内存屏障 */
    barrier();
    atomic_fetch_add(&lock->serving_ticket, 1U);
}
```

#### 3.5.2 禁止抢占
```c
/* 禁止调度器（禁止任务切换） */
static inline void scheduler_disable(void) {
    uint32_t cpu_id = get_cpu_id();
    scheduler.lock_count[cpu_id]++;
    barrier();
}

static inline void scheduler_enable(void) {
    uint32_t cpu_id = get_cpu_id();

    scheduler.lock_count[cpu_id]--;
    barrier();

    if (scheduler.lock_count[cpu_id] == 0U) {
        schedule();  /* 触发调度 */
    }
}
```

#### 3.5.3 核心间中断（IPI）
```c
/* IPI类型定义 */
#define IPI_RESCHEDULE   0U
#define IPI_STOP         1U
#define IPI_TIMER        2U
#define IPI_CALL_FUNC    3U

/* 发送IPI */
static inline void ipi_send(uint32_t target_cpu, uint32_t ipi_type) {
    uint32_t cpu_mask = (1U << target_cpu);

    /* 写入GIC SGI寄存器 */
    uint64_t sgi_reg = ((uint64_t)ipi_type << 24U) |
                       ((uint64_t)target_cpu << 16U);

    __asm__ volatile(
        "msr ICC_SGI1R_EL1, %0"
        :: "r"(sgi_reg)
    );
}
```

### 3.6 异常处理规范

#### 3.6.1 异常级别
```c
/* ARMv8异常级别 */
#define EL0    0U   /* User mode */
#define EL1    1U   /* Kernel mode */
#define EL2    2U   /* Hypervisor mode */
#define EL3    3U   /* Secure monitor mode */

/* 获取当前异常级别 */
static inline uint32_t get_current_el(void) {
    uint64_t currentel;
    __asm__ volatile("mrs %0, currentel" : "=r"(currentel));
    return (uint32_t)((currentel >> 2U) & 0x3U);
}

/* 从EL1切换到EL0 */
static inline void drop_to_el0(void) {
    __asm__ volatile(
        "mov x0, #0\n"
        "msr spsr_el1, x0\n"
        "eret"
    );
}
```

#### 3.6.2 异常向量表
```c
/* 异常向量表对齐要求（2KB = 0x800字节） */
typedef void (*ExceptionHandler_t)(void);

__attribute__((aligned(2048))) ExceptionHandler_t exception_table[16] = {
    /* 当前EL，SP0，同步异常 */
    exception_sync_sp0,
    /* 当前EL，SP0，IRQ异常 */
    exception_irq_sp0,
    /* 当前EL，SP0，FIQ异常 */
    exception_fiq_sp0,
    /* 当前EL，SP0，SError异常 */
    exception_serror_sp0,

    /* 当前EL，SPx，同步异常 */
    exception_sync_spx,
    /* 当前EL，SPx，IRQ异常 */
    exception_irq_spx,
    /* 当前EL，SPx，FIQ异常 */
    exception_fiq_spx,
    /* 当前EL，SPx，SError异常 */
    exception_serror_spx,

    /* 低EL（AArch64），同步异常 */
    exception_sync_lower64,
    /* 低EL（AArch64），IRQ异常 */
    exception_irq_lower64,
    /* 低EL（AArch64），FIQ异常 */
    exception_fiq_lower64,
    /* 低EL（AArch64），SError异常 */
    exception_serror_lower64,

    /* 低EL（AArch32），同步异常 */
    exception_sync_lower32,
    /* 低EL（AArch32），IRQ异常 */
    exception_irq_lower32,
    /* 低EL（AArch32），FIQ异常 */
    exception_fiq_lower32,
    /* 低EL（AArch32），SError异常 */
    exception_serror_lower32,
};
```

### 3.7 缓存操作规范

#### 3.7.1 缓存维护
```c
/* 清理数据缓存到内存 */
static inline void dcache_clean(void *addr, uint32_t size) 
{
    uint64_t start = (uint64_t)addr;
    uint64_t end = start + (uint64_t)size;

    start &= ~0x3FULL;  /* 64字节缓存行对齐 */

    while (start < end) 
    {
        __asm__ volatile("dc cvac, %0" :: "r"(start) : "memory");
        start += 64U;
    }
}

/* 使数据缓存无效 */
static inline void dcache_invalidate(void *addr, uint32_t size) 
{
    uint64_t start = (uint64_t)addr;
    uint64_t end = start + (uint64_t)size;

    start &= ~0x3FULL;

    while (start < end) 
    {
        __asm__ volatile("dc ivac, %0" :: "r"(start) : "memory");
        start += 64U;
    }
}

/* 清理并使数据缓存无效 */
static inline void dcache_clean_and_invalidate(void *addr, uint32_t size) 
{
    uint64_t start = (uint64_t)addr;
    uint64_t end = start + (uint64_t)size;

    start &= ~0x3FULL;

    while (start < end) 
    {
        __asm__ volatile("dc civac, %0" :: "r"(start) : "memory");
        start += 64U;
    }
}
```

#### 3.7.2 TLB操作
```c
/* 使TLB项无效（所有地址） */
static inline void tlb_invalidate_all(void) 
{
    __asm__ volatile("tlbi vmalle1is");
    barrier();
}

/* 使TLB项无效（指定地址） */
static inline void tlb_invalidate_page(uint64_t addr) 
{
    uint64_t page = addr >> 12U;
    __asm__ volatile("tlbi vae1is, %0" :: "r"(page));
    barrier();
}

/* 同步TLB操作 */
static inline void tlb_sync(void) 
{
    barrier();
    __asm__ volatile("isb");
}
```

---

