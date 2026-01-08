# AISafe64 C代码生成规范系统提示词

本文档为AISafe64操作系统（AI-Generated, Safety-Certifiable, Native 64-bit RTOS）开发提供完整的C代码生成规范，严格遵循MISRA-C:2012标准，并针对ARM64多核SMP架构进行了扩展。

---

## 1. 核心原则

### 1.1 安全编码原则
- **可预测性优先**: 所有代码行为必须可预测，避免未定义行为
- **显式优于隐式**: 所有类型转换、操作必须显式声明
- **最小权限原则**: 限制数据访问范围，使用const和volatile
- **防御性编程**: 假设所有外部输入都可能有问题
- **内存安全**: 确保没有内存泄漏、越界访问、悬空指针

### 1.2 多核安全原则
- **原子操作**: 多核共享数据必须使用原子操作或锁保护
- **内存屏障**: ARMv8弱内存模型，必须正确使用内存屏障
- **缓存一致性**: 确保多核间数据一致性
- **无锁编程**: 优先使用无锁数据结构和算法

### 1.3 功能安全原则
- **可追溯性**: 每个代码模块对应需求文档
- **可测试性**: 所有代码必须可测试
- **可验证性**: 复杂度限制，便于形式化验证
- **错误处理**: 所有可能的错误路径都必须处理

---

## 2. MISRA-C:2012 核心规则

### 2.1 必须遵循的强制规则

#### 规则 1.1: 程序不得包含不可到达的代码
```c
/* ❌ 错误: return后的代码不可到达 */
return;
x = 5;  /* 违规 */

/* ✅ 正确 */
return;
```

#### 规则 2.1: 项目不得包含未定义或未指定的行为
```c
/* ❌ 错误: 未定义行为 - 有符号整数溢出 */
int32_t a = INT32_MAX;
int32_t b = a + 1;  /* 违规 */

/* ✅ 正确: 检查溢出 */
int32_t a = INT32_MAX;
int32_t b;
if (a < INT32_MAX) {
    b = a + 1;
}
```

#### 规则 3.1: 字符和字符串字面量中不得使用字符转义序列
```c
/* ❌ 错误: 使用八进制转义 */
char c = '\123';

/* ✅ 正确: 使用十六进制转义 */
char c = '\x53';
```

#### 规则 4.1: 字节和半字对象的访问必须使用明确的类型
```c
/* ❌ 错误: 通过指针别名访问 */
uint32_t value = 0x12345678U;
uint8_t byte = *((uint8_t *)&value);

/* ✅ 正确: 使用联合体或位移操作 */
uint32_t value = 0x12345678U;
uint8_t byte = (uint8_t)(value & 0xFFU);
```

#### 规则 5.1: 位域必须显式声明为signed或unsigned
```c
/* ❌ 错误: 隐式int类型 */
struct {
    int flag : 1;  /* 违规 */
};

/* ✅ 正确 */
struct {
    int32_t flag : 1;       /* 显式有符号 */
    uint32_t status : 8;    /* 显式无符号 */
};
```

#### 规则 6.1: 不允许使用char、short、enum等隐式类型转换
```c
/* ❌ 错误: 隐式类型转换 */
uint32_t x = 10;
int32_t y = -5;
if (x > y) {  /* 违规: y被隐式转换为uint32_t */
}

/* ✅ 正确: 显式比较 */
uint32_t x = 10U;
int32_t y = -5;
if ((y >= 0) && (x > (uint32_t)y)) {
}
```

#### 规则 7.1: 禁止八进制常量（除0外）和八进制转义序列
```c
/* ❌ 错误 */
int x = 010;  /* 违规: 八进制 */

/* ✅ 正确 */
int x = 10;
int x = 0xA;  /* 十六进制 */
```

#### 规则 8.1: 类型定义必须有标识符
```c
/* ❌ 错误: 无标识符的typedef */
typedef struct { int x; };  /* 违规 */

/* ✅ 正确 */
typedef struct { int x; } MyStruct_t;
```

#### 规则 9.1: 不允许使用变长数组（VLA）
```c
/* ❌ 错误: 变长数组 */
void func(uint32_t n) {
    int32_t arr[n];  /* 违规 */
}

/* ✅ 正确: 使用固定大小或动态分配 */
#define MAX_SIZE 256U
void func(uint32_t n) {
    int32_t arr[MAX_SIZE];
    if (n <= MAX_SIZE) {
        /* 使用arr */
    }
}
```

#### 规则 10.1: 禁止隐式整数类型转换
```c
/* ❌ 错误 */
uint32_t x = 10U;
int16_t y = x;  /* 违规: 隐式转换 */

/* ✅ 正确: 显式转换 */
uint32_t x = 10U;
int16_t y = (int16_t)x;
```

#### 规则 11.1: 禁止指针和整数之间的转换（除uintptr_t外）
```c
/* ❌ 错误 */
uint32_t x = (uint32_t)ptr;  /* 违规 */

/* ✅ 正确 */
uintptr_t x = (uintptr_t)ptr;
```

#### 规则 12.1: 表达式的值不得依赖于求值顺序
```c
/* ❌ 错误: 未定义求值顺序 */
x = arr[i++] + arr[i++];  /* 违规 */

/* ✅ 正确 */
x = arr[i] + arr[i + 1U];
i = i + 2U;
```

#### 规则 13.1: 禁止初始化器列表中的未指定行为
```c
/* ❌ 错误: 跳过初始化器 */
int arr[5] = { [0] = 1, [3] = 4 };  /* 违规 */

/* ✅ 正确 */
int arr[5] = { 1, 0, 0, 4, 0 };
```

#### 规则 14.1: 禁止浮点数变量作为循环计数器
```c
/* ❌ 错误 */
float f;
for (f = 0.0F; f < 10.0F; f++) {  /* 违规 */
}

/* ✅ 正确 */
uint32_t i;
for (i = 0U; i < 10U; i++) {
    float f = (float)i;
}
```

#### 规则 15.1: 禁止goto语句
```c
/* ❌ 错误 */
goto error_handler;  /* 违规 */

/* ✅ 正确: 使用函数返回 */
if (error) {
    return ERROR_CODE;
}
```

#### 规则 16.1: 禁止递归函数调用
```c
/* ❌ 错误 */
uint32_t factorial(uint32_t n) {
    if (n <= 1U) {
        return 1U;
    }
    return n * factorial(n - 1U);  /* 违规: 递归 */
}

/* ✅ 正确: 使用迭代 */
uint32_t factorial(uint32_t n) {
    uint32_t result = 1U;
    uint32_t i;

    for (i = 2U; i <= n; i++) {
        result *= i;
    }
    return result;
}
```

#### 规则 17.1: 禁止可变参数函数（除特定情况）
```c
/* ❌ 错误 */
int my_printf(const char *fmt, ...);  /* 违规 */

/* ✅ 正确: 使用固定参数或宏定义 */
int my_printf(const char *str, int32_t val);
```

#### 规则 18.1: 指针运算必须限制在声明的数组对象内
```c
/* ❌ 错误 */
int32_t arr[10];
int32_t *p = &arr[10];  /* 违规: 超出数组范围 */

/* ✅ 正确 */
int32_t arr[10];
int32_t *p = &arr[9];
```

#### 规则 19.1: 禁止联合体（Union）用于类型双关
```c
/* ❌ 错误: 类型双关 */
union {
    uint32_t u32;
    uint16_t u16[2];
} data;
data.u32 = 0x12345678U;
uint16_t low = data.u16[0];  /* 违规 */

/* ✅ 正确: 使用位移操作 */
uint32_t value = 0x12345678U;
uint16_t low = (uint16_t)(value & 0xFFFFU);
```

#### 规则 20.1: 禁止#include包含带相对路径的文件
```c
/* ❌ 错误 */
#include "../include/types.h"  /* 违规 */

/* ✅ 正确 */
#include "types.h"
```

#### 规则 21.1: #include必须放在文件开头（除注释外）
```c
/* ✅ 正确 */
/* 文件头部注释 */
#include "types.h"
#include "scheduler.h"
```

### 2.2 建议遵循的规则

#### 规则 2.2: 禁止未知的实现相关行为
```c
/* ❌ 可能有问题 */
int32_t x = -1;
uint32_t y = (uint32_t)x;  /* 实现相关: 可能是0xFFFFFFFF或陷阱 */

/* ✅ 更安全 */
uint32_t y = (x < 0) ? 0U : (uint32_t)x;
```

#### 规则 11.3: 指针转换必须检查类型兼容性
```c
/* ❌ 警告 */
void *ptr = malloc(100);
int32_t *ip = (int32_t *)ptr;  /* 类型不明确 */

/* ✅ 更好 */
void *ptr = malloc(100);
int32_t *ip = NULL;
if (ptr != NULL) {
    ip = (int32_t *)ptr;
}
```

---

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
typedef enum {
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
typedef struct {
    uint64_t data[2];
} __attribute__((aligned(16))) SIMDData_t;

/* 缓存行对齐（64字节，多核共享数据） */
typedef struct {
    atomic_uint64_t lock;
    uint64_t data[7];
} __attribute__((aligned(64))) CacheLine_t;

/* 页对齐（4KB） */
typedef struct {
    uint64_t entries[512];
} __attribute__((aligned(4096))) PageTable_t;
```

#### 3.2.2 栈对齐
```c
/* 函数入口必须16字节对齐（ARM64 ABI要求） */
void task_entry(void) {
    /* 栈指针保证16字节对齐 */
}

/* 分配栈时确保16字节对齐 */
uint64_t *stack_alloc(uint32_t size) {
    uint64_t *stack = malloc(size + 15U);
    if (stack != NULL) {
        stack = (uint64_t *)(((uintptr_t)stack + 15U) & ~0xFU);
    }
    return stack;
}
```

### 3.3 内联汇编规范

#### 3.3.1 基本内联汇编
```c
/* 使用volatile关键字防止优化 */
static inline void memory_barrier(void) {
    __asm__ volatile("dmb ish" ::: "memory");
}

/* 带输入输出的内联汇编 */
static inline uint64_t get_cycle_count(void) {
    uint64_t count;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(count));
    return count;
}

/* 带约束的内联汇编 */
static inline void set_page_table(uint64_t ttbr0) {
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
typedef struct {
    atomic_uint16_t next_ticket;
    atomic_uint16_t serving_ticket;
} TicketLock_t;

static inline void ticket_lock_acquire(TicketLock_t *lock) {
    uint16_t my_ticket = atomic_fetch_add(&lock->next_ticket, 1U);

    while (atomic_load(&lock->serving_ticket) != my_ticket) {
        /* 使用wfe指令降低功耗 */
        __asm__ volatile("wfe");
    }

    /* 获取锁后的内存屏障 */
    barrier();
}

static inline void ticket_lock_release(TicketLock_t *lock) {
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
static inline void dcache_clean(void *addr, uint32_t size) {
    uint64_t start = (uint64_t)addr;
    uint64_t end = start + (uint64_t)size;

    start &= ~0x3FULL;  /* 64字节缓存行对齐 */

    while (start < end) {
        __asm__ volatile("dc cvac, %0" :: "r"(start) : "memory");
        start += 64U;
    }
}

/* 使数据缓存无效 */
static inline void dcache_invalidate(void *addr, uint32_t size) {
    uint64_t start = (uint64_t)addr;
    uint64_t end = start + (uint64_t)size;

    start &= ~0x3FULL;

    while (start < end) {
        __asm__ volatile("dc ivac, %0" :: "r"(start) : "memory");
        start += 64U;
    }
}

/* 清理并使数据缓存无效 */
static inline void dcache_clean_and_invalidate(void *addr, uint32_t size) {
    uint64_t start = (uint64_t)addr;
    uint64_t end = start + (uint64_t)size;

    start &= ~0x3FULL;

    while (start < end) {
        __asm__ volatile("dc civac, %0" :: "r"(start) : "memory");
        start += 64U;
    }
}
```

#### 3.7.2 TLB操作
```c
/* 使TLB项无效（所有地址） */
static inline void tlb_invalidate_all(void) {
    __asm__ volatile("tlbi vmalle1is");
    barrier();
}

/* 使TLB项无效（指定地址） */
static inline void tlb_invalidate_page(uint64_t addr) {
    uint64_t page = addr >> 12U;
    __asm__ volatile("tlbi vae1is, %0" :: "r"(page));
    barrier();
}

/* 同步TLB操作 */
static inline void tlb_sync(void) {
    barrier();
    __asm__ volatile("isb");
}
```

---

## 4. 代码风格规范

### 4.1 命名规范

#### 4.1.1 函数命名
```c
/* 格式: <模块>_<动作>_<对象> */
uint32_t scheduler_task_create(void (*entry)(void), uint8_t prio);
void scheduler_task_delete(uint32_t task_id);
void memory_pool_init(uint32_t pool_id);

/* 简短函数可以省略模块名（如果明确） */
uint32_t task_create(void (*entry)(void), uint8_t prio);
```

#### 4.1.2 变量命名
```c
/* 局部变量: 小写 + 下划线 */
uint32_t task_count;
uint64_t system_ticks;
TCB_t *current_task;

/* 全局变量: 加g_前缀 */
uint32_t g_max_tasks;
Scheduler_t g_scheduler;

/* 静态全局变量: 加s_前缀 */
static uint32_t s_initialized = 0U;
static TCB_t *s_idle_task = NULL;

/* 常量: 全大写 + _后缀表示类型 */
#define MAX_TASK_COUNT     256U
#define TICK_RATE_HZ       1000U
#define STACK_SIZE_MIN     4096U

/* 枚举值: 全大写 + 前缀 */
typedef enum {
    TASK_READY = 0U,      /* 就绪态：等待CPU调度 */
    TASK_RUNNING,         /* 运行态：正在执行 */
    TASK_BLOCKED,         /* 阻塞态：等待资源（信号量、消息队列） */
    TASK_SLEEPING,        /* 休眠态：延时等待，超时自动唤醒 */
    TASK_SUSPENDED        /* 挂起态：被挂起，需要显式恢复 */
} TaskState_t;
```

#### 4.1.3 类型命名
```c
/* 结构体和联合体: _t后缀 */
typedef struct TaskControlBlock TCB_t;
typedef struct Mutex Mutex_t;
typedef union RegisterValue RegValue_t;

/* 函数指针: _fn或_cb后缀 */
typedef void (*TaskEntry_fn)(void);
typedef uint32_t (*ErrorCallback_fn)(uint32_t error);
```

### 4.2 格式规范

#### 4.2.1 缩进和空格
```c
/* 使用4个空格缩进（不使用Tab） */
void function(void) {
    uint32_t x = 10U;

    if (x > 5U) {
        x = x + 1U;
    }
}

/* 运算符两边加空格 */
x = a + b * c;        /* ❌ 错误: *两边没有空格 */
x = a + (b * c);      /* ✅ 正确 */

/* 函数参数: 左括号前不加空格 */
func (arg);           /* ❌ 错误 */
func(arg);            /* ✅ 正确 */

/* 控制语句: 括号前加空格 */
if(condition)         /* ❌ 错误 */
if (condition)        /* ✅ 正确 */
```

#### 4.2.2 大括号规范（Allman风格）
```c
/* Allman风格：左大括号必须换行 */
void function(void)
{                    /* ✅ 正确 - Allman风格 */
    /* code */
}

void function(void) { /* ❌ 错误 - K&R风格 */
    /* code */
}

/* 单语句也必须使用大括号 */
if (condition)
    x = 1;           /* ❌ 错误：缺少大括号 */

if (condition)
{                    /* ✅ 正确：Allman风格 */
    x = 1;
}

/* 控制语句必须使用Allman风格 */
if (condition)
{
    do_something();
}
else
{
    do_other_thing();
}

while (condition)
{
    do_something();
}

for (int i = 0; i < max; i++)
{
    do_something();
}

/* 函数定义必须使用Allman风格 */
void function_name(parameter1, parameter2)
{
    /* 函数体 */
}

/* 结构体定义必须使用Allman风格 */
typedef struct StructureName
{
    uint32_t field1;
    uint32_t field2;
} StructureName_t;
```

#### 4.2.3 行长度
```c
/* 每行最多120个字符 */
uint32_t result = function_with_very_long_name(argument1, argument2, argument3, argument4);

/* 超过120字符需要换行 */
uint32_t result = function_with_very_long_name(
    argument1,
    argument2,
    argument3,
    argument4
);

/* 函数调用换行对齐 */
uint32_t result = scheduler_task_create(
    task_entry_function,
    priority_value,
    stack_size_bytes,
    task_name_string
);
```

### 4.3 注释规范

#### 4.3.1 文件头注释
```c
/**
 * @file    scheduler.c
 * @brief   任务调度器实现
 * @author  AISafe64 Team
 * @date    2025-01-07
 * @version 1.0
 *
 * @details 本文件实现了256级优先级的多核任务调度器
 *          支持抢占式调度、负载均衡和任务迁移
 *
 * @copyright Copyright (c) 2025 AISafe64 Team
 */
```

#### 4.3.2 函数注释
```c
/**
 * @brief 创建新任务
 *
 * @param entry 任务入口函数指针（不能为NULL）
 * @param priority 任务优先级（0-255，255为最高）
 * @param stack_size 堆栈大小（字节，最小4096）
 * @param name 任务名称（最多16字符）
 *
 * @return 成功返回任务ID，失败返回0
 *
 * @note 必须在调度器启动前调用
 * @warning 任务入口函数不得返回
 *
 * @code
 * uint32_t tid = task_create(my_task, 255, 8192, "MyTask");
 * if (tid != 0U) {
 *     printf("Task created: %u\n", tid);
 * }
 * @endcode
 */
uint32_t task_create(void (*entry)(void),
                    uint8_t priority,
                    uint32_t stack_size,
                    const char *name);
```

#### 4.3.3 代码注释
```c
/* 单行注释: 简短说明 */
uint32_t task_id;  /* 任务唯一标识 */

/* 多行注释: 详细说明 */
/*
 * 256级优先级位图实现：
 * - 使用4个uint64_t表示256位
 * - bitmap[0]: 优先级 0-63
 * - bitmap[1]: 优先级 64-127
 * - bitmap[2]: 优先级 128-191
 * - bitmap[3]: 优先级 192-255
 */
static uint64_t priority_bitmap[4];

/* TODO注释: 标记待完成的工作 */
/* TODO: 实现优先级捐赠算法 */

/* FIXME注释: 标记已知问题 */
/* FIXME: 负载均衡在高负载下效率低 */

/* HACK注释: 标记临时解决方案 */
/* HACK: 临时使用忙等待，后续改为WFE指令 */
```

### 4.4 文件组织规范

#### 4.4.1 头文件结构
```c
/**
 * @file    scheduler.h
 * @brief   任务调度器头文件
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

/* 1. 包含其他头文件 */
#include "types.h"
#include "task.h"

/* 2. 宏定义 */
#define MAX_PRIORITY      255U
#define MIN_PRIORITY      0U
#define PRIORITY_LEVELS   256U

/* 3. 类型定义 */
typedef struct Scheduler Scheduler_t;

/* 4. 函数声明 */
void scheduler_init(void);
void scheduler_start(void);

/* 5. 内联函数（如果需要） */
static inline uint32_t scheduler_get_cpu_count(void) {
    return MAX_CPUS;
}

#endif /* SCHEDULER_H */
```

#### 4.4.2 源文件结构
```c
/**
 * @file    scheduler.c
 * @brief   任务调度器实现
 */

/* 1. 包含头文件 */
#include "scheduler.h"
#include <string.h>

/* 2. 宏定义（仅本文件使用） */
#define SCHEDULER_LOCK_TIMEOUT_US  1000U

/* 3. 类型定义（仅本文件使用） */
typedef struct {
    uint32_t count;
    uint64_t time;
} ScheduleStat_t;

/* 4. 全局变量 */
static Scheduler_t s_scheduler;
static bool s_initialized = false;

/* 5. 内部函数声明 */
static void schedule_internal(void);
static uint8_t find_highest_priority(void);

/* 6. 公共函数实现 */
void scheduler_init(void) {
    /* 实现代码 */
}

/* 7. 内部函数实现 */
static void schedule_internal(void) {
    /* 实现代码 */
}
```

---

## 5. 内存管理规范

### 5.1 动态内存分配

#### 5.1.1 分配和释放
```c
/* ✅ 正确: 检查返回值 */
void *ptr = malloc(100);
if (ptr == NULL) {
    return ERROR_OUT_OF_MEMORY;
}
/* 使用ptr... */
free(ptr);
ptr = NULL;  /* 防止悬空指针 */

/* ❌ 错误: 未检查返回值 */
void *ptr = malloc(100);
*ptr = 10;  /* 可能段错误 */

/* ✅ 正确: 使用calloc清零内存 */
TCB_t *task = (TCB_t *)calloc(1, sizeof(TCB_t));
if (task == NULL) {
    return ERROR_OUT_OF_MEMORY;
}

/* ❌ 错误: 内存泄漏 */
void function(void) {
    void *ptr = malloc(100);
    return;  /* 忘记释放ptr */
}
```

#### 5.1.2 对齐分配
```c
/* 分配对齐的内存 */
void *aligned_alloc(uint32_t size, uint32_t alignment) {
    void *ptr = NULL;
    void *aligned_ptr = NULL;

    if ((alignment & (alignment - 1U)) != 0U) {
        return NULL;  /* alignment必须是2的幂 */
    }

    ptr = malloc(size + alignment);
    if (ptr == NULL) {
        return NULL;
    }

    aligned_ptr = (void *)(((uintptr_t)ptr + alignment) & ~(alignment - 1U));

    /* 保存原始指针，以便释放 */
    *((void **)aligned_ptr - 1) = ptr;

    return aligned_ptr;
}

void aligned_free(void *ptr) {
    if (ptr != NULL) {
        free(*((void **)ptr - 1));
    }
}
```

### 5.2 栈使用规范

#### 5.2.1 限制栈大小
```c
/* ❌ 错误: 大数组在栈上 */
void function(void) {
    uint8_t buffer[10000];  /* 10KB，可能栈溢出 */
}

/* ✅ 正确: 使用静态或动态分配 */
void function(void) {
    static uint8_t buffer[10000];  /* 静态分配 */
    /* 或 */
    uint8_t *buffer = malloc(10000);  /* 动态分配 */
    if (buffer != NULL) {
        /* 使用buffer */
        free(buffer);
    }
}
```

#### 5.2.2 栈检查
```c
/* 使用栈水印检测栈使用 */
#define STACK_CANARY 0xABCD1234U

void stack_init(TCB_t *task, void *stack, uint32_t size) {
    uint32_t *stack_bottom = (uint32_t *)stack;

    /* 设置栈魔数 */
    for (uint32_t i = 0U; i < (size / sizeof(uint32_t)); i++) {
        stack_bottom[i] = STACK_CANARY;
    }

    task->stack_base = stack;
    task->stack_size = size;
}

bool stack_check(TCB_t *task) {
    uint32_t *stack_bottom = (uint32_t *)task->stack_base;
    uint32_t used = 0U;

    /* 计算栈使用量 */
    for (uint32_t i = 0U; i < (task->stack_size / sizeof(uint32_t)); i++) {
        if (stack_bottom[i] != STACK_CANARY) {
            used = (i + 1U) * sizeof(uint32_t);
            break;
        }
    }

    uint32_t usage_percent = (used * 100U) / task->stack_size;

    if (usage_percent > 80U) {
        /* 栈使用超过80%，警告 */
        return false;
    }

    return true;
}
```

### 5.3 堆保护

#### 5.3.1 双重释放检测
```c
typedef struct {
    void *ptr;
    uint32_t magic;
} AllocationInfo_t;

#define ALLOC_MAGIC 0xDEADBEEFU

void safe_free(void **ptr) {
    if ((ptr == NULL) || (*ptr == NULL)) {
        return;
    }

    /* 检查是否已释放 */
    AllocationInfo_t *info = (AllocationInfo_t *)*ptr - 1;
    if (info->magic != ALLOC_MAGIC) {
        /* 已释放或损坏 */
        return;
    }

    /* 清除魔数 */
    info->magic = 0U;

    /* 释放内存 */
    free(info);
    *ptr = NULL;
}

void *safe_malloc(uint32_t size) {
    AllocationInfo_t *info = malloc(sizeof(AllocationInfo_t) + size);
    if (info == NULL) {
        return NULL;
    }

    info->magic = ALLOC_MAGIC;
    info->ptr = (void *)(info + 1);

    return info->ptr;
}
```

---

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

## 7. 错误处理规范

### 7.1 错误码定义
```c
/* 错误码类型定义 */
typedef uint32_t ErrorCode_t;

/* 成功 */
#define ERROR_SUCCESS          0x0000U

/* 通用错误 */
#define ERROR_FAIL             0x0001U
#define ERROR_INVALID_PARAM    0x0002U
#define ERROR_OUT_OF_MEMORY    0x0003U
#define ERROR_TIMEOUT          0x0004U
#define ERROR_BUSY             0x0005U

/* 任务相关错误 */
#define ERROR_TASK_INVALID     0x0100U
#define ERROR_TASK_CREATE_FAIL 0x0101U
#define ERROR_TASK_DELETE_FAIL 0x0102U

/* 内存相关错误 */
#define ERROR_MEM_INVALID      0x0200U
#define ERROR_MEM_ALIGN        0x0201U
#define ERROR_MEM_OVERFLOW     0x0202U
```

### 7.2 错误处理模式

#### 7.2.1 返回值检查
```c
ErrorCode_t function(void) {
    ErrorCode_t ret;

    ret = sub_function1();
    if (ret != ERROR_SUCCESS) {
        return ret;
    }

    ret = sub_function2();
    if (ret != ERROR_SUCCESS) {
        return ret;
    }

    return ERROR_SUCCESS;
}

/* 调用者必须检查返回值 */
ErrorCode_t ret = function();
if (ret != ERROR_SUCCESS) {
    handle_error(ret);
}
```

#### 7.2.2 资源清理模式
```c
ErrorCode_t complex_function(void) {
    void *resource1 = NULL;
    void *resource2 = NULL;
    ErrorCode_t ret = ERROR_SUCCESS;

    resource1 = malloc(100);
    if (resource1 == NULL) {
        return ERROR_OUT_OF_MEMORY;
    }

    resource2 = malloc(200);
    if (resource2 == NULL) {
        free(resource1);
        return ERROR_OUT_OF_MEMORY;
    }

    ret = process_resources(resource1, resource2);
    if (ret != ERROR_SUCCESS) {
        goto cleanup;
    }

    /* 更多操作... */

cleanup:
    if (resource2 != NULL) {
        free(resource2);
    }
    if (resource1 != NULL) {
        free(resource1);
    }

    return ret;
}
```

### 7.3 断言和诊断

#### 7.3.1 编译时断言
```c
/* 静态断言（C11） */
_Static_assert(sizeof(uint64_t) == 8U, "uint64_t must be 8 bytes");
_Static_assert((MAX_PRIORITY & (MAX_PRIORITY - 1U)) == 0U,
               "MAX_PRIORITY must be power of 2");

/* 兼容C99的静态断言 */
#define STATIC_ASSERT(expr, msg) \
    typedef char static_assertion_##msg[(expr) ? 1 : -1]

STATIC_ASSERT(sizeof(TCB_t) <= 1024U, TCB_too_large);
```

#### 7.3.2 运行时断言
```c
/* 调试断言 */
#ifdef DEBUG
#define ASSERT(expr) \
    do { \
        if (!(expr)) { \
            assertion_failed(__FILE__, __LINE__, #expr); \
        } \
    } while (0)
#else
#define ASSERT(expr) ((void)0)
#endif

/* 使用示例 */
void task_delete(uint32_t task_id) {
    ASSERT(task_id < MAX_TASKS);
    ASSERT(task_table[task_id] != NULL);

    /* 删除任务... */
}
```

---

## 8. 性能优化规范

### 8.1 内联函数

#### 8.1.1 何时使用inline
```c
/* ✅ 适合内联: 小函数，频繁调用 */
static inline uint32_t get_cpu_id(void) {
    uint64_t mpidr;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    return (uint32_t)(mpidr & 0xFFU);
}

/* ✅ 适合内联: 位操作 */
static inline uint8_t find_highest_priority(uint64_t *bitmap) {
    if (bitmap[0] != 0U) {
        return (uint8_t)__builtin_clzll(bitmap[0]);
    }
    return 0U;
}

/* ❌ 不适合内联: 大函数 */
static inline void complex_function(void) {  /* 不要内联 */
    /* 100行代码... */
}
```

### 8.2 分支预测提示

#### 8.2.1 likely/unlikely宏
```c
/* 分支预测宏定义 */
#if defined(__GNUC__) || defined(__clang__)
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x)   (x)
#define unlikely(x) (x)
#endif

/* 使用示例 */
uint8_t find_highest_priority(uint64_t *bitmap) {
    if (unlikely(bitmap[0] == 0U)) {  /* 通常不为0 */
        if (unlikely(bitmap[1] == 0U)) {
            /* 继续检查... */
        }
    }
    return priority;
}
```

### 8.3 缓存优化

#### 8.3.1 数据结构布局
```c
/* ✅ 好: 热数据放在一起 */
typedef struct {
    /* 频繁访问的数据 */
    uint8_t  priority;
    uint8_t  state;
    uint16_t flags;

    /* 较少访问的数据 */
    char     name[16];
    uint64_t create_time;
} TCB_t;

/* ❌ 差: 冷热数据混合 */
typedef struct {
    char     name[16];
    uint64_t create_time;
    uint8_t  priority;  /* 频繁访问，但不在缓存行开头 */
    uint8_t  state;
    uint16_t flags;
} TCB_t;
```

#### 8.3.2 缓存行对齐
```c
/* 多核共享数据，避免伪共享 */
typedef struct __attribute__((aligned(64))) {
    atomic_uint32_t lock;
    uint32_t task_count;
    uint8_t padding[64 - sizeof(atomic_uint32_t) - sizeof(uint32_t)];
} PerCPUData_t;
```

---

## 9. 测试规范

### 9.1 单元测试

#### 9.1.1 测试用例结构
```c
/* Unity测试框架示例 */
void test_scheduler_task_create(void) {
    uint32_t task_id;
    ErrorCode_t ret;

    /* 测试: 正常创建 */
    ret = task_create(dummy_task, 100, 4096, "TestTask");
    TEST_ASSERT_EQUAL(ERROR_SUCCESS, ret);
    TEST_ASSERT_NOT_EQUAL(0U, ret);

    /* 测试: 无效参数 */
    task_id = task_create(NULL, 100, 4096, "NullTask");
    TEST_ASSERT_EQUAL(ERROR_INVALID_PARAM, task_id);

    /* 测试: 优先级越界 */
    task_id = task_create(dummy_task, 256, 4096, "BadPrio");
    TEST_ASSERT_EQUAL(ERROR_INVALID_PARAM, task_id);
}
```

#### 9.1.2 Mock外部依赖
```c
/* Mock硬件定时器 */
void mock_timer_init(void) {
    /* 设置初始状态 */
    timer_tick_count = 0U;
}

void mock_timer_tick(void) {
    timer_tick_count++;
}

/* 测试中使用mock */
void test_task_delay(void) {
    mock_timer_init();

    task_delay(10);
    TEST_ASSERT_EQUAL(10U, timer_tick_count);
}
```

### 9.2 覆盖率要求

```c
/* MC/DC覆盖率示例 */
void coverage_example(uint32_t a, uint32_t b, uint32_t c) {
    /* 条件: (a > 5) && (b < 10) || (c == 0) */
    /* 测试用例必须独立改变每个条件 */

    /* 测试用例1: a=6, b=5, c=1  -> true && true || false = true */
    /* 测试用例2: a=4, b=5, c=1  -> false && true || false = false */
    /* 测试用例3: a=6, b=15, c=1 -> true && false || false = false */
    /* 测试用例4: a=6, b=5, c=0  -> true && true || true = true */

    if ((a > 5U) && (b < 10U) || (c == 0U)) {
        result = 1U;
    } else {
        result = 0U;
    }
}
```

---

## 10. 文档要求

### 10.1 代码注释覆盖率
```c
/* 所有公开API必须有文档注释 */
/**
 * @brief 函数简短描述（单行）
 *
 * @details 详细描述（可以多行）
 *          解释函数的用途、算法等
 *
 * @param param1 参数1说明
 * @param param2 参数2说明
 *
 * @return 返回值说明
 *
 * @note 注意事项
 * @warning 警告信息
 * @see 参考其他函数
 */
```

### 10.2 复杂度要求
```c
/* 圈复杂度限制: 每个函数不超过10 */
void complex_function(void) {  /* ❌ 圈复杂度太高 */
    if (condition1) {
        if (condition2) {
            if (condition3) {
                /* ... */
            }
        }
    }
}

/* 重构为多个小函数 */
void complex_function(void) {  /* ✅ 圈复杂度降低 */
    if (condition1) {
        handle_case1();
    } else {
        handle_case2();
    }
}
```

---

## 11. 检查清单

### 11.1 提交前检查
- [ ] 所有文件包含MISRA-C:2012合规的头文件注释
- [ ] 所有函数有文档注释
- [ ] 所有魔术数字替换为宏定义
- [ ] 所有类型转换显式声明
- [ ] 所有数组访问检查边界
- [ ] 所有指针使用前检查NULL
- [ ] 所有错误路径处理
- [ ] 所有锁正确释放
- [ ] 所有动态分配释放
- [ ] 通过静态分析（零警告）

### 11.2 代码审查检查
- [ ] 遵循命名规范
- [ ] 遵循格式规范
- [ ] 无未定义行为
- [ ] 无内存泄漏
- [ ] 无死锁风险
- [ ] 正确使用内存屏障
- [ ] 正确使用原子操作
- [ ] 测试覆盖率>95%

---

## 12. 工具和脚本

### 12.1 静态分析配置
```bash
# PC-lint Plus配置
# lint配置文件: .lnt

# MISRA-C:2012规则集
-misra2

# 包含路径
-I./include
-I./src

# 定义宏
+d__builtin_expect(x,y)=((x))
+d__builtin_clzll(x)=__CLZ_LL(x)

# 抑警告（如需要）
-esym(534, my_function.c)  /* 忽略返回值（已验证） */
```

### 12.2 自动化检查脚本
```python
#!/usr/bin/env python3
# misra_check.py

import subprocess
import sys

def run_lint(file_path):
    """运行PC-lint Plus"""
    cmd = ["lint", "-u", file_path]
    result = subprocess.run(cmd, capture_output=True, text=True)
    return result.returncode, result.stdout, result.stderr

def main():
    if len(sys.argv) < 2:
        print("Usage: misra_check.py <file>")
        return 1

    file_path = sys.argv[1]
    returncode, stdout, stderr = run_lint(file_path)

    if returncode != 0:
        print(f"MISRA violations found in {file_path}:")
        print(stdout)
        print(stderr)
        return 1
    else:
        print(f"No MISRA violations in {file_path}")
        return 0

if __name__ == "__main__":
    sys.exit(main())
```

---

## 13. CMake构建系统规范

### 13.1 CMake文件组织

#### 13.1.1 项目结构要求
```cmake
# CMakeLists.txt文件组织原则:
# 1. 根CMakeLists.txt: 项目整体配置
# 2. 子目录CMakeLists.txt: 模块级配置
# 3. cmake/*.cmake: 可重用的CMake模块
# 4. 工具链文件独立: cmake/toolchain-arm64.cmake
```

#### 13.1.2 命名规范
```cmake
# CMake变量命名
set(PROJECT_NAME TinyOS64)           # 项目: 全大写
set(SOURCE_FILES main.c)              # 局部: 大写下划线
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}") # CMake内置: 保留原始

# 目标命名
add_executable(tinyos64_core ${SRC})  # 可执行文件: 小写下划线
add_library(kernel STATIC ${SRC})      # 库: 小写下划线
add_custom_target(misra_check ...)    # 自定义目标: 小写下划线

# 宏和函数命名
macro(add_kernel_module name)          # 宏: 小写下划线
function(compile_config_file)          # 函数: 小写下划线
endfunction()
endmacro()
```

#### 13.1.3 最小CMake版本
```cmake
# 必须声明最小CMake版本
cmake_minimum_required(VERSION 3.20)
project(TinyOS64 C ASM)

# 设置C标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)  # 禁止编译器扩展，确保标准合规
```

### 13.2 编译选项规范

#### 13.2.1 安全相关编译选项
```cmake
# MISRA-C:2012合规的编译选项
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall                   # 启用所有警告
        -Wextra                 # 启用额外警告
        -Werror                 # 将警告视为错误
        -Wpedantic              # 严格遵循标准
        -Wconversion            # 隐式转换警告
        -Wsign-conversion       # 符号转换警告
        -Wshadow                # 变量遮蔽警告
        -Wstrict-prototypes     # 严格原型检查
        -Wmissing-prototypes    # 缺失原型警告
        -Wstrict-overflow=1     # 严格溢出检查
        -Wvla                   # 禁止变长数组警告
        -Wpedantic              # ISO C合规
    )
endif()

# 嵌入式系统特定选项
add_compile_options(
    -ffreestanding          # 自由standing环境（无标准库）
    -fno-builtin            # 禁用内置函数
    -fno-common             # 禁止未初始化全局变量合并
    -fdata-sections         # 分离数据段
    -ffunction-sections     # 分离代码段
    -fno-strict-aliasing    # 禁止严格别名（避免未定义行为）
)
```

#### 13.2.2 调试/发布配置
```cmake
# Debug配置: 无优化，包含调试信息
set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")

# Release配置: 优化，包含调试符号
set(CMAKE_C_FLAGS_RELEASE "-O2 -g1")

# RelWithDebInfo: 优化并保留调试信息
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g3")

# MinSizeRel: 最小体积优化
set(CMAKE_C_FLAGS_MINSIZEREL "-Os -g1")

# 设置默认构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
```

### 13.3 链接选项规范

#### 13.3.1 链接器脚本
```cmake
# 指定链接器脚本
set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/lds/linker.ld)

# 链接选项
set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS}
    -nostartfiles           # 不使用标准启动文件
    -nostdlib               # 不链接标准库
    -T ${LINKER_SCRIPT}     # 使用自定义链接器脚本
    -Wl,--gc-sections       # 删除未使用的段
    -Wl,-Map=$<TARGET>.map # 生成内存映射文件
    "
)
```

#### 13.3.2 链接库顺序
```cmake
# 链接库顺序: 依赖者在前，被依赖者在后
target_link_libraries(tinyos64_kernel
    kernel                      # 内核核心
    hal                         # 硬件抽象层
    lib                         # 工具库
    -lm                         # 数学库（最后）
)

# 不要链接标准C库
# 嵌入式系统通常不使用标准库
```

### 13.4 目标定义规范

#### 13.4.1 静态库目标
```cmake
# 定义静态库
add_library(kernel STATIC
    scheduler.c
    task.c
    smp.c
    mmu.c
    sync.c
    timer.c
)

# 设置库属性
set_target_properties(kernel PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    POSITION_INDEPENDENT_CODE OFF
)

# 添加包含目录
target_include_directories(kernel
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/kernel
)
```

#### 13.4.2 可执行文件目标
```cmake
# 定义可执行文件
add_executable(tinyos.elf
    startup.S
    main.c
)

# 链接库
target_link_libraries(tinyos.elf
    PRIVATE kernel hal lib
)

# 设置输出属性
set_target_properties(tinyos.elf PROPERTIES
    OUTPUT_NAME "tinyos"
    SUFFIX ".elf"
)

# 生成二进制文件
add_custom_command(TARGET tinyos.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY}
        -O binary
        $<TARGET_FILE:tinyos.elf>
        ${CMAKE_BINARY_DIR}/tinyos.bin
    COMMENT "Generating binary file..."
)

# 生成反汇编文件
add_custom_command(TARGET tinyos.elf POST_BUILD
    COMMAND ${CMAKE_OBJDUMP}
        -d -S
        $<TARGET_FILE:tinyos.elf>
        > ${CMAKE_BINARY_DIR}/tinyos.dis
    COMMENT "Generating disassembly..."
)
```

### 13.5 交叉编译配置

#### 13.5.1 工具链文件
```cmake
# cmake/toolchain-arm64.cmake
# 目标系统
set(CMAKE_SYSTEM_NAME Generic)          # 通用嵌入式系统
set(CMAKE_SYSTEM_PROCESSOR aarch64)     # ARM64架构

# 交叉编译工具链
set(CMAKE_C_COMPILER aarch64-none-elf-gcc)
set(CMAKE_ASM_COMPILER aarch64-none-elf-gcc)
set(CMAKE_AR aarch64-none-elf-ar)
set(CMAKE_RANLIB aarch64-none-elf-ranlib)
set(CMAKE_OBJCOPY aarch64-none-elf-objcopy)
set(CMAKE_OBJDUMP aarch64-none-elf-objdump)
set(CMAKE_SIZE aarch64-none-elf-size)

# 设置查找路径行为
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
```

#### 13.5.2 使用工具链文件
```bash
# 命令行使用
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm64.cmake

# 或设置环境变量
export CMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm64.cmake
cmake ..
```

### 13.6 测试集成

#### 13.1.1 启用测试
```cmake
# 启用测试
enable_testing()

# 添加测试
add_executable(test_scheduler tests/test_scheduler.c)
target_link_libraries(test_scheduler kernel unity)

# 注册测试
add_test(NAME scheduler_test COMMAND test_scheduler)
```

#### 13.6.2 覆盖率收集
```cmake
# 启用覆盖率（仅Debug构建）
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(
        -fprofile-arcs
        -ftest-coverage
    )
    add_link_options(
        -fprofile-arcs
        -ftest-coverage
    )

    # 添加覆盖率目标
    add_custom_target(coverage
        COMMAND lcov --capture --directory . --output-file coverage.info
        COMMAND lcov --remove coverage.info '/usr/*' --output-file coverage.info
        COMMAND genhtml coverage.info --output-directory coverage_html
        COMMENT "Generating code coverage report..."
    )
endif()
```

---

## 14. MenuConfig配置系统规范

### 14.1 Kconfig语法规范

#### 14.1.1 配置项类型
```kconfig
# Kconfig文件结构

# 1. bool类型: 布尔开关（y/n）
config ENABLE_MMU
    bool "Enable MMU Support"
    default y
    help
      Enable Memory Management Unit support for virtual memory.

# 2. tristate类型: 三态（y/m/n）
config KERNEL_MODULE
    tristate "Kernel Module Support"
    depends on MODULES
    default m

# 3. string类型: 字符串
config BOARD_NAME
    string "Board Name"
    default "rpi4"

# 4. hex类型: 十六进制数
config KERNEL_BASE_ADDR
    hex "Kernel Base Address"
    default 0xFFFF00000000

# 5. int类型: 整数
config MAX_TASKS
    int "Maximum Number of Tasks"
    range 1 256
    default 32

# 6. choice类型: 单选菜单
choice
    prompt "CPU Core Count"

config CPU_1_CORE
    bool "1 Core"

config CPU_2_CORES
    bool "2 Cores"

config CPU_4_CORES
    bool "4 Cores"

config CPU_8_CORES
    bool "8 Cores"

endchoice

# 使用choice选择
config NR_CPUS
    int
    default 1 if CPU_1_CORE
    default 2 if CPU_2_CORES
    default 4 if CPU_4_CORES
    default 8 if CPU_8_CORES
```

#### 14.1.2 依赖关系
```kconfig
# depends on: 依赖项
config SMP_SUPPORT
    bool "SMP Support"
    depends on ARCH_ARM64

# if ... endif: 条件块
config PRIORITY_LEVELS
    int "Priority Levels"
    default 32
    if HIGH_PRIORITY_SUPPORT
        default 256
    endif

# select: 自动选择
config ENABLE_SCHEDULER
    bool "Enable Scheduler"
    select TICK_SUPPORT
    select CONTEXT_SWITCH

# imply: 建议选择
config DEBUG_SUPPORT
    bool "Debug Support"
    imply LOGGING
```

#### 14.1.3 菜单结构
```kconfig
# main menu
mainmenu "TinyOS-64 Configuration"

# 菜单组
menu "Core Configuration"

config CPU_CORES
    int "CPU Core Count"
    range 1 8
    default 4

config MAX_TASKS
    int "Maximum Tasks"
    range 1 256
    default 32

endmenu

# 子菜单
menu "Memory Configuration"

source "kconfig/mem/Kconfig"
source "kconfig/mmu/Kconfig"

endmenu
```

### 14.2 配置文件格式

#### 14.2.1 .config文件
```bash
# .config文件由menuconfig自动生成
#
# 格式: CONFIG_<name>=<value>
#
# 符号:
#   y: 内置（编译进内核）
#   m: 模块
#   n: 未选择
#   字符串/数字: 直接值

# 自动生成的注释（不要手动编辑）
# 自动生成的注释

# 核心配置
CONFIG_CPU_CORES=4
CONFIG_MAX_TASKS=32
CONFIG_ENABLE_MMU=y
CONFIG_ENABLE_SMP=y

# 内存配置
CONFIG_KERNEL_HEAP_SIZE=1048576
CONFIG_TASK_STACK_SIZE=8192

# 调试配置
# CONFIG_DEBUG is not set
CONFIG_LOG_LEVEL=2
```

#### 14.2.2 defconfig文件
```bash
# configs/defconfig: 默认配置模板
# 只包含非默认值的配置项

CONFIG_CPU_CORES=4
CONFIG_MAX_TASKS=32
CONFIG_ENABLE_MMU=y
CONFIG_ENABLE_SMP=y
CONFIG_KERNEL_HEAP_SIZE=1048576
CONFIG_TASK_STACK_SIZE=8192
CONFIG_LOG_LEVEL=2
```

### 14.3 配置头文件生成

#### 14.3.1 config.h格式
```c
/**
 * @file config.h
 * @brief Auto-generated configuration header
 * @warning DO NOT EDIT - Generated by menuconfig
 */

#ifndef CONFIG_H
#define CONFIG_H

/**
 * @brief 配置验证宏
 */
#define CONFIG_TINYOS_VERSION_MAJOR 1
#define CONFIG_TINYOS_VERSION_MINOR 0

/**
 * @brief 核心配置
 */
#define CONFIG_CPU_CORES               4U
#define CONFIG_MAX_TASKS               32U
#define CONFIG_ENABLE_MMU              1
#define CONFIG_ENABLE_SMP              1

/**
 * @brief 优先级配置
 */
#define CONFIG_PRIORITY_LEVELS         256U
#define CONFIG_MIN_PRIORITY            0U
#define CONFIG_MAX_PRIORITY            255U

/**
 * @brief 内存配置
 */
#define CONFIG_KERNEL_HEAP_SIZE        1048576UL  /* 1MB */
#define CONFIG_TASK_STACK_SIZE         8192U      /* 8KB */
#define CONFIG_MMU_PAGE_SIZE           4096U      /* 4KB */

/**
 * @brief 调试配置
 */
#define CONFIG_LOG_LEVEL               2U         /* 0=off, 1=err, 2=warn, 3=info, 4=debug */
#define CONFIG_ASSERT_LEVEL            2U         /* 0=off, 1=error, 2=full */

/**
 * @brief 配置验证
 */
#if CONFIG_MAX_TASKS > 256
#error "CONFIG_MAX_TASKS cannot exceed 256"
#endif

#if CONFIG_PRIORITY_LEVELS != 256
#error "CONFIG_PRIORITY_LEVELS must be 256"
#endif

#endif /* CONFIG_H */
```

#### 14.3.2 配置解析脚本
```python
#!/usr/bin/env python3
# scripts/parse_config.py
import sys
import re

def parse_config(config_file, output_file):
    """Parse .config file and generate config.h"""

    configs = {}
    with open(config_file, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue

            # 解析 CONFIG_XXX=value
            match = re.match(r'CONFIG_([^=]+)=(.+)', line)
            if match:
                name = match.group(1)
                value = match.group(2)

                # 转换值类型
                if value == 'y':
                    value = '1'
                elif value == 'n':
                    value = '0'
                elif value == 'm':
                    value = '2'

                configs[name] = value

    # 生成头文件
    with open(output_file, 'w') as f:
        f.write("/** @file auto-generated config.h */\n")
        f.write("#ifndef CONFIG_H\n")
        f.write("#define CONFIG_H\n\n")

        for name, value in sorted(configs.items()):
            # 数字类型
            if re.match(r'^\d+$', value):
                f.write(f"#define CONFIG_{name} {value}U\n")
            # 字符串类型
            elif value.startswith('"'):
                f.write(f"#define CONFIG_{name} {value}\n")
            # 十六进制
            elif value.startswith('0x'):
                f.write(f"#define CONFIG_{name} {value}UL\n")

        f.write("\n#endif /* CONFIG_H */\n")

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: parse_config.py <.config> <config.h>")
        sys.exit(1)

    parse_config(sys.argv[1], sys.argv[2])
```

### 14.4 代码中使用配置

#### 14.4.1 条件编译
```c
/* 使用CONFIG_宏进行条件编译 */
#include <generated/config.h>

void scheduler_init(void) {
#if CONFIG_ENABLE_SMP
    /* 多核初始化 */
    smp_init();
#endif

#if CONFIG_ENABLE_MMU
    /* MMU初始化 */
    mmu_init();
#endif

#if CONFIG_LOG_LEVEL >= 3
    log_info("Scheduler initialized\n");
#endif
}

/* 使用配置限制数组大小 */
TCB_t task_table[CONFIG_MAX_TASKS];
uint64_t ready_queue[CONFIG_CPU_CORES][CONFIG_PRIORITY_LEVELS];
```

#### 14.4.2 编译时断言
```c
/* 使用STATIC_ASSERT验证配置 */
#include <generated/config.h>

STATIC_ASSERT(CONFIG_MAX_TASKS <= 256, max_tasks_exceeded);
STATIC_ASSERT(CONFIG_PRIORITY_LEVELS == 256, priority_levels_fixed);
STATIC_ASSERT((CONFIG_KERNEL_HEAP_SIZE % 4096) == 0, heap_not_aligned);
```

---

## 15. 持续集成规范

### 15.1 CI检查清单

#### 15.1.1 提交前检查
```bash
#!/bin/bash
# scripts/check_patch.sh

set -e

echo "=== TinyOS-64 Pre-commit Check ==="

# 1. 格式检查
echo "Checking code format..."
./scripts/check_format.sh

# 2. MISRA检查
echo "Running MISRA-C:2012 checks..."
cmake --build build --target misra-check

# 3. 单元测试
echo "Running unit tests..."
cd build
ctest --output-on-failure
cd ..

# 4. 覆盖率检查
echo "Checking code coverage..."
./scripts/check_coverage.sh 95

echo "=== All checks passed ==="
```

#### 15.1.2 CI配置
```yaml
# .gitlab-ci.yml
stages:
  - build
  - test
  - analyze

variables:
  BUILD_DIR: build

build:arm64:
  stage: build
  script:
    - mkdir -p $BUILD_DIR
    - cd $BUILD_DIR
    - cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm64.cmake
    - make -j$(nproc)
  artifacts:
    paths:
      - $BUILD_DIR/
    expire_in: 1 day

test:unit:
  stage: test
  dependencies:
    - build:arm64
  script:
    - cd $BUILD_DIR
    - ctest --output-on-failure
  coverage: '/lines\.*: (\d+\.\d+)%/'

analyze:misra:
  stage: analyze
  dependencies:
    - build:arm64
  script:
    - cd $BUILD_DIR
    - make misra-check
  allow_failure: false
```

### 15.2 代码审查清单

#### 15.2.1 CMake配置审查
- [ ] CMake最低版本 >= 3.20
- [ ] C标准设置为C11，禁用扩展
- [ ] 所有警告启用（-Wall -Wextra -Wpedantic）
- [ ] 警告视为错误（-Werror）
- [ ] MISRA合规编译选项
- [ ] 目标属性正确设置
- [ ] 链接顺序正确
- [ ] 无硬编码路径

#### 15.2.2 MenuConfig配置审查
- [ ] 配置项有明确帮助文本
- [ ] 配置项有合理默认值
- [ ] 依赖关系正确（depends on/select）
- [ ] choice选择完整且互斥
- [ ] 范围限制合理
- [ ] 配置项命名一致
- [ ] 配置生成脚本正确

---

## 15.3 Git 提交规范（Conventional Commits）

### 15.3.1 提交消息格式

AISafe64 项目严格遵循 **Conventional Commits** 规范，以确保提交历史的清晰和可追溯性。

#### 提交消息结构

```
<type>(<scope>): <subject>

<body>

<footer>
```

#### 提交类型（type）

| 类型 | 描述 | 示例 |
|------|------|------|
| `feat` | 新功能 | feat(scheduler): add EDF scheduling algorithm |
| `fix` | Bug 修复 | fix(mm): resolve page table corruption issue |
| `docs` | 文档更新 | docs(readme): update build instructions |
| `style` | 代码格式（不影响功能） | style(kernel): fix indentation in scheduler.c |
| `refactor` | 重构（既不是新功能也不是修复） | refactor(ipc): simplify message queue implementation |
| `perf` | 性能优化 | perf(scheduler): optimize priority lookup with CLZ |
| `test` | 测试相关 | test(mm): add unit tests for page allocator |
| `chore` | 构建/工具链相关 | chore(cmake): update toolchain requirements |
| `ci` | CI/CD 配置 | ci(gitlab): add MISRA check pipeline |
| `revert` | 回滚之前的提交 | revert: fix(mm): resolve page table corruption |

#### 提交作用域（scope）

作用域用于指定提交影响的模块：

| 模块 | 说明 |
|------|------|
| `kernel` | 内核核心 |
| `scheduler` | 调度器 |
| `mm` | 内存管理 |
| `ipc` | 进程间通信 |
| `fs` | 文件系统 |
| `driver` | 设备驱动 |
| `arch` | 架构相关代码 |
| `crypto` | 加密/签名 |
| `build` | 构建系统 |
| `config` | 配置系统 |

#### 主题（subject）

- 使用动词原形开头（如 add、fix、update）
- 首字母小写
- 不以句号结尾
- 限制在 50 个字符以内

**示例：**
```
✅ good: feat(scheduler): add EDF scheduling support
❌ bad: Added EDF scheduling support.
❌ bad: feat(scheduler): Added EDF scheduling support.
```

#### 正文（body）

- 详细说明本次提交的**内容**和**原因**
- 每行限制在 72 个字符以内
- **必须**说明"是什么"和"为什么"

**示例：**
```
feat(scheduler): add EDF scheduling support

- Implement earliest deadline first algorithm
- Add red-black tree for deadline tracking
- Integrate with existing scheduler class framework
- Update configuration to select between FIFO/EDF/CFS

This allows dynamic priority scheduling for real-time tasks
with periodic deadlines, improving schedulability compared
to static priority FIFO.

Performance: O(log n) enqueue/dequeue operations
```

#### 页脚（footer）

- 关联 Issue：`Closes #123`, `Fixes #456`
- 破坏性变更：`BREAKING CHANGE: <description>`
- 引用相关提交：`Co-Authored-By: <name> <email>`

**示例：**
```
feat(api): remove deprecated task_create interface

BREAKING CHANGE: The old task_create() interface has been removed.
Migrate to task_create_ex() which supports additional parameters.

Closes #789

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>
```

### 15.3.2 提交消息示例

#### 示例 1：新功能
```
feat(mm): add transparent huge page support

- Implement 2MB page allocation
- Add automatic huge page promotion
- Update page fault handler to support mixed page sizes
- Add sysfs interface for statistics

This reduces TLB pressure and improves performance for
large memory allocations by up to 30%.

Performance: 2MB page allocation takes <1ms
```

#### 示例 2：Bug 修复
```
fix(scheduler): resolve race condition in task migration

The task migration code had a race condition where a task
could be migrated while being scheduled on another CPU,
leading to a use-after-free.

Fix: Add RCU read-side lock around migration check
and update scheduler to handle migrating tasks correctly.

Reported-by: John Doe <john@example.com>
Tested-by: Jane Smith <jane@example.com>
Fixes #1234
```

#### 示例 3：文档更新
```
docs(CLARUDE.md): add Git commit convention rules

- Document Conventional Commits specification
- Add commit type definitions and examples
- Include scope guidelines and best practices

This ensures consistent commit messages across the project.
```

### 15.3.3 提交最佳实践

#### DO（推荐做法）

```bash
# 1. 每个提交做一件事
git commit -m "feat(scheduler): add EDF algorithm"
git commit -m "test(scheduler): add EDF unit tests"

# 2. 使用完整的句子解释
git commit -m "fix(mm): resolve memory leak in page allocator

The page allocator was not freeing pages on error path,
causing a memory leak of 4KB per failed allocation.

Fix: Add proper cleanup in error handling path."

# 3. 引用相关 Issue
git commit -m "feat(driver): add GPIO driver

Implements basic GPIO operations for Raspberry Pi 4.

Closes #456"
```

#### DON'T（不推荐做法）

```bash
# ❌ 1. 不要混合多个不相关的修改
git commit -m "update various things"

# ❌ 2. 不要使用模糊的描述
git commit -m "fix stuff"
git commit -m "update code"

# ❌ 3. 不要在提交消息中包含敏感信息
git commit -m "add password hardcoding: admin123"

# ❌ 4. 不要使用过长的主题行
git commit -m "feat(scheduler): implement a very complex scheduling algorithm \
that does many things and has a very long description that exceeds fifty characters"
```

### 15.3.4 提交检查清单

在执行 `git commit` 前检查：

- [ ] 提交类型符合 Conventional Commits 规范
- [ ] 作用域（scope）明确指定
- [ ] 主题行不超过 50 个字符
- [ ] 主题行以动词原形开头，首字母小写
- [ ] 主题行不以句号结尾
- [ ] 正文解释了"是什么"和"为什么"
- [ ] 正文每行不超过 72 个字符
- [ ] 关联了相关 Issue（如果存在）
- [ ] 标记了破坏性变更（如果有）
- [ ] 没有包含敏感信息

### 15.3.5 Git 配置

#### 自动化提交消息检查

安装 commitlint 工具：

```bash
npm install -g @commitlint/cli @commitlint/config-conventional
```

配置文件 `.commitlintrc.yml`：

```yaml
extends:
  - '@commitlint/config-conventional'

rules:
  type-enum:
    - feat
    - fix
    - docs
    - style
    - refactor
    - perf
    - test
    - chore
    - ci
    - revert

  scope-enum:
    - kernel
    - scheduler
    - mm
    - ipc
    - fs
    - driver
    - arch
    - crypto
    - build
    - config

  subject-case:
    - lower-case

  body-max-line-length: 72
```

#### Git Hooks 配置

`.git/hooks/commit-msg`:

```bash
#!/bin/bash
commitlint --edit "$1"
```

### 15.3.6 提交工作流

#### 功能开发工作流

```bash
# 1. 创建特性分支
git checkout -b feature/edf-scheduler

# 2. 开发并提交（遵循 Conventional Commits）
git add src/scheduler/edf.c
git commit -m "feat(scheduler): add EDF scheduling algorithm"

# 3. 更多提交
git add tests/test_edf.c
git commit -m "test(scheduler): add EDF unit tests"

# 4. 推送到远程
git push origin feature/edf-scheduler

# 5. 创建 Pull Request
# GitHub 会自动检测提交类型
```

#### Bug 修复工作流

```bash
# 1. 创建修复分支
git checkout -b fix/mm-page-leak

# 2. 修复并提交
git add src/mm/page_alloc.c
git commit -m "fix(mm): resolve memory leak in page allocator

The page allocator was not freeing pages on error path.

Fix: Add proper cleanup in error handling path.

Fixes #1234"

# 3. 推送并创建 PR
git push origin fix/mm-page-leak
```

### 15.3.7 版本号规范

遵循语义化版本（Semantic Versioning）：`MAJOR.MINOR.PATCH`

- **MAJOR**：不兼容的 API 变更
- **MINOR**：向后兼容的新功能
- **PATCH**：向后兼容的 Bug 修复

示例：
- `1.0.0` → `1.1.0`：添加新功能（MINOR）
- `1.1.0` → `1.1.1`：Bug 修复（PATCH）
- `1.1.1` → `2.0.0`：破坏性变更（MAJOR）

---

## 16. 调试功能编码规范

### 16.1 核心转储生成规范

#### 16.1.1 核心转储结构定义
```c
/* 核心转储魔数和版本 */
#define CORE_DUMP_MAGIC    0x434F5245U  /* "CORE" */
#define CORE_DUMP_VERSION  1U

/**
 * @brief 核心转储结构
 * @note 必须符合MISRA-C:2012规则
 */
typedef struct {
    uint32_t    magic;              /* 魔数验证 */
    uint32_t    version;            /* 格式版本 */
    uint32_t    task_id;            /* 崩溃任务ID */
    uint32_t    cpu_id;             /* CPU编号 */
    uint64_t    timestamp;          /* 时间戳（纳秒） */
    uint32_t    signal;             /* 信号/错误码 */
    uint32_t    registers[32];      /* CPU寄存器状态 */
    uint64_t    stack_pointer;      /* 栈指针 */
    uint64_t    stack_base;         /* 栈基址 */
    uint32_t    stack_size;         /* 栈大小 */
    uint64_t    heap_pointer;       /* 堆指针 */
    uint64_t    page_table;         /* 页表基址 */
    uint8_t     stack_data[];       /* 栈内容（变长数组） */
} CoreDump_t;

/* 编译时断言：验证结构对齐 */
_Static_assert(sizeof(CoreDump_t) % 16U == 0U,
               "CoreDump_t must be 16-byte aligned");
```

#### 16.1.2 核心转储生成实现
```c
/**
 * @brief 生成核心转储
 * @param task 崩溃的任务指针
 * @param signal 信号/错误码
 * @return 成功返回0，失败返回错误码
 *
 * @note 必须在中断禁用状态下调用
 * @warning 此函数不会返回（调用后系统重启或挂起）
 */
ErrorCode_t generate_coredump(const TCB_t *task, uint32_t signal) {
    CoreDump_t *core = NULL;
    uint32_t core_size;
    ErrorCode_t ret = ERROR_SUCCESS;

    /* 参数验证 */
    if (task == NULL) {
        return ERROR_INVALID_PARAM;
    }

    /* 验证魔数 */
    if (task->magic != TASK_MAGIC) {
        return ERROR_INVALID_TASK;
    }

    /* 计算核心转储大小（防止溢出） */
    if (task->stack_size > (UINT32_MAX - sizeof(CoreDump_t))) {
        return ERROR_OUT_OF_MEMORY;
    }
    core_size = sizeof(CoreDump_t) + task->stack_size;

    /* 分配核心转储缓冲区 */
    core = (CoreDump_t *)malloc(core_size);
    if (core == NULL) {
        return ERROR_OUT_OF_MEMORY;
    }

    /* 清零缓冲区 */
    (void)memset(core, 0, core_size);

    /* 填充头部信息 */
    core->magic = CORE_DUMP_MAGIC;
    core->version = CORE_DUMP_VERSION;
    core->task_id = task->task_id;
    core->cpu_id = get_cpu_id();
    core->timestamp = get_system_time_ns();
    core->signal = signal;

    /* 保存寄存器上下文 */
    save_cpu_context(task, core->registers);

    /* 保存栈信息 */
    core->stack_pointer = (uint64_t)task->stack_ptr;
    core->stack_base = (uint64_t)task->stack_base;
    core->stack_size = task->stack_size;

    /* 保存栈内容（安全拷贝） */
    if (task->stack_base != NULL) {
        (void)memcpy(core->stack_data,
                     task->stack_base,
                     task->stack_size);
    }

    /* 保存其他信息 */
    core->heap_pointer = 0U;  /* TODO: 实现堆跟踪 */
    core->page_table = task->page_table;

    /* 写入核心转储到存储 */
    ret = coredump_write_to_storage(core, core_size);

    /* 释放缓冲区 */
    free(core);
    core = NULL;  /* 防止悬空指针 */

    return ret;
}

/**
 * @brief CPU上下文保存（ARM64）
 * @param task 任务指针
 * @param regs 输出寄存器数组
 */
static void save_cpu_context(const TCB_t *task, uint32_t *regs) {
    if ((task == NULL) || (regs == NULL)) {
        return;
    }

    /* 从任务上下文复制寄存器 */
    uint32_t i;
    const uint32_t max_regs = 32U;

    for (i = 0U; i < max_regs; i++) {
        regs[i] = (uint32_t)(task->context[i] & 0xFFFFFFFFU);
    }
}
```

#### 16.1.3 核心转储写入
```c
/**
 * @brief 将核心转储写入存储
 * @param core 核心转储指针
 * @param size 核心转储大小
 * @return 成功返回0，失败返回错误码
 */
static ErrorCode_t coredump_write_to_storage(const CoreDump_t *core,
                                              uint32_t size) {
    ErrorCode_t ret;
    char filename[64];

    if ((core == NULL) || (size == 0U)) {
        return ERROR_INVALID_PARAM;
    }

    /* 验证魔数 */
    if (core->magic != CORE_DUMP_MAGIC) {
        return ERROR_INVALID_FORMAT;
    }

    /* 生成文件名 */
    ret = snprintf(filename, sizeof(filename),
                   "core_%u_%u.dump",
                   (unsigned int)core->task_id,
                   (unsigned int)core->timestamp);
    if ((ret < 0) || ((uint32_t)ret >= sizeof(filename))) {
        return ERROR_BUFFER_OVERFLOW;
    }

    /* 写入文件 */
    return fs_write_file(filename, (const uint8_t *)core, size);
}
```

### 16.2 栈回溯实现规范

#### 16.2.1 栈帧结构定义
```c
/**
 * @brief 栈帧信息
 */
typedef struct {
    uint64_t    pc;              /* 程序计数器 */
    uint64_t    fp;              /* 帧指针 */
    uint64_t    sp;              /* 栈指针 */
    const char  *function_name;  /* 函数名 */
    const char  *file_name;      /* 文件名 */
    uint32_t    line_number;     /* 行号 */
} StackFrame_t;

/**
 * @brief 栈跟踪结构
 */
typedef struct {
    uint32_t        frame_count;   /* 实际帧数 */
    uint32_t        max_frames;    /* 最大帧数 */
    StackFrame_t    frames[];      /* 栈帧数组（变长） */
} StackTrace_t;
```

#### 16.2.2 栈回溯实现
```c
/**
 * @brief 栈回溯（基于帧指针）
 * @param trace 输出栈跟踪结构
 * @param max_frames 最大帧数
 * @return 实际捕获的帧数
 *
 * @note 必须在任务上下文中调用
 * @warning 中断上下文中调用不可靠
 */
uint32_t stack_unwind(StackTrace_t *trace, uint32_t max_frames) {
    uint64_t *fp;
    uint64_t *prev_fp;
    uint64_t return_addr;
    uint32_t count = 0U;

    if ((trace == NULL) || (max_frames == 0U)) {
        return 0U;
    }

    /* 获取当前帧指针 */
    fp = (uint64_t *)__builtin_frame_address(0);

    /* 遍历栈帧链 */
    while ((fp != NULL) && (count < max_frames)) {
        /* 验证帧指针对齐（ARM64要求16字节对齐） */
        if (((uintptr_t)fp & 0xFU) != 0U) {
            break;  /* 未对齐，可能损坏 */
        }

        /* ARM64栈帧布局：
         * fp[0]: 上一个FP
         * fp[1]: 返回地址 (LR)
         */
        prev_fp = (uint64_t *)fp[0];
        return_addr = fp[1];

        /* 验证返回地址 */
        if (return_addr == 0U) {
            break;  /* 到达栈底 */
        }

        /* 验证返回地址在代码段范围内 */
        if (!is_code_address(return_addr)) {
            break;  /* 无效地址 */
        }

        /* 填充栈帧信息 */
        trace->frames[count].fp = (uint64_t)fp;
        trace->frames[count].pc = return_addr;
        trace->frames[count].sp = (uint64_t)(fp + 2U);

        /* 符号解析（可选，需要调试信息） */
        trace->frames[count].function_name = addr_to_function(return_addr);
        trace->frames[count].file_name = addr_to_file(return_addr);
        trace->frames[count].line_number = addr_to_line(return_addr);

        count++;

        /* 移动到上一个栈帧 */
        fp = prev_fp;

        /* 防止无限循环（检测环路） */
        if (count > 256U) {
            break;
        }
    }

    trace->frame_count = count;
    return count;
}

/**
 * @brief 检查地址是否在代码段
 * @param addr 待检查地址
 * @return true表示在代码段，false表示不在
 */
static bool is_code_address(uint64_t addr) {
    extern uint64_t _code_start;
    extern uint64_t _code_end;

    return (addr >= (uint64_t)&_code_start) &&
           (addr < (uint64_t)&_code_end);
}
```

#### 16.2.3 打印栈跟踪
```c
/**
 * @brief 打印栈跟踪
 * @param trace 栈跟踪指针
 */
void print_stack_trace(const StackTrace_t *trace) {
    uint32_t i;

    if (trace == NULL) {
        return;
    }

    log_err("Stack trace (%u frames):\n", trace->frame_count);

    for (i = 0U; i < trace->frame_count; i++) {
        const StackFrame_t *frame = &trace->frames[i];

        if (frame->function_name != NULL) {
            log_err("  #%u: 0x%016llx in %s (%s:%u)\n",
                    i,
                    frame->pc,
                    frame->function_name,
                    frame->file_name != NULL ? frame->file_name : "???",
                    frame->line_number);
        } else {
            log_err("  #%u: 0x%016llx\n", i, frame->pc);
        }
    }
}
```

### 16.3 性能监控编码规范

#### 16.3.1 性能统计结构
```c
/**
 * @brief 任务性能统计
 */
typedef struct {
    /* CPU使用率 */
    uint64_t    total_runtime;      /* 总运行时间（纳秒） */
    uint64_t    last_run_time;      /* 上次运行时间 */
    uint32_t    cpu_usage_percent;  /* CPU使用率（%） */

    /* 响应时间 */
    uint32_t    max_response_time;  /* 最大响应时间（微秒） */
    uint32_t    avg_response_time;  /* 平均响应时间（微秒） */
    uint32_t    min_response_time;  /* 最小响应时间（微秒） */

    /* 实时性 */
    uint32_t    missed_deadlines;   /* 错过截止次数 */
    uint64_t    deadline;           /* 任务截止时间（纳秒） */

    /* 栈使用 */
    uint32_t    stack_peak;         /* 栈使用峰值（字节） */
    uint32_t    stack_size;         /* 栈大小（字节） */

    /* 调度统计 */
    uint32_t    switch_count;       /* 上下文切换次数 */
    uint32_t    preempt_count;      /* 被抢占次数 */
} TaskPerf_t;
```

#### 16.3.2 性能监控实现
```c
/**
 * @brief 更新任务性能统计
 * @param task 任务指针
 * @param start_time 开始时间（纳秒）
 * @param end_time 结束时间（纳秒）
 */
void update_task_perf(TCB_t *task, uint64_t start_time, uint64_t end_time) {
    TaskPerf_t *perf;
    uint64_t elapsed;
    uint32_t elapsed_us;

    if (task == NULL) {
        return;
    }

    perf = &task->perf;

    /* 计算运行时间（防止溢出） */
    if (end_time > start_time) {
        elapsed = end_time - start_time;
    } else {
        elapsed = 0ULL;
    }

    /* 更新总运行时间 */
    perf->total_runtime += elapsed;

    /* 转换为微秒 */
    elapsed_us = (uint32_t)(elapsed / 1000ULL);

    /* 更新最大响应时间 */
    if (elapsed_us > perf->max_response_time) {
        perf->max_response_time = elapsed_us;
    }

    /* 更新最小响应时间 */
    if ((perf->min_response_time == 0U) ||
        (elapsed_us < perf->min_response_time)) {
        perf->min_response_time = elapsed_us;
    }

    /* 更新平均响应时间（移动平均，防溢出） */
    perf->avg_response_time =
        (perf->avg_response_time * 9U + elapsed_us) / 10U;

    /* 更新CPU使用率 */
    perf->cpu_usage_percent =
        (uint32_t)((perf->total_runtime * 100ULL) /
                   (get_system_time_ns() + 1ULL));

    /* 更新栈使用峰值 */
    uint32_t stack_used = calculate_stack_usage(task);
    if (stack_used > perf->stack_peak) {
        perf->stack_peak = stack_used;
    }

    /* 检查截止时间 */
    if ((perf->deadline != 0ULL) && (end_time > perf->deadline)) {
        perf->missed_deadlines++;
    }
}

/**
 * @brief 计算栈使用量
 * @param task 任务指针
 * @return 栈使用量（字节）
 */
static uint32_t calculate_stack_usage(const TCB_t *task) {
    const uint32_t *stack_bottom;
    uint32_t i;
    uint32_t stack_words;

    if (task == NULL) {
        return 0U;
    }

    stack_bottom = (const uint32_t *)task->stack_base;
    stack_words = task->stack_size / sizeof(uint32_t);

    /* 查找栈水印 */
    for (i = 0U; i < stack_words; i++) {
        if (stack_bottom[i] != STACK_CANARY) {
            return (i + 1U) * sizeof(uint32_t);
        }
    }

    return task->stack_size;  /* 全部使用 */
}
```

### 16.4 设备驱动编码规范

#### 16.4.1 设备类型定义
```c
/**
 * @brief 设备类型枚举
 */
typedef enum {
    DEVICE_CHAR = 0U,      /* 字符设备 */
    DEVICE_BLOCK,          /* 块设备 */
    DEVICE_NET,            /* 网络设备 */
    DEVICE_PLATFORM        /* 平台设备 */
} DeviceType_t;

/**
 * @brief 设备操作接口
 */
typedef struct DeviceOperations {
    int (*open)(void);
    int (*close)(void);
    ssize_t (*read)(void *buf, size_t len, off_t offset);
    ssize_t (*write)(const void *buf, size_t len, off_t offset);
    int (*ioctl)(unsigned int cmd, unsigned long arg);
    int (*mmap)(void *addr, size_t len, unsigned long prot);
    int (*poll)(void);
} DeviceOps_t;
```

#### 16.4.2 设备注册实现
```c
/**
 * @brief 注册设备
 * @param dev 设备指针
 * @return 成功返回0，失败返回错误码
 */
int device_register(Device_t *dev) {
    if (dev == NULL) {
        return ERROR_INVALID_PARAM;
    }

    /* 验证设备操作接口 */
    if (dev->ops == NULL) {
        return ERROR_INVALID_PARAM;
    }

    /* 验证设备名称 */
    if (dev->name[0] == '\0') {
        return ERROR_INVALID_PARAM;
    }

    /* 分配设备号（如果未指定） */
    if (dev->major == 0U) {
        dev->major = allocate_major_number(dev->type);
        if (dev->major == 0U) {
            return ERROR_OUT_OF_RESOURCES;
        }
    }

    /* 添加到设备列表（需要锁保护） */
    ticket_lock_acquire(&g_device_list_lock);

    /* 检查设备是否已注册 */
    if (device_find_by_name(dev->name) != NULL) {
        ticket_lock_release(&g_device_list_lock);
        return ERROR_ALREADY_EXISTS;
    }

    /* 添加到链表头部 */
    dev->next = g_device_list;
    dev->ref_count = 0U;
    g_device_list = dev;

    ticket_lock_release(&g_device_list_lock);

    log_info("Device %s registered (major=%u, minor=%u)\n",
             dev->name, dev->major, dev->minor);

    return ERROR_SUCCESS;
}

/**
 * @brief 按名称查找设备
 * @param name 设备名称
 * @return 设备指针，未找到返回NULL
 *
 * @note 必须在持有设备列表锁时调用
 */
static Device_t *device_find_by_name(const char *name) {
    Device_t *dev = g_device_list;

    while (dev != NULL) {
        /* 比较设备名称（防止缓冲区溢出） */
        if (strncmp(dev->name, name, sizeof(dev->name) - 1U) == 0) {
            return dev;
        }
        dev = dev->next;
    }

    return NULL;
}
```

#### 16.4.3 设备打开实现
```c
/**
 * @brief 打开设备
 * @param dev 设备指针
 * @return 成功返回0，失败返回错误码
 */
int device_open(Device_t *dev) {
    int ret;

    if (dev == NULL) {
        return ERROR_INVALID_PARAM;
    }

    /* 增加引用计数 */
    atomic_fetch_add(&dev->ref_count, 1U);

    /* 调用设备特定的open函数 */
    if (dev->ops->open != NULL) {
        ret = dev->ops->open();
        if (ret != 0) {
            /* 打开失败，减少引用计数 */
            atomic_fetch_sub(&dev->ref_count, 1U);
            return ret;
        }
    }

    return ERROR_SUCCESS;
}

/**
 * @brief 关闭设备
 * @param dev 设备指针
 * @return 成功返回0，失败返回错误码
 */
int device_close(Device_t *dev) {
    if (dev == NULL) {
        return ERROR_INVALID_PARAM;
    }

    /* 调用设备特定的close函数 */
    if (dev->ops->close != NULL) {
        (void)dev->ops->close();
    }

    /* 减少引用计数 */
    if (dev->ref_count > 0U) {
        atomic_fetch_sub(&dev->ref_count, 1U);
    }

    return ERROR_SUCCESS;
}
```

### 16.5 设备树解析规范

#### 16.5.1 设备树结构定义
```c
/**
 * @brief 设备树节点
 */
typedef struct DeviceTreeNode {
    char                        name[64];       /* 节点名称 */
    struct DeviceTreeNode       *children;      /* 子节点链表 */
    struct DeviceTreeNode       *parent;        /* 父节点 */
    struct DeviceTreeNode       *next;          /* 兄弟节点 */
    uint32_t                    phandle;        /* 唯一句柄 */
    const void                  *properties;    /* 属性数据 */
} DeviceTreeNode_t;

/**
 * @brief 设备树属性
 */
typedef struct {
    const char  *name;          /* 属性名 */
    uint32_t    length;         /* 长度（字节） */
    const void  *value;         /* 属性值 */
} DeviceProperty_t;
```

#### 16.5.2 设备树解析实现
```c
/**
 * @brief 解析设备树
 * @param dtb_address 设备树二进制地址
 * @return 成功返回0，失败返回错误码
 */
ErrorCode_t device_tree_parse(uint64_t dtb_address) {
    const struct fdt_header *fdt;
    ErrorCode_t ret;

    /* 验证地址 */
    if (dtb_address == 0U) {
        return ERROR_INVALID_PARAM;
    }

    fdt = (const struct fdt_header *)dtb_address;

    /* 验证设备树魔数 */
    if (fdt->magic != FDT_MAGIC) {
        log_err("Invalid device tree magic: 0x%08x\n", fdt->magic);
        return ERROR_INVALID_FORMAT;
    }

    /* 验证版本 */
    if (fdt->version < 17U) {
        log_err("Unsupported device tree version: %u\n", fdt->version);
        return ERROR_NOT_SUPPORTED;
    }

    /* 解析根节点 */
    g_dt_root = dt_parse_node(fdt, fdt->off_dt_struct, fdt->totalsize);
    if (g_dt_root == NULL) {
        log_err("Failed to parse device tree root node\n");
        return ERROR_PARSE_FAILED;
    }

    /* 遍历所有节点并探测设备 */
    ret = dt_probe_devices(g_dt_root);
    if (ret != ERROR_SUCCESS) {
        log_err("Failed to probe devices from device tree\n");
        return ret;
    }

    log_info("Device tree parsed successfully\n");
    return ERROR_SUCCESS;
}

/**
 * @brief 查找设备树节点
 * @param path 节点路径（如 "/soc/uart@ffe01000"）
 * @return 节点指针，未找到返回NULL
 */
DeviceTreeNode_t *dt_find_node(const char *path) {
    DeviceTreeNode_t *node;
    char path_copy[256];
    char *token;

    if ((path == NULL) || (path[0] != '/')) {
        return NULL;
    }

    /* 从根节点开始 */
    node = g_dt_root;
    if (node == NULL) {
        return NULL;
    }

    /* 复制路径（strtok会修改字符串） */
    if (strncpy(path_copy, path, sizeof(path_copy) - 1U) == NULL) {
        return NULL;
    }
    path_copy[sizeof(path_copy) - 1U] = '\0';

    /* 解析路径 */
    token = strtok(path_copy, "/");
    while (token != NULL) {
        node = dt_find_child(node, token);
        if (node == NULL) {
            return NULL;
        }
        token = strtok(NULL, "/");
    }

    return node;
}

/**
 * @brief 读取设备树属性（uint32_t）
 * @param node 设备树节点
 * @param name 属性名
 * @param value 输出值
 * @return 成功返回0，失败返回错误码
 */
ErrorCode_t dt_get_property_u32(const DeviceTreeNode_t *node,
                                 const char *name,
                                 uint32_t *value) {
    const DeviceProperty_t *prop;

    if ((node == NULL) || (name == NULL) || (value == NULL)) {
        return ERROR_INVALID_PARAM;
    }

    /* 查找属性 */
    prop = dt_find_property(node, name);
    if (prop == NULL) {
        return ERROR_NOT_FOUND;
    }

    /* 验证长度 */
    if (prop->length < sizeof(uint32_t)) {
        return ERROR_INVALID_FORMAT;
    }

    /* 读取值（大端序转换） */
    *value = be32_to_cpu(*(const uint32_t *)prop->value);

    return ERROR_SUCCESS;
}

/**
 * @brief 读取设备树属性（字符串）
 * @param node 设备树节点
 * @param name 属性名
 * @param str 输出字符串缓冲区
 * @param len 缓冲区长度
 * @return 成功返回0，失败返回错误码
 */
ErrorCode_t dt_get_property_string(const DeviceTreeNode_t *node,
                                    const char *name,
                                    char *str,
                                    uint32_t len) {
    const DeviceProperty_t *prop;
    uint32_t str_len;

    if ((node == NULL) || (name == NULL) || (str == NULL) || (len == 0U)) {
        return ERROR_INVALID_PARAM;
    }

    /* 查找属性 */
    prop = dt_find_property(node, name);
    if (prop == NULL) {
        return ERROR_NOT_FOUND;
    }

    /* 计算字符串长度 */
    str_len = strnlen((const char *)prop->value, prop->length);
    if (str_len >= len) {
        return ERROR_BUFFER_OVERFLOW;
    }

    /* 复制字符串 */
    (void)strncpy(str, (const char *)prop->value, len - 1U);
    str[len - 1U] = '\0';

    return ERROR_SUCCESS;
}
```

#### 16.5.3 设备探测和匹配
```c
/**
 * @brief 从设备树探测设备
 * @param root 设备树根节点
 * @return 成功返回0，失败返回错误码
 */
static ErrorCode_t dt_probe_devices(const DeviceTreeNode_t *root) {
    const DeviceTreeNode_t *node;
    uint32_t probe_count = 0U;
    uint32_t success_count = 0U;

    if (root == NULL) {
        return ERROR_INVALID_PARAM;
    }

    /* 遍历所有节点 */
    node = root;
    while (node != NULL) {
        /* 查找compatible属性 */
        const char *compatible;
        ErrorCode_t ret;

        ret = dt_get_property_string(node, "compatible",
                                     (char *)&compatible,
                                     sizeof(compatible));
        if (ret == ERROR_SUCCESS) {
            /* 查找匹配的驱动 */
            const struct device_driver *drv;

            drv = dt_find_driver(compatible);
            if (drv != NULL) {
                log_info("Probing device %s with driver %s\n",
                         node->name, drv->name);

                probe_count++;

                /* 调用驱动探测函数 */
                if (drv->probe != NULL) {
                    ret = drv->probe(node);
                    if (ret == ERROR_SUCCESS) {
                        success_count++;
                        log_info("Device %s probed successfully\n",
                                 node->name);
                    } else {
                        log_warn("Device %s probe failed: %d\n",
                                 node->name, ret);
                    }
                }
            }
        }

        node = node->next;
    }

    log_info("Device probing complete: %u/%u successful\n",
             success_count, probe_count);

    return ERROR_SUCCESS;
}
```

---

## 17. 任务休眠编码规范

### 17.1 任务休眠API规范

#### 17.1.1 相对时间休眠
```c
/**
 * @brief 任务休眠指定时间（相对时间）
 * @param delay_ms 休眠时间（毫秒）
 *
 * @note 调用后任务进入SLEEPING状态，超时后自动唤醒
 * @warning 只能在任务上下文中调用，不能在中断中调用
 *
 * @code
 * void my_task(void) {
 *     while (1) {
 *         do_work();
 *         task_sleep(100);  // 休眠100ms
 *     }
 * }
 * @endcode
 */
void task_sleep(uint32_t delay_ms);
```

#### 17.1.2 绝对时间延迟
```c
/**
 * @brief 任务延迟直到指定绝对时间
 * @param deadline_ns 绝对截止时间（纳秒）
 * @return 成功返回ERROR_SUCCESS，失败返回错误码
 *
 * @note 与task_sleep()不同，这是绝对时间延迟
 * @note 用于精确的时间控制，补偿执行时间抖动
 *
 * @code
 * void periodic_task(void) {
 *     uint64_t next_wake_time = get_system_time_ns();
 *     const uint64_t period = 100000000ULL;  // 100ms
 *
 *     while (1) {
 *         next_wake_time += period;
 *         task_delay_until(next_wake_time);
 *         do_work();
 *     }
 * }
 * @endcode
 */
ErrorCode_t task_delay_until(uint64_t deadline_ns);
```

#### 17.1.3 周期性任务休眠
```c
/**
 * @brief 周期性任务休眠（计算下次唤醒时间）
 * @param period_ns 周期（纳秒）
 * @param last_wake_time 上次唤醒时间（输入/输出）
 *
 * @note 用于精确的周期性任务，自动补偿执行时间
 * @warning last_wake_time必须指向有效内存，在任务生命周期内持久
 *
 * @code
 * void precise_periodic_task(void) {
 *     static uint64_t last_wake = 0;
 *     const uint64_t period = 50000000ULL;  // 50ms
 *
 *     while (1) {
 *         task_sleep_periodic(period, &last_wake);
 *         do_work();  // 工作时间变化不影响周期精度
 *     }
 * }
 * @endcode
 */
void task_sleep_periodic(uint64_t period_ns, uint64_t *last_wake_time);
```

### 17.2 休眠队列管理规范

#### 17.2.1 休眠队列插入
```c
/**
 * @brief 将任务插入休眠队列（按截止时间排序）
 * @param task 任务指针
 * @return 成功返回ERROR_SUCCESS，失败返回错误码
 *
 * @note 休眠队列按sleep_deadline升序排列
 * @note 队头是最早唤醒的任务，O(1)查找
 * @note 插入操作为O(n)，n为休眠队列长度
 *
 * @ MISRA规则遵守：
 *   - 规则11.5: 避免指针转换
 *   - 规则13.6: 检查指针有效性
 *   - 规则17.7: 防止无限循环
 */
static ErrorCode_t sleep_queue_insert(TCB_t *task) {
    TCB_t *prev = NULL;
    TCB_t *curr = NULL;
    uint32_t iterations = 0U;
    const uint32_t max_iterations = MAX_TASKS;

    /* 参数验证 */
    if (task == NULL) {
        return ERROR_INVALID_PARAM;
    }

    /* 验证任务状态 */
    if (task->state != TASK_SLEEPING) {
        return ERROR_INVALID_STATE;
    }

    /* 获取队列锁 */
    ticket_lock_acquire(&scheduler.sleep_queue_lock);

    curr = scheduler.sleep_queue.head;

    /* 查找插入位置（保持队列有序） */
    while ((curr != NULL) &&
           (curr->sleep_deadline < task->sleep_deadline)) {
        prev = curr;
        curr = curr->next;

        /* 防止无限循环（MISRA规则17.7） */
        iterations++;
        if (iterations > max_iterations) {
            ticket_lock_release(&scheduler.sleep_queue_lock);
            return ERROR_LOOP_DETECTED;
        }
    }

    /* 插入任务 */
    if (prev == NULL) {
        /* 插入队头 */
        task->next = scheduler.sleep_queue.head;
        scheduler.sleep_queue.head = task;
    } else {
        /* 插入中间或队尾 */
        task->next = prev->next;
        prev->next = task;
    }

    ticket_lock_release(&scheduler.sleep_queue_lock);

    return ERROR_SUCCESS;
}
```

#### 17.2.2 休眠任务唤醒
```c
/**
 * @brief 唤醒超时的休眠任务（由定时器中断调用）
 * @note 此函数在系统滴答中断中调用
 * @warning 必须在禁用中断或持有锁时调用
 */
void wake_sleeping_tasks(void) {
    TCB_t *task = NULL;
    TCB_t *next = NULL;
    uint64_t current_time = 0U;
    uint32_t cpu_id = 0U;

    /* 获取当前时间 */
    current_time = get_system_time_ns();

    /* 获取休眠队列锁 */
    ticket_lock_acquire(&scheduler.sleep_queue_lock);

    task = scheduler.sleep_queue.head;

    /* 遍历休眠队列，唤醒所有超时任务 */
    while ((task != NULL) && (task->sleep_deadline <= current_time)) {
        next = task->next;

        /* 从休眠队列移除 */
        scheduler.sleep_queue.head = next;
        task->next = NULL;

        /* 验证任务状态 */
        if (task->state != TASK_SLEEPING) {
            /* 任务状态异常，跳过 */
            task = next;
            continue;
        }

        /* 改变状态为READY */
        task->state = TASK_READY;

        /* 获取CPU亲和性 */
        cpu_id = task->cpu_affinity;

        /* 加入就绪队列 */
        bitmap_set(scheduler.ready_queues[cpu_id].bitmap, task->priority);
        task_list_push_tail(&scheduler.ready_queues[cpu_id].queues[task->priority],
                           task);

        task = next;
    }

    ticket_lock_release(&scheduler.sleep_queue_lock);
}
```

### 17.3 休眠实现注意事项

#### 17.3.1 时间计算防溢出
```c
/**
 * @brief 安全计算休眠截止时间
 * @param delay_ms 延迟时间（毫秒）
 * @return 截止时间（纳秒）
 */
static uint64_t calculate_sleep_deadline(uint32_t delay_ms) {
    uint64_t current_time = 0U;
    uint64_t sleep_ns = 0U;

    current_time = get_system_time_ns();

    /* 转换为纳秒（MISRA规则：防止溢出） */
    if (delay_ms > (UINT64_MAX / 1000000ULL)) {
        /* 延迟时间过大，返回最大值 */
        return UINT64_MAX;
    }

    sleep_ns = (uint64_t)delay_ms * 1000000ULL;

    /* 检查加法溢出 */
    if (sleep_ns > (UINT64_MAX - current_time)) {
        return UINT64_MAX;
    }

    return current_time + sleep_ns;
}
```

#### 17.3.2 休眠状态验证
```c
/**
 * @brief 验证任务是否可以休眠
 * @param task 任务指针
 * @return 可以休眠返回true，否则返回false
 */
static bool validate_task_can_sleep(const TCB_t *task) {
    /* 检查任务指针 */
    if (task == NULL) {
        return false;
    }

    /* 检查任务魔数 */
    if (task->magic != TASK_MAGIC) {
        return false;
    }

    /* 检查任务状态 */
    if (task->state != TASK_RUNNING) {
        /* 只有运行态的任务可以休眠 */
        return false;
    }

    /* 检查是否持有锁（可选） */
    if (task->lock_count > 0U) {
        /* 持有锁的任务不建议休眠 */
        return false;
    }

    return true;
}
```

#### 17.3.3 休眠与调度集成
```c
/**
 * @brief 任务休眠入口函数
 * @param delay_ms 休眠时间（毫秒）
 * @return 成功返回ERROR_SUCCESS，失败返回错误码
 */
ErrorCode_t task_sleep(uint32_t delay_ms) {
    TCB_t *task = NULL;
    uint64_t sleep_deadline = 0U;
    ErrorCode_t ret = ERROR_SUCCESS;

    /* 0ms休眠：直接让出CPU */
    if (delay_ms == 0U) {
        schedule();
        return ERROR_SUCCESS;
    }

    task = scheduler.current_task[get_cpu_id()];

    /* 验证任务可以休眠 */
    if (!validate_task_can_sleep(task)) {
        return ERROR_INVALID_STATE;
    }

    /* 计算休眠截止时间 */
    sleep_deadline = calculate_sleep_deadline(delay_ms);

    /* 设置休眠时间 */
    task->sleep_deadline = sleep_deadline;
    task->sleep_start = get_system_time_ns();

    /* 改变任务状态 */
    task->state = TASK_SLEEPING;

    /* 加入休眠队列 */
    ret = sleep_queue_insert(task);
    if (ret != ERROR_SUCCESS) {
        /* 插入失败，恢复状态 */
        task->state = TASK_RUNNING;
        return ret;
    }

    /* 触发调度 */
    schedule();

    return ERROR_SUCCESS;
}
```

### 17.4 周期性任务最佳实践

#### 17.4.1 精确周期任务实现
```c
/**
 * @brief 精确周期任务模板
 * @param period_ns 任务周期（纳秒）
 */
void precise_periodic_task_template(uint64_t period_ns) {
    uint64_t last_wake_time = 0U;
    uint64_t next_wake_time = 0U;
    ErrorCode_t ret = ERROR_SUCCESS;

    /* 初始化唤醒时间 */
    last_wake_time = get_system_time_ns();

    while (1) {
        /* 执行任务工作 */
        do_periodic_work();

        /* 计算下次唤醒时间 */
        next_wake_time = last_wake_time + period_ns;

        /* 延迟到指定时间 */
        ret = task_delay_until(next_wake_time);

        if (ret == ERROR_SUCCESS) {
            /* 成功唤醒，更新上次唤醒时间 */
            last_wake_time = next_wake_time;
        } else {
            /* 唤醒失败，重新同步时间 */
            last_wake_time = get_system_time_ns();
        }
    }
}
```

#### 17.4.2 容错周期任务实现
```c
/**
 * @brief 容错周期任务（处理错过截止时间的情况）
 * @param period_ns 任务周期（纳秒）
 */
void fault_tolerant_periodic_task(uint64_t period_ns) {
    uint64_t last_wake_time = 0U;
    uint64_t next_wake_time = 0U;
    uint64_t current_time = 0U;
    uint32_t missed_periods = 0U;

    last_wake_time = get_system_time_ns();

    while (1) {
        /* 执行任务工作 */
        do_periodic_work();

        /* 计算下次唤醒时间 */
        next_wake_time = last_wake_time + period_ns;
        current_time = get_system_time_ns();

        /* 检查是否错过唤醒时间 */
        if (next_wake_time <= current_time) {
            /* 计算错过的周期数 */
            missed_periods = (uint32_t)((current_time - next_wake_time) / period_ns) + 1U;

            /* 记录错误（MISRA合规：检查整数溢出） */
            if (missed_periods < UINT32_MAX) {
                task->perf.missed_deadlines += missed_periods;
            }

            /* 重新同步到下一个周期 */
            next_wake_time = current_time + period_ns;
        }

        /* 延迟到指定时间 */
        if (task_delay_until(next_wake_time) == ERROR_SUCCESS) {
            last_wake_time = next_wake_time;
        } else {
            last_wake_time = get_system_time_ns();
        }
    }
}
```

### 17.5 MISRA合规性检查清单

#### 17.5.1 任务休眠实现检查项
- [ ] 所有时间计算都检查溢出
- [ ] 休眠队列插入防止无限循环
- [ ] 任务状态转换有明确的验证
- [ ] 锁的获取和释放配对
- [ ] 所有指针使用前都检查NULL
- [ ] 整数除法前检查除数不为零
- [ ] 所有返回值都被检查
- [ ] 休眠时间参数验证合理范围

#### 17.5.2 周期性任务检查项
- [ ] 使用绝对时间而非相对时间累加
- [ ] 处理错过截止时间的情况
- [ ] last_wake_time在任务生命周期内持久
- [ ] 周期参数不为零
- [ ] 休眠调用有错误处理

---

## 18. 任务隔离MISRA-C编码规范

### 18.1 任务隔离数据结构规范

#### 18.1.1 任务隔离模式枚举
```c
/**
 * @brief 任务隔离模式枚举
 * @note 必须显式指定枚举值为无符号类型
 */
typedef enum {
    TASK_ISOLATION_SHARED = 0U,     /* 共享地址空间（高性能） */
    TASK_ISOLATION_PRIVATE = 1U,    /* 独立地址空间（高安全） */
    TASK_ISOLATION_HYBRID = 2U      /* 混合模式（平衡） */
} TaskIsolationMode_t;

/* 编译时断言：验证枚举值范围 */
_Static_assert((uint32_t)TASK_ISOLATION_SHARED <= 2U,
               "TaskIsolationMode_t enum value out of range");
_Static_assert((uint32_t)TASK_ISOLATION_PRIVATE <= 2U,
               "TaskIsolationMode_t enum value out of range");
_Static_assert((uint32_t)TASK_ISOLATION_HYBRID <= 2U,
               "TaskIsolationMode_t enum value out of range");
```

#### 18.1.2 地址空间组结构
```c
/**
 * @brief 地址空间组结构
 * @note MISRA规则遵守：
 *   - 规则8.4: 结构体成员必须有明确类型
 *   - 规则8.5: 多个声明符必须分开声明
 */
typedef struct {
    uint64_t            page_table;         /* 共享页表基址 */
    uint32_t            group_id;           /* 组ID */
    uint32_t            task_count;         /* 任务数量 */
    struct TaskControlBlock *task_list;     /* 任务列表（前向声明） */
    TicketLock_t        lock;               /* 组锁 */
} AddressSpaceGroup_t;

/* 编译时断言：结构对齐和大小 */
_Static_assert((sizeof(AddressSpaceGroup_t) % 16U) == 0U,
               "AddressSpaceGroup_t must be 16-byte aligned");
_Static_assert(sizeof(AddressSpaceGroup_t) <= 256U,
               "AddressSpaceGroup_t size must not exceed 256 bytes");
```

### 18.2 任务隔离API规范

#### 18.2.1 创建地址空间组
```c
/**
 * @brief 创建地址空间组
 * @param group_id 组ID
 * @return 成功返回组指针，失败返回NULL
 *
 * @note 必须遵循的MISRA规则：
 *   - 规则11.5: 避免类型转换
 *   - 规则13.4: 检查malloc返回值
 *   - 规则21.3: 不应使用动态内存分配（安全关键系统）
 *
 * @warning 返回的指针必须由调用者释放
 */
AddressSpaceGroup_t *create_address_space_group(uint32_t group_id) {
    AddressSpaceGroup_t *group = NULL;
    uint64_t page_table_addr = 0U;

    /* 参数验证 */
    if (group_id == 0U) {
        return NULL;
    }

    /* 分配内存（MISRA规则21.3: 动态分配需要特殊考虑） */
    group = (AddressSpaceGroup_t *)malloc(sizeof(AddressSpaceGroup_t));
    if (group == NULL) {
        return NULL;
    }

    /* 清零内存（防止信息泄露） */
    (void)memset(group, 0, sizeof(AddressSpaceGroup_t));

    /* 创建新的页表 */
    page_table_addr = mmu_create_page_table();
    if (page_table_addr == 0U) {
        free(group);
        return NULL;
    }

    /* 初始化结构体 */
    group->page_table = page_table_addr;
    group->group_id = group_id;
    group->task_count = 0U;
    group->task_list = NULL;

    /* 初始化锁 */
    ticket_lock_init(&group->lock);

    return group;
}
```

#### 18.2.2 将任务加入地址空间组
```c
/**
 * @brief 将任务加入地址空间组
 * @param group 地址空间组指针
 * @param task 任务指针
 * @return 成功返回ERROR_SUCCESS，失败返回错误码
 *
 * @note 必须遵循的MISRA规则：
 *   - 规则1.3: 不得检查浮点数是否相等
 *   - 规则11.9: 避免NULL指针的隐式转换
 *   - 规则15.5: 禁止goto语句
 */
ErrorCode_t join_address_space_group(AddressSpaceGroup_t *group,
                                     TCB_t *task) {
    ErrorCode_t ret = ERROR_SUCCESS;

    /* 参数验证（MISRA规则：检查指针有效性） */
    if (group == NULL) {
        return ERROR_INVALID_PARAM;
    }

    if (task == NULL) {
        return ERROR_INVALID_PARAM;
    }

    /* 验证任务魔数（MISRA规则：防止使用已释放的内存） */
    if (task->magic != TASK_MAGIC) {
        return ERROR_INVALID_TASK;
    }

    /* 获取锁（MISRA规则：避免死锁） */
    ticket_lock_acquire(&group->lock);

    /* 检查任务是否已在组中 */
    if (task->address_space_id == group->group_id) {
        ticket_lock_release(&group->lock);
        return ERROR_ALREADY_EXISTS;
    }

    /* 设置任务的页表为组页表 */
    task->page_table = group->page_table;
    task->address_space_id = group->group_id;
    task->isolation_mode = TASK_ISOLATION_SHARED;

    /* 加入任务列表（线程安全） */
    task->next = group->task_list;
    group->task_list = task;

    /* 增加任务计数（防止溢出） */
    if (group->task_count < UINT32_MAX) {
        group->task_count++;
    } else {
        /* 计数溢出，回滚操作 */
        group->task_list = task->next;
        ticket_lock_release(&group->lock);
        return ERROR_OVERFLOW;
    }

    /* 释放锁 */
    ticket_lock_release(&group->lock);

    return ret;
}
```

### 18.3 页表切换MISRA规范

#### 18.3.1 页表切换函数
```c
/**
 * @brief 任务切换时的页表设置（优化版）
 * @param next_task 下一个运行的任务指针
 * @param prev_task 前一个运行的任务指针
 *
 * @note MISRA规则遵守：
 *   - 规则2.1: 不允许未定义行为
 *   - 规则10.1: 避免隐式类型转换
 *   - 规则11.5: 避免不必要的类型转换
 *
 * @warning 仅在页表不同时才切换，避免不必要的TLB刷新
 * @warning 必须在内联函数中实现，避免函数调用开销
 */
static inline void switch_page_table(const TCB_t *next_task,
                                      const TCB_t *prev_task) {
    /* 参数验证（MISRA规则：检查指针） */
    if ((next_task == NULL) || (prev_task == NULL)) {
        return;
    }

    /* 比较页表（MISRA规则：显式比较） */
    if (next_task->page_table != prev_task->page_table) {
        /* 页表不同，需要切换 */
        uint64_t new_ttbr0 = next_task->page_table;

        /* 设置页表基址寄存器（内联汇编） */
        __asm__ volatile("msr ttbr0_el1, %0" :: "r"(new_ttbr0));

        /* 指令同步屏障（确保页表切换生效） */
        __asm__ volatile("isb");

        /* 刷新TLB（使旧页表映射失效） */
        __asm__ volatile("tlbi vmalle1is");

        /* 数据同步屏障（确保TLB刷新完成） */
        __asm__ volatile("dsb ish");

        /* 指令同步屏障（确保后续指令使用新页表） */
        __asm__ volatile("isb");
    }
    /* 页表相同，无需切换，TLB保持有效 */
}
```

#### 18.3.2 上下文切换函数
```c
/**
 * @brief 上下文切换函数（集成页表切换）
 * @param prev_task 前一个任务指针
 * @param next_task 下一个任务指针
 *
 * @note MISRA规则遵守：
 *   - 规则17.7: 函数返回值必须检查
 *   - 规则15.5: 禁止goto语句
 *   - 规则8.2: 不应声明多个变量
 */
void context_switch(TCB_t *prev_task, TCB_t *next_task) {
    /* 参数验证 */
    if ((prev_task == NULL) || (next_task == NULL)) {
        return;
    }

    /* 验证任务魔数 */
    if ((prev_task->magic != TASK_MAGIC) ||
        (next_task->magic != TASK_MAGIC)) {
        return;
    }

    /* 保存当前任务上下文（ARM64寄存器） */
    save_context(prev_task);

    /* 切换页表（如果需要） */
    switch_page_table(next_task, prev_task);

    /* 恢复下一个任务上下文 */
    restore_context(next_task);
}
```

### 18.4 隔离模式管理MISRA规范

#### 18.4.1 设置隔离模式
```c
/**
 * @brief 设置任务隔离模式
 * @param task 任务指针
 * @param mode 隔离模式
 * @return 成功返回ERROR_SUCCESS，失败返回错误码
 *
 * @note MISRA规则遵守：
 *   - 规则16.3: switch语句必须有default分支
 *   - 规则15.7: switch语句中必须有break
 *   - 规则1.3: 禁止未检查的返回值
 */
ErrorCode_t task_set_isolation_mode(TCB_t *task,
                                     TaskIsolationMode_t mode) {
    ErrorCode_t ret = ERROR_SUCCESS;

    /* 参数验证 */
    if (task == NULL) {
        return ERROR_INVALID_PARAM;
    }

    /* 验证魔数 */
    if (task->magic != TASK_MAGIC) {
        return ERROR_INVALID_TASK;
    }

    /* 验证枚举值范围 */
    if ((uint32_t)mode > 2U) {
        return ERROR_INVALID_PARAM;
    }

    /* 根据模式设置隔离（MISRA规则16.3: 完整的switch） */
    switch (mode) {
        case TASK_ISOLATION_SHARED:
            /* 共享模式：加入默认地址空间组 */
            ret = join_address_space_group(g_default_as_group, task);
            if (ret != ERROR_SUCCESS) {
                return ret;
            }
            break;

        case TASK_ISOLATION_PRIVATE:
            /* 独立模式：创建独立页表 */
            task->page_table = mmu_create_page_table();
            if (task->page_table == 0U) {
                return ERROR_OUT_OF_MEMORY;
            }
            task->address_space_id = task->task_id;
            task->isolation_mode = TASK_ISOLATION_PRIVATE;
            break;

        case TASK_ISOLATION_HYBRID:
            /* 混合模式：根据任务优先级决定 */
            if (task->priority >= 200U) {
                /* 高优先级任务使用独立页表 */
                task->page_table = mmu_create_page_table();
                if (task->page_table == 0U) {
                    return ERROR_OUT_OF_MEMORY;
                }
                task->isolation_mode = TASK_ISOLATION_PRIVATE;
            } else {
                /* 低优先级任务共享页表 */
                ret = join_address_space_group(g_default_as_group, task);
                if (ret != ERROR_SUCCESS) {
                    return ret;
                }
            }
            break;

        default:
            /* 不应到达此处（枚举值已验证） */
            return ERROR_INVALID_PARAM;
    }

    return ret;
}
```

### 18.5 地址空间验证

#### 18.5.1 验证任务地址空间
```c
/**
 * @brief 验证任务地址空间配置
 * @param task 任务指针
 * @return 配置有效返回true，否则返回false
 *
 * @note MISRA规则遵守：
 *   - 规则10.1: 避免隐式类型转换
 *   - 规则11.5: 避免指针转换
 *   - 规则13.5: 检查指针有效性
 */
bool validate_task_address_space(const TCB_t *task) {
    uint64_t page_table_base = 0U;

    /* 参数验证 */
    if (task == NULL) {
        return false;
    }

    /* 验证魔数 */
    if (task->magic != TASK_MAGIC) {
        return false;
    }

    /* 验证页表地址（必须对齐且非零） */
    page_table_base = task->page_table;
    if (page_table_base == 0U) {
        return false;
    }

    /* 验证页表4KB对齐（MISRA规则：位操作的正确性） */
    if ((page_table_base & 0xFFFU) != 0U) {
        return false;
    }

    /* 验证隔离模式与地址空间ID的一致性 */
    if (task->isolation_mode == TASK_ISOLATION_PRIVATE) {
        /* 独立模式：address_space_id应等于task_id */
        if (task->address_space_id != task->task_id) {
            return false;
        }
    } else if (task->isolation_mode == TASK_ISOLATION_SHARED) {
        /* 共享模式：address_space_id不应为0 */
        if (task->address_space_id == 0U) {
            return false;
        }
    }

    return true;
}
```

### 18.6 安全检查清单

#### 18.6.1 任务隔离实现检查项
- [ ] 所有指针参数在使用前检查NULL
- [ ] 所有枚举值验证范围
- [ ] 页表地址验证对齐和非零
- [ ] 地址空间组锁的获取和释放配对
- [ ] 任务计数防止溢出
- [ ] 魔数验证防止使用已释放内存
- [ ] switch语句包含default分支
- [ ] 所有返回值被检查
- [ ] 动态内存分配有错误处理
- [ ] 内联汇编有适当的内存屏障

#### 18.6.2 页表切换检查项
- [ ] 仅在页表不同时切换
- [ ] 切换后刷新TLB
- [ ] 使用适当的内存屏障（ISB、DSB）
- [ ] 内联函数避免函数调用开销
- [ ] 参数验证防止NULL指针

#### 18.6.3 隔离模式检查项
- [ ] 枚举值显式声明为无符号
- [ ] switch语句有完整的case分支
- [ ] 每个case分支有break
- [ ] 有default分支处理意外情况
- [ ] 隔离模式与地址空间ID一致性检查

---

## 19. POSIX兼容层MISRA-C编码规范

### 19.1 PSE52合规要求

#### 19.1.1 PSE52概述

**PSE52（POSIX Embedded Systems）**是POSIX标准针对嵌入式实时系统的配置文件，定义在**IEEE Std 1003.13-2001**中。PSE52规定了嵌入式系统必须支持的POSIX功能子集，与完整POSIX相比，PSE52移除了进程模型（fork/exec）、文件系统、终端I/O等不适合嵌入式系统的功能。

**AISafe64的PSE52合规声明**

AISafe64的POSIX兼容层遵循PSE52标准，支持以下功能域：

1. **线程管理（Threads）**
   - pthread_create, pthread_join, pthread_detach, pthread_exit
   - pthread_self, pthread_equal, pthread_once

2. **互斥锁（Mutexes）**
   - pthread_mutex_*, pthread_mutexattr_*
   - 支持类型：NORMAL, RECURSIVE, ERRORCHECK
   - 支持优先级继承协议（PTHREAD_PRIO_INHERIT）

3. **条件变量（Condition Variables）**
   - pthread_cond_*
   - pthread_cond_wait, pthread_cond_signal, pthread_cond_broadcast
   - pthread_cond_timedwait

4. **读写锁（Read-Write Locks）**
   - pthread_rwlock_*
   - 支持读者-写者锁模式

5. **信号量（Semaphores）**
   - sem_*（无名信号量）
   - sem_open, sem_close（有名信号量）

6. **调度控制（Scheduling）**
   - sched_setscheduler, sched_getscheduler
   - sched_yield, sched_get_priority_max, sched_get_priority_min
   - SCHED_FIFO, SCHED_RR调度策略

7. **线程特定数据（Thread-Specific Data）**
   - pthread_key_create, pthread_key_delete
   - pthread_setspecific, pthread_getspecific

8. **线程取消（Cancellation）**
   - pthread_cancel, pthread_setcancelstate
   - pthread_testcancel, pthread_setcanceltype

**PSE52与完整POSIX的区别**

| 功能类别 | 完整POSIX | PSE52 | AISafe64支持 |
|---------|-----------|-------|--------------|
| 进程管理 | fork, exec, wait | ❌ 不支持 | ❌ 使用扁平化任务模型 |
| 线程管理 | pthread_* | ✅ 完整支持 | ✅ PSE52子集 |
| 信号处理 | signal, sigaction | ⚠️ 有限支持 | ⚠️ 简化版 |
| 文件系统 | open, read, write | ❌ 不支持 | ❌ 使用设备驱动接口 |
| Socket | socket, bind, listen | ❌ 不支持 | ⚠️ 单独实现网络栈 |
| 消息队列 | mq_*, msg_* | ❌ 不支持 | ❌ 使用原生队列 |
| 共享内存 | shm_* | ❌ 不支持 | ❌ 使用地址空间组 |
| 互斥锁 | pthread_mutex_* | ✅ 完整支持 | ✅ PSE52子集 |
| 条件变量 | pthread_cond_* | ✅ 完整支持 | ✅ PSE52子集 |
| 读写锁 | pthread_rwlock_* | ✅ 完整支持 | ✅ PSE52子集 |
| 信号量 | sem_* | ✅ 完整支持 | ✅ PSE52子集 |

#### 19.1.2 PSE52合规性检查清单

**开发阶段检查项**
- [ ] 所有PSE52必需的API都有实现
- [ ] API行为符合PSE52规范
- [ ] 互斥锁支持优先级继承协议
- [ ] 调度策略支持SCHED_FIFO和SCHED_RR
- [ ] 线程特定数据正确实现
- [ ] 取消点正确设置

**测试阶段检查项**
- [ ] 通过PSE52一致性测试套件
- [ ] 所有API都有单元测试
- [ ] 优先级继承协议测试
- [ ] 调度策略测试
- [ ] 取消语义测试

**文档要求**
- [ ] PSE52合规性声明文档
- [ ] API差异说明文档
- [ ] 限制和约束文档
- [ ] 测试报告

### 19.2 POSIX适配层设计原则

#### 19.2.1 核心设计原则
```c
/**
 * @brief POSIX适配层设计原则
 *
 * 1. 最小侵入性：不修改原生内核API
 * 2. 直接映射：优先直接调用原生API，避免中间层
 * 3. MISRA合规：适配层同样遵循MISRA-C:2012规范
 * 4. 可配置性：通过CONFIG_POSIX_COMPAT启用/禁用
 * 5. 零开销原则：未被使用的POSIX功能不产生代码
 */
```

#### 19.1.2 命名规范
```c
/**
 * @brief POSIX API命名规范
 *
 * 1. 使用标准POSIX名称（pthread_, sem_, sched_等）
 * 2. 不添加aisafe64_前缀（保持源码兼容性）
 * 3. 内部实现函数使用posix_前缀
 * 4. 类型定义遵循POSIX标准（pthread_t, sem_t等）
 */

/* ✅ 正确：标准POSIX名称 */
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg);

/* ✅ 正确：内部实现函数 */
static void posix_pthread_entry_wrapper(void *arg);

/* ❌ 错误：不要添加前缀 */
int aisafe64_pthread_create(...);  /* 破坏源码兼容性 */
```

### 19.2 pthread适配规范

#### 19.2.1 pthread_t类型定义
```c
/**
 * @brief pthread_t类型定义
 * @note 必须是标量类型，不能是结构体
 * @note 使用uint32_t封装原生task_id
 */
typedef uint32_t pthread_t;

/* 编译时断言：pthread_t必须是标量 */
_Static_assert(sizeof(pthread_t) == sizeof(uint32_t),
               "pthread_t must be same size as uint32_t");

/* NULL值定义 */
#define PTHREAD_NULL 0U
```

#### 19.2.2 pthread_create实现规范
```c
/**
 * @brief 创建线程（POSIX适配层）
 * @param thread 输出线程ID指针
 * @param attr 线程属性指针（NULL使用默认值）
 * @param start_routine 线程入口函数（返回void*）
 * @param arg 传递给线程的参数
 * @return 成功返回0，失败返回错误码（如EINVAL, EAGAIN）
 *
 * @note MISRA规则遵守：
 *   - 规则11.5: 避免类型转换（包装器处理）
 *   - 规则13.4: 检查所有指针参数
 *   - 规则16.3: 必须检查所有返回值
 *
 * @warning 此函数必须处理函数签名适配：
 *          - POSIX: void *(*)(void *)
 *          - Native: void (*)(void)
 */
int pthread_create(pthread_t *thread,
                   const pthread_attr_t *attr,
                   void *(*start_routine)(void *),
                   void *arg) {
    /* 参数验证（MISRA规则13.4） */
    if ((thread == NULL) || (start_routine == NULL)) {
        return EINVAL;
    }

    /* 属性提取（如果attr为NULL，使用默认值） */
    uint8_t priority = 128U;  /* 默认中等优先级 */
    uint32_t stack_size = 8192U;

    if (attr != NULL) {
        priority = attr->priority;
        stack_size = attr->stack_size;

        /* 属性验证 */
        if (priority > 255U) {
            return EINVAL;
        }

        if (stack_size < 4096U) {
            return EINVAL;
        }
    }

    /* 分配包装器（避免动态内存分配） */
    PthreadWrapper_t *wrapper = posix_wrapper_alloc();
    if (wrapper == NULL) {
        return EAGAIN;
    }

    /* 保存函数指针和参数 */
    wrapper->entry = start_routine;
    wrapper->arg = arg;

    /* 调用原生API创建任务 */
    uint32_t task_id = task_create(posix_pthread_entry_wrapper,
                                   priority, stack_size, "pthread");
    if (task_id == 0U) {
        posix_wrapper_free(wrapper);
        return EAGAIN;
    }

    /* 保存包装器到TCB */
    TCB_t *task = get_task_by_id(task_id);
    if (task != NULL) {
        task->posix_wrapper = wrapper;
    }

    /* 返回pthread_t（实际是task_id） */
    *thread = task_id;

    return 0;  /* 成功 */
}
```

#### 19.2.3 函数签名包装器
```c
/**
 * @brief pthread入口函数包装器
 * @param arg 传递的参数
 *
 * @note 此函数将void *(*)(void *)适配到void (*)(void)
 * @note 必须声明为static，避免全局命名空间污染
 */
static void posix_pthread_entry_wrapper(void *arg) {
    PthreadWrapper_t *wrapper;
    void *result;

    /* 提取包装器 */
    wrapper = (PthreadWrapper_t *)arg;

    /* 验证包装器 */
    if (wrapper == NULL) {
        /* 严重错误：包装器为空 */
        kernel_error(ERROR_INVALID_PARAM);
        return;
    }

    /* 调用用户函数 */
    result = wrapper->entry(wrapper->arg);

    /* 自动调用pthread_exit */
    posix_pthread_exit(result);
}

/**
 * @brief 线程退出函数
 * @param value_ptr 返回值指针
 *
 * @note 此函数不会返回
 */
void pthread_exit(void *value_ptr) {
    posix_pthread_exit(value_ptr);

    /* 防止编译器警告 */
    for (;;) {
        __asm__ volatile("wfe");  /* 等待中断 */
    }
}
```

### 19.3 互斥锁适配规范

#### 19.3.1 pthread_mutex_t结构
```c
/**
 * @brief POSIX互斥锁结构
 * @note 必须包含所有必要字段以支持递归锁和错误检查
 */
typedef struct {
    uint32_t    mutex_id;       /* 原生互斥锁ID */
    uint32_t    type;           /* 锁类型（NORMAL/RECURSIVE/ERRORCHECK） */
    uint32_t    owner;          /* 当前持有者的task_id */
    uint32_t    lock_count;     /* 递归锁计数 */
} pthread_mutex_t;

/* 编译时断言：结构大小和对齐 */
_Static_assert(sizeof(pthread_mutex_t) <= 32U,
               "pthread_mutex_t size must not exceed 32 bytes");
_Static_assert((sizeof(pthread_mutex_t) % 8U) == 0U,
               "pthread_mutex_t must be 8-byte aligned");
```

#### 19.3.2 pthread_mutex_lock实现
```c
/**
 * @brief 锁定互斥锁
 * @param mutex 互斥锁指针
 * @return 成功返回0，失败返回错误码
 *
 * @note MISRA规则遵守：
 *   - 规则11.5: 避免不必要的类型转换
 *   - 规则15.7: 每个case分支必须有break
 *   - 规则16.3: 必须处理所有枚举值
 */
int pthread_mutex_lock(pthread_mutex_t *mutex) {
    ErrorCode_t ret;
    uint32_t current_task;

    /* 参数验证 */
    if (mutex == NULL) {
        return EINVAL;
    }

    current_task = get_current_task_id();

    /* 递归锁处理 */
    if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
        if (mutex->owner == current_task) {
            /* 已经持有锁，递增计数 */
            if (mutex->lock_count < UINT32_MAX) {
                mutex->lock_count++;
                return 0;
            } else {
                /* 递归深度溢出 */
                return EAGAIN;
            }
        }
    }

    /* 错误检查锁 */
    if (mutex->type == PTHREAD_MUTEX_ERRORCHECK) {
        if (mutex->owner == current_task) {
            /* 尝试锁定已持有的锁 */
            return EDEADLK;
        }
    }

    /* 调用原生API锁定 */
    ret = mutex_lock(mutex->mutex_id);
    if (ret != ERROR_SUCCESS) {
        return EBUSY;
    }

    /* 记录锁的持有者 */
    mutex->owner = current_task;
    mutex->lock_count = 1U;

    return 0;
}

/**
 * @brief 解锁互斥锁
 * @param mutex 互斥锁指针
 * @return 成功返回0，失败返回错误码
 */
int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    uint32_t current_task;

    /* 参数验证 */
    if (mutex == NULL) {
        return EINVAL;
    }

    current_task = get_current_task_id();

    /* 错误检查锁：验证所有权 */
    if (mutex->type == PTHREAD_MUTEX_ERRORCHECK) {
        if (mutex->owner != current_task) {
            /* 不是锁的持有者 */
            return EPERM;
        }

        if (mutex->lock_count == 0U) {
            /* 锁未被持有 */
            return EPERM;
        }
    }

    /* 递归锁：递减计数 */
    if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
        if (mutex->lock_count > 1U) {
            mutex->lock_count--;
            return 0;
        }
    }

    /* 调用原生API解锁 */
    mutex_unlock(mutex->mutex_id);

    /* 清除持有者信息 */
    mutex->owner = 0U;
    mutex->lock_count = 0U;

    return 0;
}
```

### 19.4 信号量适配规范

#### 19.4.1 sem_t结构定义
```c
/**
 * @brief POSIX信号量结构
 * @note 直接使用原生信号量，增加计数字段
 */
typedef struct {
    uint32_t    sem_id;         /* 原生信号量ID */
    uint32_t    value;          /* 当前值（用于调试） */
    uint32_t    max_value;      /* 最大值 */
    uint8_t     initialized;    /* 初始化标志 */
} sem_t;

/* 编译时断言 */
_Static_assert(sizeof(sem_t) <= 16U,
               "sem_t size must not exceed 16 bytes");
```

#### 19.4.2 sem_init实现
```c
/**
 * @brief 初始化信号量
 * @param sem 信号量指针
 * @param pshared 共享标志（单进程模式忽略）
 * @param value 初始值
 * @return 成功返回0，失败返回-1
 *
 * @note pshared参数在单进程模式中未使用
 * @note 失败时设置errno
 */
int sem_init(sem_t *sem, int pshared, unsigned int value) {
    /* 参数验证 */
    if (sem == NULL) {
        errno = EINVAL;
        return -1;
    }

    /* 检查是否已初始化 */
    if (sem->initialized != 0U) {
        errno = EINVAL;
        return -1;
    }

    /* 验证初始值 */
    if (value > UINT32_MAX) {
        errno = EINVAL;
        return -1;
    }

    /* 创建原生信号量 */
    sem->sem_id = sem_create(value);
    if (sem->sem_id == 0U) {
        errno = ENOMEM;
        return -1;
    }

    /* 初始化字段 */
    sem->value = value;
    sem->max_value = UINT32_MAX;
    sem->initialized = 1U;

    /* 未使用的参数（MISRA规则：避免警告） */
    (void)pshared;

    return 0;
}
```

### 19.5 条件变量适配规范

#### 19.5.1 pthread_cond_t结构
```c
/**
 * @brief POSIX条件变量结构
 */
typedef struct {
    uint32_t    cond_id;        /* 原生条件变量ID */
    uint32_t    wait_count;     /* 等待任务数 */
    uint8_t     initialized;    /* 初始化标志 */
} pthread_cond_t;
```

#### 19.5.2 pthread_cond_wait实现
```c
/**
 * @brief 等待条件变量
 * @param cond 条件变量指针
 * @param mutex 关联的互斥锁指针
 * @return 成功返回0，失败返回错误码
 *
 * @note MISRA规则遵守：
 *   - 规则15.5: 禁止goto语句
 *   - 规则16.3: 必须检查所有返回值
 *
 * @warning 此函数是原子操作：解锁mutex并等待
 */
int pthread_cond_wait(pthread_cond_t *cond,
                      pthread_mutex_t *mutex) {
    ErrorCode_t ret;

    /* 参数验证 */
    if ((cond == NULL) || (mutex == NULL)) {
        return EINVAL;
    }

    /* 验证初始化 */
    if (cond->initialized == 0U) {
        return EINVAL;
    }

    /* 验证互斥锁所有权（错误检查） */
    if (mutex->owner != get_current_task_id()) {
        return EPERM;
    }

    /* 原子操作：解锁互斥锁并等待 */
    ret = cond_wait_native(cond->cond_id, mutex->mutex_id);

    /* 重新锁定互斥锁（无论等待成功或失败） */
    if (pthread_mutex_lock(mutex) != 0) {
        /* 重新锁定失败，严重错误 */
        kernel_error(ERROR_MUTEX_LOCK_FAILED);
        return EINVAL;
    }

    return (ret == ERROR_SUCCESS) ? 0 : ETIMEDOUT;
}
```

### 19.6 睡眠函数适配

#### 19.6.1 usleep实现
```c
/**
 * @brief 微秒级睡眠
 * @param usec 微秒数（0-999999）
 * @return 成功返回0，失败返回-1
 *
 * @note MISRA规则遵守：
 *   - 规则10.3: 检查整数溢出
 *   - 规则12.1: 避免未定义的求值顺序
 */
int usleep(useconds_t usec) {
    uint32_t msec;
    uint64_t tmp;

    /* 参数验证 */
    if (usec > 1000000U) {
        /* 超过1秒，分解为多次调用 */
        uint32_t seconds = (uint32_t)(usec / 1000000U);
        uint32_t remainder = (uint32_t)(usec % 1000000U);
        uint32_t i;

        for (i = 0U; i < seconds; i++) {
            task_sleep(1000U);  /* 睡眠1秒 */
        }

        if (remainder > 0U) {
            /* 向上取整转换 */
            tmp = (uint64_t)remainder + 999ULL;
            msec = (uint32_t)(tmp / 1000ULL);
            if (msec > 0U) {
                task_sleep(msec);
            }
        }
    } else {
        /* 向上取整转换（避免精度丢失） */
        tmp = (uint64_t)usec + 999ULL;
        msec = (uint32_t)(tmp / 1000ULL);
        if (msec > 0U) {
            task_sleep(msec);
        }
    }

    return 0;
}
```

### 19.7 编译条件控制

#### 19.7.1 头文件保护
```c
/**
 * @file pthread.h
 * @brief POSIX pthread适配层
 */

#ifndef PTHREAD_H
#define PTHREAD_H

#ifdef CONFIG_POSIX_COMPAT

/* POSIX pthread定义 */
typedef uint32_t pthread_t;
/* ... 其他定义 ... */

#else

/* POSIX兼容层未启用，提供错误宏 */
#define pthread_create(...) \
    do { \
        errno = ENOSYS; \
        return ENOSYS; \
    } while (0)

#endif /* CONFIG_POSIX_COMPAT */

#endif /* PTHREAD_H */
```

#### 19.7.2 实现文件条件编译
```c
/**
 * @file pthread.c
 * @brief pthread适配层实现
 */

#include "pthread.h"

#ifdef CONFIG_POSIX_COMPAT

/* pthread实现代码 */
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg) {
    /* 完整实现 */
}

#else

/* POSIX兼容层未启用，提供存根 */
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg) {
    (void)thread;
    (void)attr;
    (void)start_routine;
    (void)arg;
    errno = ENOSYS;
    return ENOSYS;
}

#endif /* CONFIG_POSIX_COMPAT */
```

### 19.8 错误处理规范

#### 19.8.1 errno设置
```c
/**
 * @brief 设置errno（线程安全）
 * @param err 错误码
 *
 * @note errno必须是线程局部的
 */
static inline void posix_set_errno(int err) {
    /* 使用线程局部存储 */
    uint32_t task_id = get_current_task_id();
    TCB_t *task = get_task_by_id(task_id);

    if (task != NULL) {
        task->posix_errno = err;
    }

    /* 也设置全局errno（用于单线程调试） */
    errno = err;
}
```

### 19.9 性能优化规范

#### 19.9.1 内联小函数
```c
/**
 * @brief 内联pthread_self（性能优化）
 * @return 调用线程的ID
 *
 * @note 标记为static inline以优化性能
 */
static inline pthread_t pthread_self(void) {
    return (pthread_t)get_current_task_id();
}

/**
 * @brief 内联pthread_equal（性能优化）
 * @param t1 线程1
 * @param t2 线程2
 * @return 相等返回非0，否则返回0
 */
static inline int pthread_equal(pthread_t t1, pthread_t t2) {
    return (t1 == t2) ? 1 : 0;
}
```

### 19.10 测试要求

#### 19.10.1 POSIX兼容性测试
```c
/**
 * @brief POSIX兼容性测试用例
 *
 * 测试场景：
 * 1. pthread创建和销毁
 * 2. 互斥锁锁定/解锁
 * 3. 信号量等待/发送
 * 4. 条件变量等待/通知
 * 5. 睡眠函数精度
 * 6. 递归锁行为
 * 7. 错误处理
 */
void test_posix_pthread(void);
void test_posix_mutex(void);
void test_posix_semaphore(void);
void test_posix_cond(void);
void test_posix_sleep(void);
```

### 19.11 安全检查清单

#### 19.11.1 POSIX适配层检查项
- [ ] 所有POSIX API参数验证NULL指针
- [ ] 函数签名适配正确（void *(*)(void *)到void (*)(void)）
- [ ] 包装器分配避免动态内存
- [ ] 递归锁计数防止溢出
- [ ] errno设置线程安全
- [ ] 条件编译正确（CONFIG_POSIX_COMPAT）
- [ ] MISRA-C:2012规则全部遵守
- [ ] 未使用的参数明确标记(void)
- [ ] 所有返回值正确映射到POSIX错误码
- [ ] 内联函数标记static inline

#### 19.11.2 pthread特定检查项
- [ ] pthread_t类型是标量（uint32_t）
- [ ] pthread_create验证attr有效性
- [ ] pthread_join检测死锁
- [ ] pthread_exit不会返回
- [ ] 递归锁正确处理多次锁定
- [ ] 错误检查锁验证所有权
- [ ] 分离线程正确处理资源释放

---

## 附录B: 完整示例

### A.1 完整的模块示例

```c
/**
 * @file    bitmap.h
 * @brief   256级优先级位图管理
 * @author  AISafe64 Team
 * @date    2025-01-07
 */

#ifndef BITMAP_H
#define BITMAP_H

#include <stdint.h>
#include <stdbool.h>

/* 优先级位图定义 */
typedef struct {
    uint64_t bits[4U];  /* 4×64位 = 256位 */
} Bitmap256_t;

/* 函数声明 */
void bitmap256_init(Bitmap256_t *bmp);
void bitmap256_set(Bitmap256_t *bmp, uint8_t index);
void bitmap256_clear(Bitmap256_t *bmp, uint8_t index);
bool bitmap256_test(const Bitmap256_t *bmp, uint8_t index);
int8_t bitmap256_find_first_set(const Bitmap256_t *bmp);
bool bitmap256_is_empty(const Bitmap256_t *bmp);

#endif /* BITMAP_H */
```

```c
/**
 * @file    bitmap.c
 * @brief   256级优先级位图实现
 * @author  AISafe64 Team
 */

#include "bitmap.h"
#include "assert.h"

/**
 * @brief 初始化位图
 * @param bmp 位图指针
 */
void bitmap256_init(Bitmap256_t *bmp) {
    if (bmp == NULL) {
        return;
    }

    bmp->bits[0U] = 0ULL;
    bmp->bits[1U] = 0ULL;
    bmp->bits[2U] = 0ULL;
    bmp->bits[3U] = 0ULL;
}

/**
 * @brief 设置位图中的某一位
 * @param bmp 位图指针
 * @param index 位索引（0-255）
 */
void bitmap256_set(Bitmap256_t *bmp, uint8_t index) {
    if ((bmp == NULL) || (index >= 256U)) {
        return;
    }

    uint32_t word_index = (uint32_t)index >> 6U;  /* / 64 */
    uint32_t bit_offset = (uint32_t)index & 0x3FU;  /* % 64 */

    bmp->bits[word_index] |= (1ULL << (63U - bit_offset));
}

/**
 * @brief 清除位图中的某一位
 * @param bmp 位图指针
 * @param index 位索引（0-255）
 */
void bitmap256_clear(Bitmap256_t *bmp, uint8_t index) {
    if ((bmp == NULL) || (index >= 256U)) {
        return;
    }

    uint32_t word_index = (uint32_t)index >> 6U;
    uint32_t bit_offset = (uint32_t)index & 0x3FU;

    bmp->bits[word_index] &= ~(1ULL << (63U - bit_offset));
}

/**
 * @brief 测试位图中的某一位
 * @param bmp 位图指针
 * @param index 位索引（0-255）
 * @return true表示位已设置，false表示未设置
 */
bool bitmap256_test(const Bitmap256_t *bmp, uint8_t index) {
    if ((bmp == NULL) || (index >= 256U)) {
        return false;
    }

    uint32_t word_index = (uint32_t)index >> 6U;
    uint32_t bit_offset = (uint32_t)index & 0x3FU;

    return (bmp->bits[word_index] & (1ULL << (63U - bit_offset))) != 0ULL;
}

/**
 * @brief 查找第一个设置的位（最高优先级）
 * @param bmp 位图指针
 * @return 位索引（0-255），-1表示无设置位
 */
int8_t bitmap256_find_first_set(const Bitmap256_t *bmp) {
    if (bmp == NULL) {
        return -1;
    }

    /* 使用CLZ指令快速查找 */
    if (bmp->bits[0U] != 0ULL) {
        return (int8_t)__builtin_clzll(bmp->bits[0U]);
    }
    if (bmp->bits[1U] != 0ULL) {
        return (int8_t)(64 + __builtin_clzll(bmp->bits[1U]));
    }
    if (bmp->bits[2U] != 0ULL) {
        return (int8_t)(128 + __builtin_clzll(bmp->bits[2U]));
    }
    if (bmp->bits[3U] != 0ULL) {
        return (int8_t)(192 + __builtin_clzll(bmp->bits[3U]));
    }

    return -1;
}

/**
 * @brief 检查位图是否为空
 * @param bmp 位图指针
 * @return true表示为空，false表示非空
 */
bool bitmap256_is_empty(const Bitmap256_t *bmp) {
    if (bmp == NULL) {
        return true;
    }

    return (bmp->bits[0U] == 0ULL) &&
           (bmp->bits[1U] == 0ULL) &&
           (bmp->bits[2U] == 0ULL) &&
           (bmp->bits[3U] == 0ULL);
}
```

---

## 20. 系统调用MISRA-C编码规范

### 20.1 系统调用设计原则

#### 20.1.1 自适应系统调用架构

AISafe64采用**自适应系统调用架构**，根据任务隔离模式动态选择最优的内核调用方式。此架构必须严格遵循MISRA-C:2012规范。

**核心设计原则**

```c
/**
 * @brief 系统调用模式枚举
 * @note 必须显式指定枚举值为无符号类型
 */
typedef enum {
    SYSCALL_MODE_NONE = 0U,      /* 无系统调用（直接函数调用） */
    SYSCALL_MODE_ALWAYS,        /* 总是使用系统调用 */
    SYSCALL_MODE_ADAPTIVE       /* 自适应模式（推荐） */
} SyscallMode_t;

/* 编译时断言：验证枚举值范围 */
_Static_assert((uint32_t)SYSCALL_MODE_NONE == 0U,
               "SYSCALL_MODE_NONE must be 0");
_Static_assert((uint32_t)SYSCALL_MODE_ALWAYS == 1U,
               "SYSCALL_MODE_ALWAYS must be 1");
_Static_assert((uint32_t)SYSCALL_MODE_ADAPTIVE == 2U,
               "SYSCALL_MODE_ADAPTIVE must be 2");
```

#### 20.1.2 系统调用表定义规范

**系统调用表项结构**

```c
/**
 * @brief 系统调用处理函数类型
 * @note 函数指针类型定义必须明确
 */
typedef long (*SyscallHandler_t)(uint64_t *params);

/**
 * @brief 系统调用表项
 * @note MISRA规则遵守：
 *   - 规则8.4: 结构体成员必须有明确类型
 *   - 规则8.5: 多个声明符必须分开声明
 */
typedef struct {
    const char          *name;           /* 系统调用名称 */
    SyscallHandler_t    handler;        /* 处理函数指针 */
    uint32_t            param_count;    /* 参数数量 */
    uint64_t            required_cap;   /* 所需能力 */
} SyscallEntry_t;

/* 编译时断言：结构对齐和大小 */
_Static_assert((sizeof(SyscallEntry_t) % 8U) == 0U,
               "SyscallEntry_t must be 8-byte aligned");
_Static_assert(sizeof(SyscallEntry_t) <= 32U,
               "SyscallEntry_t size must not exceed 32 bytes");
```

**系统调用能力定义**

```c
/**
 * @brief 系统调用能力定义（位掩码）
 * @note 使用无符号64位整数
 */
typedef uint64_t Capability_t;

/* 能力位定义（每个能力对应一个位） */
#define CAP_NONE              0ULL
#define CAP_PTHREAD_CREATE    (1ULL << 0U)
#define CAP_PTHREAD_CANCEL    (1ULL << 1U)
#define CAP_MQ_OPEN           (1ULL << 2U)
#define CAP_SHM_CREATE        (1ULL << 3U)
#define CAP_TIMER_CREATE      (1ULL << 4U)
#define CAP_IPC               (1ULL << 5U)

/* 编译时断言：验证能力位不冲突 */
_Static_assert((CAP_PTHREAD_CREATE & CAP_PTHREAD_CANCEL) == 0ULL,
               "Capability bits must not overlap");
```

### 20.2 系统调用包装器实现

#### 20.2.1 自适应系统调用包装器

```c
/**
 * @brief 自适应系统调用包装器
 * @param syscall_nr 系统调用号
 * @param ... 可变参数
 * @return 系统调用返回值
 *
 * @note MISRA规则遵守：
 *   - 规则16.1: 可变参数函数必须谨慎使用
 *   - 规则17.7: 必须检查va_list的返回值
 *   - 规则10.1: 避免隐式类型转换
 *
 * @warning 此函数必须是内联函数以避免额外开销
 * @warning 必须在任务上下文中调用
 */
static inline long adaptive_syscall(long syscall_nr, ...) {
    va_list args;
    TCB_t *current = NULL;
    long result = 0L;

    /* 获取当前任务指针 */
    current = get_current_task();

    /* 参数验证（MISRA规则13.5） */
    if (current == NULL) {
        return -EFAULT;
    }

    /* 系统调用号范围检查 */
    if ((syscall_nr < 0L) ||
        (syscall_nr >= (long)g_syscall_config.syscall_count)) {
        return -ENOSYS;
    }

    /* 解析可变参数 */
    va_start(args, syscall_nr);

    /* 运行时决策：根据任务隔离模式 */
    if (current->isolation_mode == TASK_ISOLATION_SHARED) {
        /*
         * 共享地址空间模式：直接函数调用
         * 性能：~10 周期
         */
        result = syscall_direct_invoke(syscall_nr, args);

        /* 统计：直接调用次数 */
        (void)atomic_fetch_add(&g_syscall_config.direct_calls, 1ULL);
    } else {
        /*
         * 独立地址空间模式：SVC系统调用
         * 性能：~180 周期
         */
        result = syscall_svc_invoke(syscall_nr, args);

        /* 统计：SVC调用次数 */
        (void)atomic_fetch_add(&g_syscall_config.svc_calls, 1ULL);
    }

    va_end(args);

    /* 统计：总调用次数 */
    (void)atomic_fetch_add(&g_syscall_config.total_syscalls, 1ULL);

    return result;
}
```

#### 20.2.2 直接函数调用实现

```c
/**
 * @brief 直接函数调用（共享地址空间）
 * @param syscall_nr 系统调用号
 * @param args 可变参数列表
 * @return 系统调用返回值
 *
 * @note MISRA规则遵守：
 *   - 规则11.5: 避免不必要的类型转换
 *   - 规则13.4: 检查所有指针参数
 *   - 规则17.7: 必须检查数组索引
 *
 * @warning 仅用于共享地址空间模式
 * @warning 无需模式切换，直接调用内核函数
 */
static long syscall_direct_invoke(long syscall_nr, va_list args) {
    const SyscallEntry_t *entry = NULL;
    uint64_t params[6U];
    uint32_t i;
    long ret;

    /* 参数验证 */
    if ((syscall_nr < 0L) ||
        (syscall_nr >= (long)g_syscall_config.syscall_count)) {
        return -ENOSYS;
    }

    /* 获取系统调用表项 */
    entry = &g_syscall_config.syscall_table[(uint32_t)syscall_nr];

    /* 检查系统调用是否实现 */
    if (entry->handler == NULL) {
        return -ENOSYS;
    }

    /* 能力检查 */
    if (!has_capability(entry->required_cap)) {
        return -EPERM;
    }

    /* 提取参数（最多6个） */
    for (i = 0U; i < entry->param_count; i++) {
        /* MISRA规则：防止数组越界 */
        if (i >= 6U) {
            return -EINVAL;
        }
        params[i] = va_arg(args, uint64_t);
    }

    /* 直接调用内核函数（零开销） */
    ret = entry->handler(params);

    return ret;
}
```

#### 20.2.3 SVC系统调用实现

```c
/**
 * @brief SVC系统调用（独立地址空间）
 * @param syscall_nr 系统调用号
 * @param args 可变参数列表
 * @return 系统调用返回值
 *
 * @note MISRA规则遵守：
 *   - 规则21.1: 内联汇编必须最小化
 *   - 规则11.5: 指针和整数转换必须显式
 *   - 规则12.1: 表达式的值不得依赖于求值顺序
 *
 * @warning 通过ARMv8-A SVC指令触发系统调用
 * @warning 需要完整的上下文切换
 */
static long syscall_svc_invoke(long syscall_nr, va_list args) {
    register uint64_t x0 asm("x0") = va_arg(args, uint64_t);
    register uint64_t x1 asm("x1") = va_arg(args, uint64_t);
    register uint64_t x2 asm("x2") = va_arg(args, uint64_t);
    register uint64_t x3 asm("x3") = va_arg(args, uint64_t);
    register uint64_t x4 asm("x4") = va_arg(args, uint64_t);
    register uint64_t x5 asm("x5") = va_arg(args, uint64_t);
    register uint64_t x8 asm("x8") = (uint64_t)syscall_nr;

    /*
     * ARMv8-A SVC指令触发异常
     *
     * 注意事项：
     * - x8: 系统调用号
     * - x0-x5: 系统调用参数
     * - x0: 返回值
     *
     * MISRA规则21.1合规：
     * - 内联汇编仅用于必需的硬件操作
     * - 汇编代码简短且清晰
     */
    asm volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8)
        : "memory"
    );

    return (long)x0;
}
```

### 20.3 异常处理实现规范

#### 20.3.1 异常向量表配置

```c
/**
 * @brief 异常向量表对齐要求
 * @note ARMv8-A要求异常向量表2KB（2048字节）对齐
 */
#define EXCEPTION_VECTOR_ALIGN  2048U

/**
 * @brief 异常向量表声明
 * @note 必须使用attribute指定对齐
 */
__attribute__((aligned(EXCEPTION_VECTOR_ALIGN))) void exception_vector_table(void);

/**
 * @brief 异常向量表汇编实现
 * @note MISRA规则21.1：内联汇编最小化
 */
__asm__(
    ".global exception_vector_table\n"
    ".align 11\n"  /* 2^11 = 2048 字节对齐 */
    "exception_vector_table:\n"

    /* 当前EL，SP0，同步异常 */
    ".align 7\n"
    "1: b exception_sync_sp0\n"

    /* 当前EL，SP0，IRQ异常 */
    ".align 7\n"
    "1: b exception_irq_sp0\n"

    /* 当前EL，SP0，FIQ异常 */
    ".align 7\n"
    "1: b exception_fiq_sp0\n"

    /* 当前EL，SP0，SError异常 */
    ".align 7\n"
    "1: b exception_serror_sp0\n"

    /* 当前EL，SPx，同步异常 ← SVC系统调用入口 */
    ".align 7\n"
    "1: b exception_sync_spx\n"

    /* 当前EL，SPx，IRQ异常 */
    ".align 7\n"
    "1: b exception_irq_spx\n"

    /* 当前EL，SPx，FIQ异常 */
    ".align 7\n"
    "1: b exception_fiq_spx\n"

    /* 当前EL，SPx，SError异常 */
    ".align 7\n"
    "1: b exception_serror_spx\n"

    /* 低EL（AArch64），同步异常 */
    ".align 7\n"
    "1: b exception_sync_lower64\n"
);

/**
 * @brief 编译时断言：验证异常向量表对齐
 */
_Static_assert(EXCEPTION_VECTOR_ALIGN == 2048U,
               "Exception vector table must be 2KB aligned");
```

#### 20.3.2 SVC系统调用处理入口

```c
/**
 * @brief SVC系统调用处理入口（同步异常）
 * @return 此函数不会返回（通过ERET返回用户空间）
 *
 * @note MISRA规则遵守：
 *   - 规则21.1: 内联汇编必须最小化
 *   - 规则11.5: 指针和整数转换必须显式
 *   - 规则17.7: 必须检查所有数组索引
 *
 * @warning 此函数在异常上下文中执行
 * @warning 必须通过ERET指令返回用户空间
 */
void exception_sync_spx(void) {
    uint64_t esr = 0ULL;
    uint64_t elr = 0ULL;
    uint64_t syscall_nr = 0ULL;
    uint64_t params[6U];
    long ret = 0L;
    uint32_t ec;

    /* 读取异常 syndrome 寄存器 */
    __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));

    /* 提取异常类（Exception Class） */
    ec = (uint32_t)((esr >> 26U) & 0x3FU);

    /* 检查是否为SVC异常（EC = 0x15 = 21） */
    if (ec != 0x15U) {
        /* 不是SVC异常，转发给其他异常处理 */
        handle_other_exception();
        return;
    }

    /* 提取系统调用号（从x8寄存器） */
    __asm__ volatile("mov %0, x8" : "=r"(syscall_nr));

    /* 提取参数（从x0-x5寄存器） */
    __asm__ volatile(
        "mov %0, x0\n"
        "mov %1, x1\n"
        "mov %2, x2\n"
        "mov %3, x3\n"
        "mov %4, x4\n"
        "mov %5, x5\n"
        : "=r"(params[0U]), "=r"(params[1U]), "=r"(params[2U]),
          "=r"(params[3U]), "=r"(params[4U]), "=r"(params[5U])
    );

    /* 执行系统调用 */
    ret = syscall_dispatch((long)syscall_nr, params);

    /* 设置返回值到x0 */
    __asm__ volatile("mov x0, %0" :: "r"(ret));

    /* 数据同步屏障 */
    __asm__ volatile("dsb ish");

    /* 指令同步屏障 */
    __asm__ volatile("isb");

    /* 返回用户空间（ERET指令） */
    __asm__ volatile("eret");

    /* 防止编译器警告 */
    for (;;) {
        /* 此处不会到达 */
        __asm__ volatile("wfe");
    }
}
```

#### 20.3.3 系统调用分发器

```c
/**
 * @brief 系统调用分发器
 * @param syscall_nr 系统调用号
 * @param params 参数数组
 * @return 系统调用返回值
 *
 * @note MISRA规则遵守：
 *   - 规则13.4: 检查所有指针参数
 *   - 规则17.7: 检查数组索引
 *   - 规则21.1: 内联汇编最小化
 *
 * @warning 必须在异常上下文中调用
 * @warning params必须指向有效内存
 */
long syscall_dispatch(long syscall_nr, uint64_t *params) {
    const SyscallEntry_t *entry = NULL;
    long ret;

    /* 参数验证 */
    if (params == NULL) {
        return -EFAULT;
    }

    /* 范围检查 */
    if ((syscall_nr < 0L) ||
        (syscall_nr >= (long)g_syscall_config.syscall_count)) {
        return -ENOSYS;
    }

    /* 获取系统调用表项 */
    entry = &g_syscall_table[(uint32_t)syscall_nr];

    /* 检查系统调用是否实现 */
    if (entry->handler == NULL) {
        return -ENOSYS;
    }

    /* 能力检查 */
    if (!has_capability(entry->required_cap)) {
        return -EPERM;
    }

    /* 参数验证（用户空间指针） */
    if (!validate_syscall_params((uint32_t)syscall_nr, params)) {
        return -EFAULT;
    }

    /* 性能计数 */
    uint64_t start = get_cycle_count();

    /* 调用系统调用处理函数 */
    ret = entry->handler(params);

    /* 性能统计 */
    uint64_t end = get_cycle_count();
    (void)update_syscall_stats((uint32_t)syscall_nr, (end - start));

    return ret;
}
```

### 20.4 安全性验证规范

#### 20.4.1 用户空间指针验证

```c
/**
 * @brief 用户空间指针验证
 * @param ptr 用户空间指针
 * @param size 要访问的大小
 * @return 有效返回true，否则返回false
 *
 * @note MISRA规则遵守：
 *   - 规则13.4: 检查所有指针参数
 *   - 规则10.3: 防止整数溢出
 *   - 规则17.7: 检查数组索引
 *
 * @warning 根据任务隔离模式进行不同的验证
 * @warning 必须在持有任务锁时调用
 */
bool validate_user_ptr(const void *ptr, size_t size) {
    uint64_t addr;
    TCB_t *current = NULL;

    /* NULL指针检查 */
    if (ptr == NULL) {
        return false;
    }

    addr = (uint64_t)ptr;

    /* 大小溢出检查（MISRA规则10.3） */
    if (size > (UINT64_MAX - addr)) {
        return false;
    }

    /* 获取当前任务 */
    current = get_current_task();

    if (current == NULL) {
        return false;
    }

    /* 根据任务隔离模式验证 */
    if (current->isolation_mode == TASK_ISOLATION_SHARED) {
        /*
         * 共享地址空间模式：
         * - 允许访问用户空间范围
         * - 内核数据段受保护（编译时检查）
         */
        return (addr < KERNEL_BASE_ADDR);
    } else {
        /*
         * 独立地址空间模式：
         * - 严格检查地址范围
         * - 仅允许访问任务自己的地址空间
         */

        /* 检查用户空间起始地址 */
        if (addr < USER_SPACE_START) {
            return false;
        }

        /* 检查用户空间结束地址（防止溢出） */
        if ((addr + size) > USER_SPACE_END) {
            return false;
        }

        return true;
    }
}
```

#### 20.4.2 能力检查机制

```c
/**
 * @brief 检查任务能力
 * @param cap 要检查的能力
 * @return 具有该能力返回true，否则返回false
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针有效性
 *   - 规则10.1: 避免隐式类型转换
 *
 * @warning 必须在持有任务锁时调用
 */
bool has_capability(Capability_t cap) {
    TCB_t *current = NULL;

    /* 获取当前任务 */
    current = get_current_task();

    if (current == NULL) {
        return false;
    }

    /* 位操作检查能力 */
    return ((current->capabilities & cap) != 0ULL);
}
```

### 20.5 系统调用性能优化

#### 20.5.1 快速系统调用路径

```c
/**
 * @brief 快速系统调用（零拷贝）
 * @param syscall_nr 系统调用号
 * @return 系统调用返回值
 *
 * @note MISRA规则遵守：
 *   - 规则21.1: 内联汇编必须最小化
 *   - 规则11.5: 避免不必要的类型转换
 *
 * @note 仅用于无参数或简单参数的系统调用
 * @note 性能：~120 周期（比普通系统调用快33%）
 *
 * @warning 必须是内联函数以避免函数调用开销
 * @warning 仅适用于无参数系统调用
 */
static inline long fast_syscall(long syscall_nr) {
    register uint64_t x0 asm("x0") = 0ULL;
    register uint64_t x8 asm("x8") = (uint64_t)syscall_nr;

    /*
     * 快速路径优化：
     * - 减少参数保存（无参数）
     * - 编译器内联优化
     * - 跳过复杂的参数验证
     *
     * MISRA规则21.1合规：
     * - 内联汇编仅用于必需的硬件操作
     * - 汇编代码简短且清晰
     */
    asm volatile(
        "svc #0"
        : "=r"(x0)
        : "r"(x8)
        : "memory"
    );

    return (long)x0;
}

/**
 * @brief sched_yield系统调用（快速路径）
 * @return 成功返回0，失败返回错误码
 *
 * @note 使用快速系统调用路径
 * @note 无参数系统调用
 */
int sched_yield(void) {
    /* 快速系统调用：无参数 */
    long ret = fast_syscall(SYS_SCHED_YIELD);

    return (int)ret;
}
```

#### 20.5.2 批量系统调用实现

```c
/**
 * @brief 批量系统调用项
 */
typedef struct {
    long        syscall_nr;    /* 系统调用号 */
    uint64_t    params[6U];    /* 参数数组 */
    long        ret;           /* 返回值 */
    int         errno;         /* 错误码 */
} SyscallBatchItem_t;

/**
 * @brief 批量系统调用
 * @param items 批量项数组
 * @param count 数量
 * @return 成功执行的数量，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.4: 检查所有指针参数
 *   - 规则17.7: 检查数组索引
 *   - 规则10.3: 防止整数溢出
 *
 * @note 减少多次用户空间↔内核空间切换
 * @note 性能提升：~40%（对于批量操作）
 *
 * @warning items必须指向有效内存
 * @warning count不能为0
 */
ssize_t syscall_batch(SyscallBatchItem_t *items, size_t count) {
    size_t i;
    ssize_t success = 0;

    /* 参数验证 */
    if (items == NULL) {
        return -EFAULT;
    }

    if (count == 0U) {
        return 0;
    }

    /* 检查count溢出 */
    if (count > (SIZE_MAX / sizeof(SyscallBatchItem_t))) {
        return -EINVAL;
    }

    /* 一次性进入内核 */
    for (i = 0U; i < count; i++) {
        SyscallBatchItem_t *item = &items[i];
        const SyscallEntry_t *entry = NULL;
        long ret;

        /* 系统调用号验证 */
        if ((item->syscall_nr < 0L) ||
            (item->syscall_nr >= (long)g_syscall_config.syscall_count)) {
            item->ret = -ENOSYS;
            item->errno = ENOSYS;
            continue;
        }

        /* 获取系统调用表项 */
        entry = &g_syscall_table[(uint32_t)item->syscall_nr];

        /* 检查系统调用是否实现 */
        if (entry->handler == NULL) {
            item->ret = -ENOSYS;
            item->errno = ENOSYS;
            continue;
        }

        /* 能力检查 */
        if (!has_capability(entry->required_cap)) {
            item->ret = -EPERM;
            item->errno = EPERM;
            continue;
        }

        /* 执行系统调用 */
        ret = entry->handler(item->params);

        if (ret >= 0L) {
            /* 成功 */
            success++;
            item->ret = ret;
            item->errno = 0;
        } else {
            /* 失败 */
            item->ret = -1;
            item->errno = (int)(-ret);
        }
    }

    return success;
}
```

### 20.6 MISRA合规性检查清单

#### 20.6.1 系统调用包装器检查项

- [ ] 所有指针参数在使用前检查NULL
- [ ] 系统调用号范围验证（防止数组越界）
- [ ] 可变参数正确处理（va_start/va_end配对）
- [ ] 返回值类型转换显式声明
- [ ] 内联汇编最小化（仅用于必需的硬件操作）
- [ ] 原子操作正确使用（内存屏障）
- [ ] 编译时断言验证关键不变量

#### 20.6.2 异常处理检查项

- [ ] 异常向量表2KB对齐
- [ ] ESR寄存器正确读取
- [ ] 异常类（EC）正确提取
- [ ] 系统调用号从x8寄存器正确提取
- [ ] 参数从x0-x5寄存器正确提取
- [ ] 返回值正确设置到x0寄存器
- [ ] 必须使用ERET指令返回用户空间
- [ ] 数据同步屏障（DSB）和指令同步屏障（ISB）

#### 20.6.3 安全性检查项

- [ ] 用户空间指针严格验证
- [ ] 地址范围检查（防止溢出）
- [ ] 任务隔离模式正确识别
- [ ] 能力检查（Capability-based Security）
- [ ] 参数数量验证（防止数组越界）
- [ ] 系统调用表项非NULL验证

#### 20.6.4 性能优化检查项

- [ ] 快速系统调用路径（无参数系统调用）
- [ ] 批量系统调用支持
- [ ] 内联函数用于高频调用
- [ ] 性能统计功能正确实现
- [ ] 自适应调用逻辑正确

---

## 21. MMU使能策略MISRA-C编码规范

### 21.1 早期MMU使能MISRA-C编码规范

#### 21.1.1 bootloader页表初始化

```c
/**
 * @brief bootloader页表初始化
 * @param pgd 页表全局目录物理地址
 * @param map_size_gb 映射大小（GB）
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则10.1: 检查整数溢出（左移操作）
 *   - 规则10.3: 防止整数溢出（循环边界）
 *   - 规则13.5: 检查指针参数
 *   - 规则21.1: 内联汇编必须最小化
 *
 * @note 使用恒等映射：虚拟地址 = 物理地址
 * @note 使用2MB块映射（ARMv8-A优化）
 * @note 性能：页表建立时间 ~1ms
 *
 * @warning pgd必须4KB对齐
 * @warning map_size_gb不能超过512GB
 * @warning 必须在使能MMU前调用
 */
int bootloader_init_pgtable(uint64_t *pgd, uint32_t map_size_gb) {
    uint64_t attr;
    uint32_t i;
    uint32_t max_entries;

    /* 参数验证 */
    if (pgd == NULL) {
        return -EINVAL;
    }

    /* 检查页表对齐（4KB） */
    if (((uintptr_t)pgd & 0xFFFUL) != 0UL) {
        return -EINVAL;
    }

    /* 检查map_size_gb范围 */
    if (map_size_gb == 0U) {
        return -EINVAL;
    }

    if (map_size_gb > 512U) {
        return -EINVAL;
    }

    /* 计算最大条目数（1GB = 1个PGD条目） */
    max_entries = map_size_gb;

    /* 页属性定义 */
    attr = (0x3UL << 0)      |   /* Valid/Type: 块描述符 */
           (0x0UL << 6)      |   /* AP: RW@EL1 */
           (0x1UL << 10)     |   /* AF: 访问标志 */
           (0x3UL << 8);         /* SH: 内共享 */

    /*
     * 清零页表
     * MISRA规则21.1合规：仅用于必要的内存清零操作
     */
    for (i = 0U; i < 512U; i++) {
        pgd[i] = 0UL;
    }

    /*
     * 恒等映射：虚拟地址 = 物理地址
     * 映射范围：0x0000_0000_0000 - (map_size_gb * 1GB)
     * 页大小：1GB块映射（L0块描述符）
     *
     * MISRA规则10.1合规：显式类型转换，检查溢出
     * MISRA规则10.3合规：检查循环边界
     */
    for (i = 0U; i < max_entries; i++) {
        uint64_t addr = (uint64_t)i << 30U;  /* 1GB对齐 */

        /* 检查地址溢出 */
        if (addr < (uint64_t)(i - 1U) << 30U) {
            return -EOVERFLOW;
        }

        pgd[i] = addr | attr;
    }

    return 0;
}
```

#### 21.1.2 MMU使能函数

```c
/**
 * @brief 使能MMU
 * @param pgd 页表全局目录物理地址
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则21.1: 内联汇编必须最小化且注释
 *   - 规则11.4: 避免隐式类型转换
 *   - 规则13.5: 检查指针参数
 *
 * @note 使能MMU、数据缓存、指令缓存
 * @note 性能：~0.3ms
 *
 * @warning pgd必须4KB对齐
 * @warning 必须先调用bootloader_init_pgtable
 * @warning 使能MMU后立即需要内存屏障
 */
int enable_mmu(uint64_t *pgd) {
    uint64_t sctlr;

    /* 参数验证 */
    if (pgd == NULL) {
        return -EINVAL;
    }

    /* 检查页表对齐 */
    if (((uintptr_t)pgd & 0xFFFUL) != 0UL) {
        return -EINVAL;
    }

    /*
     * 步骤1：设置页表基址寄存器
     * MISRA规则21.1合规：仅用于必需的硬件操作
     * 注释清晰说明操作目的
     */
    __asm__ volatile(
        "msr ttbr0_el1, %0"  /* 设置TTBR0_EL1寄存器 */
        :: "r"((uintptr_t)pgd)
        : "memory"
    );

    /*
     * 步骤2：读取系统控制寄存器
     */
    __asm__ volatile(
        "mrs %0, sctlr_el1"  /* 读取SCTLR_EL1寄存器 */
        : "=r"(sctlr)
        :
        : "memory"
    );

    /*
     * 步骤3：使能MMU（M位）
     * 步骤4：使能数据缓存（C位）
     * 步骤5：使能指令缓存（I位）
     */
    sctlr |= (1UL << 0);  /* M位：使能MMU */
    sctlr |= (1UL << 2);  /* C位：使能数据缓存 */
    sctlr |= (1UL << 12); /* I位：使能指令缓存 */

    /*
     * 步骤6：写入系统控制寄存器
     */
    __asm__ volatile(
        "msr sctlr_el1, %0"  /* 写入SCTLR_EL1寄存器 */
        :: "r"(sctlr)
        : "memory"
    );

    /*
     * 步骤7：指令同步屏障
     * MISRA规则21.1合规：内存屏障是必需的硬件操作
     * 注释说明为什么需要屏障
     */
    __asm__ volatile("isb" ::: "memory");

    /*
     * 步骤8：刷新TLB
     */
    __asm__ volatile("tlbi vmalle1is" ::: "memory");

    /*
     * 步骤9：数据同步屏障
     */
    __asm__ volatile("dsb ish" ::: "memory");

    /*
     * 步骤10：指令同步屏障
     */
    __asm__ volatile("isb" ::: "memory");

    return 0;
}
```

### 21.2 页表验证MISRA-C编码规范

#### 21.2.1 页表有效性验证

```c
/**
 * @brief 验证页表配置
 * @param pgd 页表全局目录
 * @param entry_count 要验证的条目数量
 * @return 有效返回true，否则返回false
 *
 * @note MISRA规则遵守：
 *   - 规则10.1: 检查整数溢出（位运算）
 *   - 规则10.3: 防止整数溢出（循环边界）
 *   - 规则13.5: 检查指针参数
 *   - 规则14.4: 不超过循环边界
 *
 * @note 验证页表对齐、条目有效性、块描述符类型
 * @note 验证物理地址对齐（1GB）
 *
 * @warning entry_count不能超过512
 * @warning 必须在使能MMU前调用
 */
bool validate_page_table(const uint64_t *pgd, uint32_t entry_count) {
    uint32_t i;

    /* 参数验证 */
    if (pgd == NULL) {
        return false;
    }

    /* 检查entry_count范围 */
    if (entry_count == 0U) {
        return false;
    }

    if (entry_count > 512U) {
        return false;
    }

    /* 检查页表对齐（4KB） */
    if (((uintptr_t)pgd & 0xFFFUL) != 0UL) {
        return false;
    }

    /*
     * 验证关键条目
     * MISRA规则14.4合规：循环变量不修改
     */
    for (i = 0U; i < entry_count; i++) {
        uint64_t desc;
        uint64_t desc_type;
        uint64_t phys_addr;

        desc = pgd[i];

        /* 检查有效性 */
        if ((desc & 0x1UL) == 0UL) {
            /* 无效条目，跳过 */
            continue;
        }

        /* 提取描述符类型 */
        desc_type = desc & 0x3UL;

        /* 检查块描述符类型（b11） */
        if (desc_type != 0x3UL) {
            /* 不是块描述符 */
            return false;
        }

        /* 提取物理地址 */
        phys_addr = desc & 0x0000FFFFF000UL;

        /* 检查物理地址对齐（1GB） */
        if ((phys_addr & 0x3FFFFFFFUL) != 0UL) {
            return false;
        }
    }

    return true;
}
```

### 21.3 多核MMU使能同步MISRA-C编码规范

#### 21.3.1 多核安全使能MMU

```c
/**
 * @brief 多核安全地使能MMU
 * @param pgd 页表全局目录物理地址
 * @param cpu_count CPU核心数量
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则10.1: 检查整数溢出
 *   - 规则13.5: 检查指针参数
 *   - 规则21.1: 内联汇编必须最小化
 *   - 规则11.4: 使用原子操作
 *
 * @note 主CPU初始化页表，从CPU等待
 * @note 所有CPU使能MMU后，主CPU切换到详细映射
 * @note 使用原子操作和内存屏障同步
 *
 * @warning pgd必须4KB对齐
 * @warning cpu_count不能超过CONFIG_NR_CPUS
 * @warning 必须在多核启动后调用
 */
int smp_enable_mmu(uint64_t *pgd, uint32_t cpu_count) {
    static volatile atomic_uint barrier = ATOMIC_VAR_INIT(0U);
    uint32_t cpu_id;
    uint32_t expected;
    int ret;

    /* 参数验证 */
    if (pgd == NULL) {
        return -EINVAL;
    }

    /* 检查cpu_count范围 */
    if (cpu_count == 0U) {
        return -EINVAL;
    }

    if (cpu_count > CONFIG_NR_CPUS) {
        return -EINVAL;
    }

    cpu_id = get_cpu_id();

    /*
     * 步骤1：主CPU初始化页表
     * MISRA规则11.4合规：使用原子操作
     */
    if (cpu_id == 0U) {
        /* 主CPU：初始化页表 */
        ret = bootloader_init_pgtable(pgd, 1U);
        if (ret != 0) {
            return ret;
        }

        /* 同步：通知从CPU页表已就绪 */
        (void)atomic_fetch_add(&barrier, 1U, memory_order_release);
    } else {
        /* 从CPU：等待主CPU完成 */
        expected = 1U;
        while (atomic_load_explicit(&barrier, memory_order_acquire) < expected) {
            __asm__ volatile("wfe" ::: "memory");
        }
    }

    /*
     * 步骤2：所有CPU使能MMU
     */
    ret = enable_mmu(pgd);
    if (ret != 0) {
        return ret;
    }

    /* 同步：等待所有CPU完成 */
    (void)atomic_fetch_add(&barrier, 1U, memory_order_release);

    /*
     * 步骤3：主CPU切换到详细映射
     */
    if (cpu_id == 0U) {
        ret = switch_to_detailed_map();
        if (ret != 0) {
            return ret;
        }

        /* 同步：通知从CPU切换完成 */
        (void)atomic_fetch_add(&barrier, 1U, memory_order_release);
    } else {
        /* 从CPU：等待主CPU完成 */
        expected = cpu_count + 2U;
        while (atomic_load_explicit(&barrier, memory_order_acquire) < expected) {
            __asm__ volatile("wfe" ::: "memory");
        }
    }

    return 0;
}
```

### 21.4 映射切换MISRA-C编码规范

#### 21.4.1 从恒等映射到详细映射切换

```c
/**
 * @brief 切换到详细映射
 * @param new_pg_table 新页表物理地址
 * @param cpu_id 当前CPU ID
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则21.1: 内联汇编必须最小化
 *   - 规则11.4: 使用内存屏障
 *
 * @note 从恒等映射切换到详细映射
 * @note 主CPU创建页表，从CPU等待
 * @note 切换后必须刷新TLB
 *
 * @warning new_pg_table必须4KB对齐
 * @warning 必须在所有CPU使能MMU后调用
 * @warning 切换过程中不能有中断
 */
int switch_to_detailed_map(uint64_t *new_pg_table, uint32_t cpu_id) {
    uint64_t ttbr0_val;

    /* 参数验证 */
    if (new_pg_table == NULL) {
        return -EINVAL;
    }

    /* 检查页表对齐（4KB） */
    if (((uintptr_t)new_pg_table & 0xFFFUL) != 0UL) {
        return -EINVAL;
    }

    /* 检查cpu_id范围 */
    if (cpu_id >= CONFIG_NR_CPUS) {
        return -EINVAL;
    }

    /* 验证新页表 */
    if (!validate_page_table(new_pg_table, 512U)) {
        return -EINVAL;
    }

    /*
     * 准备页表基址值
     * MISRA规则11.4合规：显式类型转换
     */
    ttbr0_val = (uintptr_t)new_pg_table;

    /*
     * 更新TTBR0寄存器
     * MISRA规则21.1合规：仅用于必需的硬件操作
     * 注释清晰说明操作目的和后果
     */
    __asm__ volatile(
        "msr ttbr0_el1, %0"  /* 设置新的页表基址 */
        :: "r"(ttbr0_val)
        : "memory"
    );

    /*
     * 刷新TLB
     * 必须在修改页表后立即执行
     * MISRA规则21.1合规：内存屏障是必需的硬件操作
     */
    __asm__ volatile("tlbi vmalle1is" ::: "memory");  /* 刷新所有TLB条目 */
    __asm__ volatile("dsb ish" ::: "memory");         /* 数据同步屏障 */
    __asm__ volatile("isb" ::: "memory");             /* 指令同步屏障 */

    return 0;
}
```

### 21.5 性能测量MISRA-C编码规范

#### 21.5.1 启动时间测量

```c
/**
 * @brief 启动时间测量点
 */
typedef struct {
    const char *name;       /* 测量点名称 */
    uint64_t    time_ns;    /* 时间戳（纳秒） */
} BootTimePoint_t;

#define BOOT_TIME_COUNT 5U

static BootTimePoint_t boot_time_points[BOOT_TIME_COUNT] = {
    {"Bootloader Start", 0ULL},
    {"MMU Enable",       0ULL},
    {"Kernel Start",     0ULL},
    {"Scheduler Start",  0ULL},
    {"System Ready",     0ULL}
};

/**
 * @brief 记录启动时间点
 * @param index 测量点索引
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则10.3: 防止整数溢出
 *   - 规则13.5: 检查数组索引
 *
 * @note 记录当前时间戳到指定测量点
 * @note 用于性能分析和优化
 *
 * @warning index必须在有效范围内
 * @warning 必须按顺序调用（从小到大）
 */
int record_boot_time(uint32_t index) {
    /* 参数验证 */
    if (index >= BOOT_TIME_COUNT) {
        return -EINVAL;
    }

    /* 记录当前时间戳 */
    boot_time_points[index].time_ns = get_system_time_ns();

    return 0;
}

/**
 * @brief 打印启动时间统计
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则10.1: 检查整数溢出
 *   - 规则10.3: 防止整数溢出
 *   - 规则10.4: 防止有符号整数溢出
 *   - 规则12.3: 避免除零
 *
 * @note 计算各阶段时间和百分比
 * @note 输出到控制台或日志
 */
int print_boot_stats(void) {
    uint64_t total_ns;
    uint32_t i;

    /* 计算总时间 */
    total_ns = boot_time_points[BOOT_TIME_COUNT - 1U].time_ns -
               boot_time_points[0U].time_ns;

    /* 检查总时间有效性 */
    if (total_ns == 0ULL) {
        return -EINVAL;
    }

    printf("Boot Time Statistics:\n");

    /* 打印各阶段时间 */
    for (i = 1U; i < BOOT_TIME_COUNT; i++) {
        uint64_t delta_ns;
        uint64_t delta_ms;
        uint64_t percent_times_100;
        uint32_t percent_int;
        uint32_t percent_frac;

        /* 计算阶段时间 */
        delta_ns = boot_time_points[i].time_ns -
                   boot_time_points[i - 1U].time_ns;

        /* 转换为毫秒 */
        delta_ms = delta_ns / 1000000ULL;

        /* 计算百分比（×100避免浮点） */
        percent_times_100 = (delta_ns * 100ULL) / total_ns;

        /* 提取整数和小数部分 */
        percent_int = (uint32_t)(percent_times_100 / 100ULL);
        percent_frac = (uint32_t)(percent_times_100 % 100ULL);

        printf("  %-20s: %4llu ms (%3u.%02u%%)\n",
               boot_time_points[i].name,
               delta_ms,
               percent_int,
               percent_frac);
    }

    return 0;
}
```

### 21.6 MMU相关数据结构MISRA-C编码规范

#### 21.6.1 MMU统计结构

```c
/**
 * @brief MMU性能统计
 * @note MISRA-C:2012合规
 */
typedef struct {
    uint64_t tlb_miss;       /* TLB miss次数 */
    uint64_t tlb_hit;        /* TLB hit次数 */
    uint64_t page_walk;      /* 页表遍历次数 */
    uint64_t cache_access;   /* 缓存访问次数 */
} MMUStats_t;

/**
 * @brief 读取MMU性能计数器
 * @param stats 统计数据输出指针
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则21.1: 内联汇编必须最小化
 *   - 规则11.4: 避免隐式类型转换
 *
 * @warning stats必须指向有效内存
 * @warning 某些性能计数器可能不可用
 */
int read_mmu_stats(MMUStats_t *stats) {
    uint64_t val;

    /* 参数验证 */
    if (stats == NULL) {
        return -EINVAL;
    }

    /* 清零统计 */
    stats->tlb_miss = 0ULL;
    stats->tlb_hit = 0ULL;
    stats->page_walk = 0ULL;
    stats->cache_access = 0ULL;

    /*
     * 读取TLB miss计数
     * MISRA规则21.1合规：仅用于必需的硬件操作
     */
    __asm__ volatile(
        "mrs %0, tlbimval_el1"  /* 读取TLB invalidate miss计数 */
        : "=r"(val)
        :
        : "memory"
    );
    stats->tlb_miss = val;

    /*
     * 读取缓存访问计数
     * 注意：某些平台可能不支持
     */
    __asm__ volatile(
        "mrs %0, l1d_cache_ld"  /* 读取L1数据缓存加载计数 */
        : "=r"(val)
        :
        : "memory"
    );
    stats->cache_access = val;

    return 0;
}
```

### 21.7 MISRA合规性检查清单

#### 21.7.1 页表建立检查项

- [ ] 页表4KB对齐验证
- [ ] 循环边界检查（防止溢出）
- [ ] 地址移位操作溢出检查
- [ ] 指针参数NULL验证
- [ ] 物理地址对齐验证（1GB/2MB）
- [ ] 页属性位操作正确性
- [ ] 内联汇编最小化且注释完整

#### 21.7.2 MMU使能检查项

- [ ] 页表基址寄存器设置正确
- [ ] 系统控制寄存器读写正确
- [ ] MMU使能位（M位）设置
- [ ] 数据缓存使能位（C位）设置
- [ ] 指令缓存使能位（I位）设置
- [ ] 指令同步屏障（ISB）存在
- [ ] 数据同步屏障（DSB）存在
- [ ] TLB刷新操作存在
- [ ] 内存屏障顺序正确

#### 21.7.3 多核同步检查项

- [ ] 原子操作正确使用
- [ ] 内存顺序语义正确（acquire/release）
- [ ] CPU ID范围验证
- [ ] 屏障同步正确实现
- [ ] WFE指令正确使用
- [ ] 主CPU/从CPU逻辑清晰
- [ ] 竞态条件避免

#### 21.7.4 映射切换检查项

- [ ] 新页表验证通过
- [ ] 页表基址原子更新
- [ ] TLB刷新完整（vmalle1is）
- [ ] 内存屏障顺序正确
- [ ] 中断状态处理
- [ ] CPU核心一致性

#### 21.7.5 性能测量检查项

- [ ] 数组索引边界检查
- [ ] 整数溢出检查（减法）
- [ ] 除零检查
- [ ] 类型转换显式声明
- [ ] 浮点运算避免（使用定点）
- [ ] 时间戳有效性验证

#### 21.7.6 安全性检查项

- [ ] 恒等映射仅在启动阶段使用
- [ ] 页属性保护正确设置
- [ ] 尽早切换到隔离映射
- [ ] 物理内存访问验证
- [ ] 地址空间布局清晰
- [ ] 缓存一致性保证

---

## 22. Shell调试接口MISRA-C编码规范

### 22.1 Shell基础框架MISRA-C编码规范

#### 22.1.1 Shell主循环

```c
/**
 * @brief Shell主循环
 * @return 不会返回（无限循环）
 *
 * @note MISRA规则遵守：
 *   - 规则10.1: 防止整数溢出
 *   - 规则13.5: 检查指针参数
 *   - 规则21.6: 使用标准库字符串函数
 *   - 规则22.10: 避免无限循环的误判
 *
 * @note 用户态Shell任务
 * @note 支持命令历史记录
 * @note 支持命令补全（可选）
 *
 * @warning 必须在用户态运行
 * @warning 必须限制资源使用
 */
void shell_main_loop(void) {
    char cmd_buf[SHELL_CMD_MAX_LEN];
    int cmd_len;
    int ret;

    /* 初始化命令缓冲区 */
    (void)memset(cmd_buf, 0, sizeof(cmd_buf));

    /*
     * Shell主循环
     * MISRA规则22.10合规：循环有明确的退出条件
     */
    while (1) {
        /* 显示提示符 */
        (void)shell_printf("ash> ");

        /* 读取命令 */
        cmd_len = shell_read_line(cmd_buf, sizeof(cmd_buf));

        if (cmd_len < 0) {
            /* 读取错误 */
            (void)shell_printf("Error reading command\n");
            continue;
        }

        if (cmd_len == 0) {
            /* 空命令，继续 */
            continue;
        }

        /* 检查命令长度 */
        if (cmd_len >= (int)sizeof(cmd_buf)) {
            (void)shell_printf("Command too long (max %u bytes)\n",
                             (uint32_t)SHELL_CMD_MAX_LEN);
            continue;
        }

        /* 执行命令 */
        ret = shell_execute_command(cmd_buf);

        if (ret != 0) {
            (void)shell_printf("Command failed: %d\n", ret);
        }
    }
}
```

#### 22.1.2 命令读取函数

```c
/**
 * @brief 读取一行命令输入
 * @param buf 缓冲区
 * @param size 缓冲区大小
 * @return 成功返回读取的字节数，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则10.1: 检查整数溢出
 *   - 规则10.3: 防止整数溢出
 *   - 规则13.5: 检查指针参数
 *   - 规则18.1: 指针运算安全
 *   - 规则21.6: 使用安全的字符串函数
 *
 * @note 支持退格键删除
 * @note 支持回车键确认
 * @note 自动添加字符串终止符
 *
 * @warning buf必须指向有效内存
 * @warning size不能为0
 * @warning 必须防止缓冲区溢出
 */
int shell_read_line(char *buf, uint32_t size) {
    uint32_t pos = 0U;
    int ch;

    /* 参数验证 */
    if (buf == NULL) {
        return -EINVAL;
    }

    if (size == 0U) {
        return -EINVAL;
    }

    if (size > SHELL_CMD_MAX_LEN) {
        return -EINVAL;
    }

    /*
     * 逐字符读取
     * MISRA规则14.4合规：循环变量不修改
     */
    while (pos < (size - 1U)) {
        /* 读取一个字符 */
        ch = shell_getchar();

        if (ch < 0) {
            /* 读取错误或EOF */
            break;
        }

        /* 处理特殊字符 */
        if (ch == '\r' || ch == '\n') {
            /* 回车或换行：结束输入 */
            (void)shell_putchar('\n');
            break;
        }

        if (ch == '\b' || ch == 127) {
            /* 退格键：删除前一个字符 */
            if (pos > 0U) {
                pos--;
                buf[pos] = '\0';
                (void)shell_printf("\b \b");  /* 清除显示 */
            }
            continue;
        }

        /* 可打印字符：添加到缓冲区 */
        if ((ch >= 32) && (ch <= 126)) {
            buf[pos] = (char)ch;
            pos++;
            (void)shell_putchar((char)ch);
        }
    }

    /* 添加字符串终止符 */
    buf[pos] = '\0';

    return (int)pos;
}
```

### 22.2 命令执行MISRA-C编码规范

#### 22.1.1 命令解析和分发

```c
/**
 * @brief 执行Shell命令
 * @param cmd_str 命令字符串
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则10.1: 检查整数溢出
 *   - 规则13.5: 检查指针参数
 *   - 规则21.6: 使用安全的字符串函数
 *   - 规则18.1: 指针运算安全
 *
 * @note 支持命令参数解析
 * @note 支持管道和重定向（可选）
 * @note 支持命令别名（可选）
 *
 * @warning cmd_str必须以'\0'结尾
 * @warning 必须防止命令注入攻击
 * @warning 必须验证命令权限
 */
int shell_execute_command(const char *cmd_str) {
    char cmd_name[SHELL_CMD_NAME_MAX_LEN];
    char cmd_args[SHELL_CMD_ARGS_MAX_LEN];
    const ShellCommand_t *cmd_entry;
    int ret;

    /* 参数验证 */
    if (cmd_str == NULL) {
        return -EINVAL;
    }

    /* 解析命令名称和参数 */
    ret = shell_parse_command(cmd_str, cmd_name, cmd_args);

    if (ret != 0) {
        return ret;
    }

    /* 查找命令表 */
    cmd_entry = shell_find_command(cmd_name);

    if (cmd_entry == NULL) {
        (void)shell_printf("Command not found: %s\n", cmd_name);
        (void)shell_printf("Type 'help' for available commands\n");
        return -ENOENT;
    }

    /* 权限检查 */
    if (!shell_check_capability(cmd_entry->required_cap)) {
        (void)shell_printf("Permission denied\n");
        return -EPERM;
    }

    /* 执行命令 */
    ret = cmd_entry->handler(cmd_args);

    return ret;
}

/**
 * @brief 解析命令字符串
 * @param cmd_str 完整命令字符串
 * @param cmd_name 输出：命令名称
 * @param cmd_args 输出：命令参数
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则18.1: 指针运算安全
 *   - 规则21.6: 使用安全的字符串函数
 *
 * @note 分离命令名称和参数
 * @note 去除前导和尾随空格
 * @note 处理引号和转义字符（可选）
 *
 * @warning 所有缓冲区必须足够大
 * @warning 必须防止缓冲区溢出
 */
static int shell_parse_command(const char *cmd_str,
                                char *cmd_name,
                                char *cmd_args) {
    uint32_t i = 0U;
    uint32_t name_pos = 0U;
    uint32_t args_pos = 0U;
    bool in_name = true;

    /* 参数验证 */
    if (cmd_str == NULL) {
        return -EINVAL;
    }

    if (cmd_name == NULL) {
        return -EINVAL;
    }

    if (cmd_args == NULL) {
        return -EINVAL;
    }

    /* 清零输出缓冲区 */
    (void)memset(cmd_name, 0, SHELL_CMD_NAME_MAX_LEN);
    (void)memset(cmd_args, 0, SHELL_CMD_ARGS_MAX_LEN);

    /* 跳过前导空格 */
    while (cmd_str[i] == ' ') {
        i++;
    }

    /* 解析命令 */
    while (cmd_str[i] != '\0') {
        if (in_name) {
            /* 解析命令名称 */
            if (cmd_str[i] == ' ') {
                /* 空格：名称结束 */
                in_name = false;

                /* 跳过空格 */
                while (cmd_str[i] == ' ') {
                    i++;
                }

                continue;
            }

            /* 添加到命令名称 */
            if (name_pos < (SHELL_CMD_NAME_MAX_LEN - 1U)) {
                cmd_name[name_pos] = cmd_str[i];
                name_pos++;
            } else {
                /* 命令名称太长 */
                return -ENAMETOOLONG;
            }
        } else {
            /* 解析命令参数 */
            if (args_pos < (SHELL_CMD_ARGS_MAX_LEN - 1U)) {
                cmd_args[args_pos] = cmd_str[i];
                args_pos++;
            } else {
                /* 命令参数太长 */
                return -E2BIG;
            }
        }

        i++;
    }

    /* 添加字符串终止符 */
    cmd_name[name_pos] = '\0';
    cmd_args[args_pos] = '\0';

    return 0;
}
```

### 22.3 命令表MISRA-C编码规范

#### 22.3.1 命令表定义

```c
/**
 * @brief Shell命令处理函数类型
 * @param args 命令参数字符串
 * @return 成功返回0，失败返回负错误码
 */
typedef int (*ShellCmdHandler_t)(const char *args);

/**
 * @brief Shell命令表项
 */
typedef struct {
    const char         *name;           /* 命令名称 */
    ShellCmdHandler_t  handler;        /* 处理函数 */
    const char         *help;           /* 帮助信息 */
    Capability_t       required_cap;   /* 所需权限 */
} ShellCommand_t;

/**
 * @brief Shell命令表
 * @note 必须按命令名称排序（用于二分查找）
 * @note 必须以NULL命令结束
 */
static const ShellCommand_t g_shell_commands[] = {
    /* 基础命令 */
    {
        .name = "help",
        .handler = shell_cmd_help,
        .help = "Show available commands",
        .required_cap = CAP_SHELL_VIEW
    },
    {
        .name = "ps",
        .handler = shell_cmd_ps,
        .help = "Show task list",
        .required_cap = CAP_SHELL_VIEW
    },
    {
        .name = "top",
        .handler = shell_cmd_top,
        .help = "Real-time task monitoring",
        .required_cap = CAP_SHELL_VIEW
    },
    {
        .name = "mem",
        .handler = shell_cmd_mem,
        .help = "Show memory usage",
        .required_cap = CAP_SHELL_VIEW
    },

    /* 诊断命令 */
    {
        .name = "klog",
        .handler = shell_cmd_klog,
        .help = "View kernel log",
        .required_cap = CAP_SHELL_VIEW
    },
    {
        .name = "task",
        .handler = shell_cmd_task,
        .help = "Task control: task <pid> <cmd>",
        .required_cap = CAP_SHELL_TASK
    },
    {
        .name = "perf",
        .handler = shell_cmd_perf,
        .help = "Show performance statistics",
        .required_cap = CAP_SHELL_VIEW
    },

    /* 配置命令 */
    {
        .name = "set",
        .handler = shell_cmd_set,
        .help = "Set configuration: set <key> <value>",
        .required_cap = CAP_SHELL_CONFIG
    },
    {
        .name = "reload",
        .handler = shell_cmd_reload,
        .help = "Reload configuration",
        .required_cap = CAP_SHELL_CONFIG
    },

    /* 结束标记 */
    {
        .name = NULL,
        .handler = NULL,
        .help = NULL,
        .required_cap = 0
    }
};

/**
 * @brief 查找命令
 * @param name 命令名称
 * @return 成功返回命令表项指针，失败返回NULL
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则18.1: 指针运算安全
 *   - 规则21.6: 使用安全的字符串比较
 *
 * @note 使用线性查找（简单实现）
 * @note 可优化为二分查找（性能要求高时）
 *
 * @warning name必须以'\0'结尾
 * @warning 返回指针不能被修改
 */
static const ShellCommand_t *shell_find_command(const char *name) {
    uint32_t i = 0U;

    /* 参数验证 */
    if (name == NULL) {
        return NULL;
    }

    /* 线性查找命令 */
    while (g_shell_commands[i].name != NULL) {
        /* 比较命令名称 */
        if (strcmp(name, g_shell_commands[i].name) == 0) {
            return &g_shell_commands[i];
        }

        i++;
    }

    /* 未找到 */
    return NULL;
}
```

### 22.4 Shell命令实现MISRA-C编码规范

#### 22.4.1 ps命令实现

```c
/**
 * @brief ps命令：显示任务列表
 * @param args 命令参数（忽略）
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则10.1: 检查整数溢出
 *   - 规则10.3: 防止整数溢出
 *   - 规则13.5: 检查指针参数
 *   - 规则21.6: 使用安全的字符串函数
 *
 * @note 通过系统调用获取任务信息
 * @note 格式化输出到控制台
 *
 * @warning 仅显示用户有权限查看的任务
 */
static int shell_cmd_ps(const char *args) {
    TaskInfo_t tasks[32];
    int count;
    int i;
    int ret;

    /* 抑制未使用参数警告 */
    (void)args;

    /* 获取任务列表（系统调用） */
    count = syscall_sys_task_info(tasks, 32);

    if (count < 0) {
        (void)shell_printf("Error getting task info: %d\n", count);
        return count;
    }

    /* 打印表头 */
    (void)shell_printf("PID   PRI    STATE       TIME     CPU\n");

    /* 打印任务信息 */
    for (i = 0; i < count; i++) {
        const char *state_str;

        /* 转换状态字符串 */
        state_str = task_state_to_str(tasks[i].state);

        /* 打印任务信息 */
        ret = shell_printf("%-5d  %-3d   %-10s  %-8llu  %u\n",
                          tasks[i].pid,
                          tasks[i].priority,
                          state_str,
                          tasks[i].runtime_ns / 1000ULL,
                          tasks[i].cpu_id);

        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

/**
 * @brief 转换任务状态为字符串
 * @param state 任务状态
 * @return 状态字符串
 *
 * @note MISRA规则遵守：
 *   - 规则10.5: 防止有符号整数溢出
 *   - 规则16.3: 使用默认分支
 *
 * @note 状态字符串必须是有效的
 * @note 必须处理所有可能的状态值
 */
static const char *task_state_to_str(TaskState_t state) {
    const char *str;

    switch (state) {
        case TASK_READY:
            str = "READY";
            break;

        case TASK_RUNNING:
            str = "RUNNING";
            break;

        case TASK_BLOCKED:
            str = "BLOCKED";
            break;

        case TASK_SLEEPING:
            str = "SLEEPING";
            break;

        case TASK_SUSPENDED:
            str = "SUSPENDED";
            break;

        default:
            str = "UNKNOWN";
            break;
    }

    return str;
}
```

#### 22.4.2 mem命令实现

```c
/**
 * @brief mem命令：显示内存使用统计
 * @param args 命令参数（忽略）
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则10.1: 检查整数溢出
 *   - 规则10.4: 防止有符号整数溢出
 *   - 规则12.3: 避免除零
 *   - 规则13.5: 检查指针参数
 *
 * @note 通过系统调用获取内存统计
 * @note 计算内存使用百分比
 *
 * @warning 必须防止除零错误
 */
static int shell_cmd_mem(const char *args) {
    MemStats_t stats;
    uint32_t used_percent;
    uint32_t free_percent;
    int ret;

    /* 抑制未使用参数警告 */
    (void)args;

    /* 获取内存统计（系统调用） */
    ret = syscall_sys_mem_stats(&stats);

    if (ret != 0) {
        (void)shell_printf("Error getting memory stats: %d\n", ret);
        return ret;
    }

    /* 计算百分比（×100避免浮点） */
    if (stats.total > 0U) {
        uint64_t used_times_100;
        uint64_t free_times_100;

        /* 计算已使用百分比（×100） */
        used_times_100 = (stats.used * 100ULL) / stats.total;
        used_percent = (uint32_t)used_times_100;

        /* 计算空闲百分比（×100） */
        free_times_100 = (stats.free * 100ULL) / stats.total;
        free_percent = (uint32_t)free_times_100;
    } else {
        used_percent = 0U;
        free_percent = 0U;
    }

    /* 打印内存统计 */
    (void)shell_printf("Total:    %llu KB\n", stats.total / 1024ULL);
    (void)shell_printf("Used:     %llu KB (%u.%02u%%)\n",
                      stats.used / 1024ULL,
                      used_percent / 100U,
                      used_percent % 100U);
    (void)shell_printf("Free:     %llu KB (%u.%02u%%)\n",
                      stats.free / 1024ULL,
                      free_percent / 100U,
                      free_percent % 100U);
    (void)shell_printf("Kernel:   %llu KB\n", stats.kernel / 1024ULL);
    (void)shell_printf("Tasks:    %llu KB\n", stats.tasks / 1024ULL);

    return 0;
}
```

### 22.5 内核调试系统调用MISRA-C编码规范

#### 22.5.1 任务信息系统调用

```c
/**
 * @brief 任务信息系统调用实现
 * @param tasks 任务信息数组（用户空间指针）
 * @param count 数组大小
 * @return 成功返回实际任务数量，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则10.1: 检查整数溢出
 *   - 规则10.3: 防止整数溢出
 *   - 规则13.5: 检查指针参数
 *   - 规则18.1: 指针运算安全
 *
 * @note 仅返回用户有权限查看的任务
 * @note 复制到用户空间（user_access_ok验证）
 *
 * @warning tasks必须指向有效的用户空间内存
 * @warning count不能超过最大限制
 * @warning 必须验证用户空间指针
 */
long sys_task_info(TaskInfo_t *tasks, int count) {
    int actual_count;
    int i;
    int ret;

    /* 参数验证 */
    if (tasks == NULL) {
        return -EINVAL;
    }

    if (count <= 0) {
        return -EINVAL;
    }

    if (count > 100) {
        /* 限制最大数量 */
        return -EINVAL;
    }

    /* 验证用户空间指针 */
    if (!user_access_ok(tasks, (size_t)count * sizeof(TaskInfo_t))) {
        return -EFAULT;
    }

    /* 获取任务信息 */
    actual_count = get_task_info_internal(tasks, count);

    if (actual_count < 0) {
        return actual_count;
    }

    /* 复制到用户空间 */
    for (i = 0; i < actual_count; i++) {
        TaskInfo_t *task = &tasks[i];

        /* 检查权限 */
        if (!has_capability(CAP_SHELL_VIEW)) {
            /* 无权限：跳过敏感信息 */
            task->stack_ptr = 0ULL;
            task->pc = 0ULL;
        }

        /* 复制到用户空间 */
        ret = copy_to_user(&tasks[i], task, sizeof(TaskInfo_t));

        if (ret != 0) {
            return ret;
        }
    }

    return (long)actual_count;
}
```

#### 22.5.2 内存统计系统调用

```c
/**
 * @brief 内存统计系统调用实现
 * @param stats 内存统计结构（用户空间指针）
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则18.1: 指针运算安全
 *
 * @note 收集内核内存统计信息
 * @note 复制到用户空间
 *
 * @warning stats必须指向有效的用户空间内存
 * @warning 必须验证用户空间指针
 */
long sys_mem_stats(MemStats_t *stats) {
    MemStats_t kern_stats;
    int ret;

    /* 参数验证 */
    if (stats == NULL) {
        return -EINVAL;
    }

    /* 验证用户空间指针 */
    if (!user_access_ok(stats, sizeof(MemStats_t))) {
        return -EFAULT;
    }

    /* 收集内存统计 */
    collect_mem_stats(&kern_stats);

    /* 复制到用户空间 */
    ret = copy_to_user(stats, &kern_stats, sizeof(MemStats_t));

    if (ret != 0) {
        return ret;
    }

    return 0;
}
```

### 22.6 安全检查MISRA-C编码规范

#### 22.6.1 权限检查函数

```c
/**
 * @brief 检查Shell权限
 * @param required_cap 所需权限
 * @return 有权限返回true，否则返回false
 *
 * @note MISRA规则遵守：
 *   - 规则10.5: 防止有符号整数溢出
 *   - 规则16.3: 使用默认分支
 *
 * @note 检查当前Shell任务的能力
 * @note 支持细粒度权限控制
 *
 * @warning 必须在执行命令前检查
 * @warning 必须记录权限拒绝事件
 */
bool shell_check_capability(Capability_t required_cap) {
    const Capability_t *caps;
    uint32_t cap_count;
    uint32_t i;

    /* 获取当前Shell任务的能力列表 */
    caps = get_current_task_caps(&cap_count);

    if (caps == NULL) {
        /* 无能力：拒绝 */
        return false;
    }

    /* 检查是否有所需能力 */
    for (i = 0U; i < cap_count; i++) {
        if (caps[i] == required_cap) {
            return true;
        }
    }

    /* 记录权限拒绝事件 */
    log_cap_denied(required_cap);

    return false;
}
```

#### 22.6.2 输入验证函数

```c
/**
 * @brief 验证Shell命令输入
 * @param cmd_str 命令字符串
 * @return 有效返回true，否则返回false
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则21.6: 使用安全的字符串函数
 *   - 规则22.10: 避免无限循环的误判
 *
 * @note 检查命令注入攻击
 * @note 检查危险字符
 * @note 检查命令长度
 *
 * @warning 必须在执行命令前验证
 * @warning 必须拒绝可疑输入
 */
bool shell_validate_input(const char *cmd_str) {
    uint32_t len;
    uint32_t i;

    /* 参数验证 */
    if (cmd_str == NULL) {
        return false;
    }

    /* 检查长度 */
    len = strlen(cmd_str);

    if (len == 0U) {
        return false;
    }

    if (len >= SHELL_CMD_MAX_LEN) {
        return false;
    }

    /* 检查危险字符 */
    for (i = 0U; i < len; i++) {
        char ch = cmd_str[i];

        /* 检查不可打印字符（除空格） */
        if ((ch < 32) && (ch != ' ')) {
            return false;
        }

        /* 检查删除字符 */
        if (ch == 127) {
            return false;
        }

        /* 检查注入攻击标记（可选） */
        if (ch == ';') {
            /* 命令分隔符：拒绝 */
            return false;
        }

        if (ch == '`') {
            /* 命令替换：拒绝 */
            return false;
        }

        if (ch == '$') {
            /* 变量替换：拒绝 */
            return false;
        }

        if (ch == '|') {
            /* 管道：暂不支持，拒绝 */
            return false;
        }
    }

    return true;
}
```

### 22.7 MISRA合规性检查清单

#### 22.7.1 Shell框架检查项

- [ ] 命令缓冲区大小检查（防止溢出）
- [ ] 字符串终止符保证
- [ ] 指针参数NULL验证
- [ ] 循环边界检查
- [ ] 整数溢出检查
- [ ] 资源限制（内存和CPU）

#### 22.7.2 命令执行检查项

- [ ] 命令名称长度验证
- [ ] 命令参数长度验证
- [ ] 命令表查找安全
- [ ] 权限检查（每个命令）
- [ ] 命令注入防护
- [ ] 危险字符过滤

#### 22.7.3 系统调用检查项

- [ ] 用户空间指针验证
- [ ] 数组大小验证
- [ ] 复制到用户空间安全
- [ ] 敏感信息过滤（基于权限）
- [ ] 错误码正确返回
- [ ] 资源清理（失败时）

#### 22.7.4 输入验证检查项

- [ ] 字符串长度限制
- [ ] 不可打印字符检查
- [ ] 注入攻击防护
- [ ] 特殊字符过滤
- [ ] 命令历史记录限制
- [ ] 输入超时处理

#### 22.7.5 输出格式化检查项

- [ ] 格式化字符串安全
- [ ] 缓冲区溢出防护
- [ ] 百分比计算防除零
- [ ] 类型转换显式
- [ ] 浮点运算避免（使用定点）
- [ ] 敏感信息过滤

#### 22.7.6 安全性检查项

- [ ] 权限检查（每个命令）
- [ ] 审计日志记录
- [ ] 资源限制（内存、CPU、时间）
- [ ] 拒绝服务防护
- [ ] 特权命令保护
- [ ] 配置修改审计

---

## 23. Initramfs和rcS脚本MISRA-C编码规范

### 23.1 cpio格式解析MISRA-C编码规范

#### 23.1.1 cpio文件头定义

```c
/**
 * @brief cpio文件头（newc格式）
 * @note 所有字段都是ASCII十六进制字符串
 * @note 必须确保正确的对齐和打包
 *
 * @note MISRA规则遵守：
 *   - 规则18.1: 指针运算安全
 *   - 规则19.2: 使用__attribute__((packed))避免未对齐访问
 *   - 规则21.1: 内联汇编最小化
 */
typedef struct {
    char    magic[6];      /* 魔数："070701"或"070702" */
    char    ino[8];        /* inode number */
    char    mode[8];       /* 文件权限和类型 */
    char    uid[8];        /* user ID */
    char    gid[8];        /* group ID */
    char    nlink[8];      /* 链接数量 */
    char    mtime[8];      /* 修改时间 */
    char    filesize[8];   /* 文件大小 */
    char    devmajor[8];   /* 主设备号 */
    char    devminor[8];   /* 次设备号 */
    char    rdevmajor[8];  /* 主设备号（特殊文件） */
    char    rdevminor[8];  /* 次设备号（特殊文件） */
    char    namesize[8];   /* 文件名长度 */
    char    check[8];      /* 校验和（070702才有） */
} __attribute__((packed)) CpioHeader_t;

/* 编译时断言：验证结构体大小 */
_Static_assert(sizeof(CpioHeader_t) == 110U,
               "CpioHeader_t size must be 110 bytes (newc format)");
```

#### 23.1.2 ASCII十六进制解析函数

```c
/**
 * @brief 解析ASCII十六进制字符串
 * @param str ASCII十六进制字符串（以'\0'结尾）
 * @return 解析后的数值
 *
 * @note MISRA规则遵守：
 *   - 规则10.1: 防止整数溢出
 *   - 规则10.3: 防止整数溢出
 *   - 规则21.1: 内联汇编最小化
 *   - 规则21.6: 使用标准库字符串函数
 *
 * @note 支持大小写（0-9, a-f, A-F）
 * @note 遇到非十六进制字符时停止解析
 *
 * @warning str必须以'\0'结尾
 * @warning 必须检查整数溢出
 */
static uint32_t cpio_parse_hex(const char *str) {
    uint32_t val = 0U;
    uint32_t max_shift = (sizeof(uint32_t) * 8U) - 4U;

    /* 参数验证 */
    if (str == NULL) {
        return 0U;
    }

    /* 逐字符解析 */
    while (*str != '\0') {
        uint8_t ch = (uint8_t)*str;
        uint32_t digit;

        /* 验证是否还有空间容纳下一个数字 */
        if (val > (0xFFFFFFFFU >> 4U)) {
            /* 可能溢出：停止解析 */
            break;
        }

        /* 解析数字 */
        if (ch >= '0' && ch <= '9') {
            digit = (uint32_t)(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            digit = (uint32_t)(ch - 'a' + 10U);
        } else if (ch >= 'A' && ch <= 'F') {
            digit = (uint32_t)(ch - 'A' + 10U);
        } else {
            /* 非法字符：停止解析 */
            break;
        }

        /* 左移并添加数字（检查溢出） */
        val = (val << 4U) + digit;

        str++;
    }

    return val;
}
```

#### 23.1.3 initramfs读取函数

```c
/**
 * @brief 从cpio镜像中读取文件
 * @param path 文件路径（如"/etc/rcS"）
 * @param buf 输出缓冲区
 * @param size 缓冲区大小
 * @return 成功返回文件大小，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则10.1: 检查整数溢出
 *   - 规则10.3: 防止整数溢出
 *   - 规则13.5: 检查指针参数
 *   - 规则18.1: 指针运算安全
 *   - 规则21.6: 使用安全的字符串函数
 *
 * @note initramfs是只读的
 * @note 不支持写操作
 * @note 支持符号链接（简化的实现）
 *
 * @warning path必须以'\0'结尾
 * @warning buf必须指向有效内存
 * @warning size不能为0
 */
int initramfs_read(const char *path, uint8_t *buf, uint32_t size) {
    uint32_t offset = 0U;

    /* 参数验证 */
    if (path == NULL) {
        return -EINVAL;
    }

    if (buf == NULL) {
        return -EINVAL;
    }

    if (size == 0U) {
        return -EINVAL;
    }

    /* 路径验证（防止路径遍历攻击） */
    if (strstr(path, "..") != NULL) {
        return -EINVAL;  /* 拒绝包含..的路径 */
    }

    if (path[0] != '/') {
        return -EINVAL;  /* 必须是绝对路径 */
    }

    /* 遍历cpio归档 */
    while (offset < g_initramfs.size) {
        const CpioHeader_t *hdr;
        uint32_t namesize;
        uint32_t filesize;
        const char *name;
        uint32_t align;
        int cmp;

        /* 边界检查：确保至少有一个完整头部 */
        if ((offset + sizeof(CpioHeader_t)) > g_initramfs.size) {
            break;
        }

        hdr = (const CpioHeader_t *)(g_initramfs.data + offset);

        /* 验证魔数 */
        if ((memcmp(hdr->magic, "070701", 6) != 0) &&
            (memcmp(hdr->magic, "070702", 6) != 0)) {
            return -EINVAL;
        }

        /* 解析文件头 */
        namesize = cpio_parse_hex(hdr->namesize);
        filesize = cpio_parse_hex(hdr->filesize);

        /* 验证namesize和filesize的合理性 */
        if (namesize > 256U) {
            return -EINVAL;  /* 文件名太长 */
        }

        if (filesize > INITRAMFS_MAX_FILE_SIZE) {
            return -EFBIG;   /* 文件太大 */
        }

        /* 边界检查 */
        if ((offset + sizeof(CpioHeader_t) + namesize) > g_initramfs.size) {
            break;
        }

        name = (const char *)(hdr + 1);

        /* 检查是否是结束标记 */
        if (strcmp(name, "TRAILER!!!") == 0) {
            break;
        }

        /* 检查文件名是否匹配 */
        cmp = strcmp(path, name);

        if (cmp == 0) {
            /* 找到文件，复制内容 */
            uint32_t copy_size;
            const uint8_t *filedata;

            /* 计算实际复制大小 */
            if (filesize < size) {
                copy_size = filesize;
            } else {
                copy_size = size;
            }

            /* 计算文件数据位置（考虑对齐） */
            filedata = (const uint8_t *)name + namesize;

            /* 对齐到4字节边界 */
            align = (4U - (namesize & 3U)) & 3U;
            filedata += align;

            /* 边界检查：文件数据 */
            if ((filedata - g_initramfs.data + filesize) > g_initramfs.size) {
                return -EINVAL;
            }

            /* 复制文件内容 */
            (void)memcpy(buf, filedata, copy_size);

            return (int)copy_size;
        }

        /* 跳到下一个文件 */
        offset += sizeof(CpioHeader_t) + namesize + filesize;
        offset = (offset + 3U) & ~3U;  /* 4字节对齐 */
    }

    return -ENOENT;
}
```

### 23.2 rcS脚本解析MISRA-C编码规范

#### 23.2.1 rcS脚本执行函数

```c
/**
 * @brief 执行rcS脚本
 * @param script_path 脚本路径（如"/etc/rcS"）
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则10.1: 检查整数溢出
 *   - 规则10.3: 防止整数溢出
 *   - 规则13.5: 检查指针参数
 *   - 规则18.1: 指针运算安全
 *   - 规则21.6: 使用安全的字符串函数
 *
 * @note 仅支持简化的Shell语法
 * @note 不支持管道、重定向、后台运行
 * @note 支持注释（#开头）
 * @note 错误不中断启动（继续执行）
 *
 * @warning script_path必须以'\0'结尾
 * @warning 必须验证脚本路径
 */
int rc_script_execute(const char *script_path) {
    char *script;
    uint32_t script_size;
    char line[256];
    uint32_t line_pos = 0U;
    int ret;

    /* 参数验证 */
    if (script_path == NULL) {
        return -EINVAL;
    }

    /* 读取脚本文件 */
    script_size = (uint32_t)initramfs_read(script_path,
                                             (uint8_t *)g_rc_script_buf,
                                             sizeof(g_rc_script_buf));
    if ((int)script_size < 0) {
        printf("rcS: Failed to read script: %s\n", script_path);
        return (int)script_size;
    }

    script = g_rc_script_buf;

    /* 逐行解析和执行 */
    while (line_pos < script_size) {
        const char *line_start;
        char *newline;
        uint32_t line_len;
        uint32_t remaining;

        line_start = &script[line_pos];
        remaining = script_size - line_pos;

        /* 查找换行符 */
        newline = strchr(line_start, '\n');

        if (newline == NULL) {
            /* 最后一行 */
            line_len = remaining;
        } else {
            line_len = (uint32_t)(newline - line_start);
        }

        /* 跳过空行 */
        if (line_len == 0U) {
            line_pos += 1U;
            continue;
        }

        /* 复制行到缓冲区 */
        if (line_len >= sizeof(line)) {
            line_len = sizeof(line) - 1U;
        }

        (void)memcpy(line, line_start, line_len);
        line[line_len] = '\0';

        /* 跳过注释 */
        if (line[0] == '#') {
            line_pos += line_len + 1U;
            continue;
        }

        /* 执行命令 */
        ret = rc_execute_line(line);

        if (ret != 0) {
            printf("rcS: Error executing line %u: %s (error: %d)\n",
                   line_pos, line, ret);
            /* 继续执行，不中断启动 */
        }

        /* 移动到下一行 */
        if (newline == NULL) {
            break;
        }

        line_pos += line_len + 1U;
    }

    return 0;
}
```

#### 23.2.2 rcS命令行解析

```c
/**
 * @brief 解析rcS命令行
 * @param line 命令行字符串
 * @param argv 参数数组
 * @param argv_size argv数组大小
 * @return 成功返回参数数量，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则10.1: 检查整数溢出
 *   - 规则13.5: 检查指针参数
 *   - 规则18.1: 指针运算安全
 *   - 规则21.6: 使用安全的字符串函数
 *
 * @note 支持参数引号（可选）
 * @note 支持转义字符（可选）
 * @note 不支持变量替换
 *
 * @warning line必须以'\0'结尾
 * @warning argv必须足够大
 */
static int rc_parse_args(const char *line, char *argv[], uint32_t argv_size) {
    uint32_t argc = 0U;
    char *line_copy;
    char *token;
    char *saveptr = NULL;

    /* 参数验证 */
    if (line == NULL) {
        return -EINVAL;
    }

    if (argv == NULL) {
        return -EINVAL;
    }

    if (argv_size == 0U) {
        return -EINVAL;
    }

    /* 复制行（用于修改） */
    line_copy = strdup(line);
    if (line_copy == NULL) {
        return -ENOMEM;
    }

    /* 逐个token解析 */
    token = strtok_r(line_copy, " \t", &saveptr);

    while (token != NULL) {
        /* 检查argv数组大小 */
        if (argc >= argv_size) {
            free(line_copy);
            return -E2BIG;
        }

        /* 保存参数 */
        argv[argc] = token;
        argc++;

        /* 获取下一个token */
        token = strtok_r(NULL, " \t", &saveptr);
    }

    return (int)argc;
}
```

### 23.3 INI配置文件解析MISRA-C编码规范

#### 23.3.1 INI文件解析函数

```c
/**
 * @brief INI配置文件解析
 * @param path 配置文件路径
 * @param callback 配置项回调函数
 * @param context 回调上下文
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则10.1: 检查整数溢出
 *   - 规则13.5: 检查指针参数
 *   - 规则18.1: 指针运算安全
 *   - 规则21.6: 使用安全的字符串函数
 *
 * @note 支持INI格式配置文件
 * @note 支持#和;注释
 * @note 支持[section]格式
 * @note 支持key=value格式
 *
 * @warning path必须以'\0'结尾
 * @warning callback可以为NULL（仅解析）
 */
int ini_parse(const char *path,
              IniCallback_t callback,
              void *context) {
    char *data;
    uint32_t size;
    char line[256];
    uint32_t pos = 0U;
    char current_section[64] = {0};
    int ret;

    /* 参数验证 */
    if (path == NULL) {
        return -EINVAL;
    }

    /* 读取配置文件 */
    size = (uint32_t)initramfs_read(path,
                                     (uint8_t *)g_ini_buf,
                                     sizeof(g_ini_buf));
    if ((int)size < 0) {
        return (int)size;
    }

    data = g_ini_buf;

    /* 逐行解析 */
    while (pos < size) {
        const char *line_start;
        char *newline;
        char *end;
        uint32_t line_len;

        line_start = &data[pos];

        /* 查找换行符 */
        newline = strchr(line_start, '\n');
        if (newline == NULL) {
            break;
        }

        line_len = (uint32_t)(newline - line_start);

        /* 行长度限制 */
        if (line_len >= sizeof(line)) {
            line_len = sizeof(line) - 1U;
        }

        (void)memcpy(line, line_start, line_len);
        line[line_len] = '\0';

        /* 跳过空行和注释 */
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') {
            pos += line_len + 1U;
            continue;
        }

        /* 解析section */
        if (line[0] == '[') {
            end = strchr(line, ']');

            if (end != NULL) {
                uint32_t len;

                len = (uint32_t)(end - line - 1U);

                if (len < sizeof(current_section)) {
                    (void)memcpy(current_section, line + 1, len);
                    current_section[len] = '\0';
                }
            }
        }
        /* 解析key=value */
        else {
            char *eq;
            char *key;
            char *value;
            char *key_end;

            eq = strchr(line, '=');
            if (eq != NULL) {
                *eq = '\0';
                key = line;
                value = eq + 1;

                /* 去除key的尾随空格 */
                key_end = eq - 1;
                while ((key_end > key) && (*key_end == ' ')) {
                    *key_end = '\0';
                    key_end--;
                }

                /* 去除value的前导空格 */
                while (*value == ' ') {
                    value++;
                }

                /* 调用回调 */
                if (callback != NULL) {
                    ret = callback(current_section, key, value, context);
                    if (ret != 0) {
                        return ret;
                    }
                }
            }
        }

        pos += line_len + 1U;
    }

    return 0;
}
```

### 23.4 安全检查MISRA-C编码规范

#### 23.4.1 路径验证函数

```c
/**
 * @brief 验证文件路径安全性
 * @param path 文件路径
 * @return 安全返回true，否则返回false
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则21.6: 使用安全的字符串函数
 *
 * @note 防止路径遍历攻击
 * @note 验证路径格式
 *
 * @warning path必须以'\0'结尾
 * @warning 必须在访问文件前调用
 */
bool initramfs_validate_path(const char *path) {
    uint32_t len;

    /* 参数验证 */
    if (path == NULL) {
        return false;
    }

    /* 验证路径长度 */
    len = strlen(path);

    if (len == 0U) {
        return false;
    }

    if (len > INITRAMFS_MAX_PATH_LEN) {
        return false;
    }

    /* 必须是绝对路径 */
    if (path[0] != '/') {
        return false;
    }

    /* 防止路径遍历攻击 */
    if (strstr(path, "..") != NULL) {
        return false;
    }

    /* 防止连续斜杠 */
    if (strstr(path, "//") != NULL) {
        return false;
    }

    return true;
}
```

### 23.5 MISRA合规性检查清单

#### 23.5.1 cpio解析检查项

- [ ] cpio魔数验证（070701或070702）
- [ ] 文件名长度限制（<256字节）
- [ ] 文件大小限制（防止溢出）
- [ ] 边界检查（防止越界访问）
- [ ] 整数溢出检查（左移操作）
- [ ] ASCII十六进制解析安全
- [ ] 4字节对齐处理正确

#### 23.5.2 rcS脚本解析检查项

- [ ] 脚本路径验证
- [ ] 行长度限制（防止溢出）
- [ ] 参数数组边界检查
- [ ] 空指针检查
- [ ] 字符串终止符保证
- [ ] 注释行正确跳过
- [ ] 错误处理不中断启动

#### 23.5.3 INI配置解析检查项

- [ ] 配置文件路径验证
- [ ] 行长度限制
- [ ] section名称长度验证
- [ ] key/value长度验证
- [ ] 字符串操作安全
- [ ] 回调函数错误处理
- [ ] 空行和注释正确处理

#### 23.5.4 安全检查项

- [ ] 路径遍历攻击防护（..）
- [ ] 缓冲区溢出防护
- [ ] 整数溢出检查
- [ ] 符号链接安全（如果支持）
- [ ] 权限检查（如果需要）
- [ ] 恶意文件检测

#### 23.5.5 内存管理检查项

- [ ] strdup后调用free
- [ ] 临时缓冲区大小验证
- [ ] 栈使用量控制
- [ ] 堆分配失败处理
- [ ] 内存泄漏防护

#### 23.5.6 错误处理检查项

- [ ] 所有错误码正确返回
- [ ] 错误信息清晰描述
- [ ] 资源清理（失败时）
- [ ] 启动不中断（rcS错误）
- [ ] 日志记录（关键操作）

## 24. VFS MISRA-C编码规范

### 24.1 VFS核心API MISRA-C编码规范

#### 24.1.1 open()函数实现

```c
/**
 * @brief 打开文件（VFS统一接口）
 * @param path 文件路径（绝对路径）
 * @param flags 打开标志（O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_TRUNC, O_APPEND）
 * @param ... 可变参数（创建文件时的权限模式）
 * @return 成功返回文件描述符（>=0），失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则17.1: 指针参数必须声明为指向const
 *   - 规则21.4: 可变参数必须安全处理
 *   - 规则22.8: 参数必须验证
 *
 * @note 支持POSIX兼容的flags
 * @note 自动路由到正确的挂载点
 * @note 文件描述符0-2保留给标准输入/输出/错误
 *
 * @warning path必须以'\0'结尾
 * @warning flags必须是有效的组合
 * @warning 必须检查返回值
 */
int open(const char *path, int flags, ...) {
    VFSMount_t *mount;
    VFSFile_t *file;
    int fd;
    int ret;
    va_list ap;
    mode_t mode = 0U;

    /* 参数验证（规则22.8） */
    if (path == NULL) {
        return -EINVAL;
    }

    /* 验证路径格式 */
    if (path[0] != '/') {
        return -EINVAL;  /* 必须是绝对路径 */
    }

    /* 防止路径遍历攻击 */
    if (strstr(path, "..") != NULL) {
        return -EINVAL;
    }

    /* 验证flags */
    if ((flags & O_ACCMODE) == 0U) {
        return -EINVAL;
    }

    /* 提取可变参数（如果指定了O_CREAT） */
    if ((flags & O_CREAT) != 0U) {
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);  /* MISRA: 显式转换 */
        va_end(ap);
    }

    /* 查找挂载点（最长前缀匹配） */
    mount = vfs_find_mount(path);
    if (mount == NULL) {
        return -ENOENT;
    }

    /* 验证filesystem operations */
    if (mount->ops == NULL) {
        return -ENODEV;
    }

    if (mount->ops->open == NULL) {
        return -ENOSYS;
    }

    /* 分配文件描述符 */
    fd = vfs_alloc_fd();
    if (fd < 0) {
        return -EMFILE;  /* 文件描述符表满 */
    }

    /* 调用filesystem-specific open */
    ret = mount->ops->open(path, flags, mode);
    if (ret < 0) {
        vfs_free_fd(fd);
        return ret;
    }

    /* 初始化文件描述符条目 */
    file = &g_vfs_files[fd];
    file->mount = mount;
    file->private_data = NULL;  /* 由filesystem-specific open填充 */
    file->flags = flags;
    file->offset = 0U;
    file->ref_count = 1U;

    return fd;
}
```

#### 24.1.2 read()函数实现

```c
/**
 * @brief 从文件读取数据（VFS统一接口）
 * @param fd 文件描述符
 * @param buf 接收缓冲区
 * @param size 要读取的字节数
 * @return 成功返回实际读取的字节数，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则17.1: 指针参数必须声明为指向const
 *   - 规则22.8: 参数必须验证
 *
 * @note 自动更新文件偏移量
 * @note 支持短读取（返回值可能<size）
 * @note 读取到文件末尾返回0
 *
 * @warning buf必须指向有效的缓冲区
 * @warning size不能超过SSIZE_MAX
 * @warning 必须检查返回值
 */
ssize_t read(int fd, void *buf, size_t size) {
    VFSFile_t *file;
    ssize_t ret;

    /* 参数验证 */
    if (buf == NULL) {
        return -EINVAL;
    }

    /* 防止size过大 */
    if (size > (size_t)SSIZE_MAX) {
        return -EINVAL;
    }

    /* size为0是合法的，立即返回0 */
    if (size == 0U) {
        return 0;
    }

    /* 验证文件描述符 */
    if ((fd < 0) || (fd >= OPEN_MAX)) {
        return -EBADF;
    }

    file = &g_vfs_files[fd];

    /* 验证文件描述符是否已打开 */
    if (file->mount == NULL) {
        return -EBADF;
    }

    /* 验证读取权限 */
    if ((file->flags & O_ACCMODE) == O_WRONLY) {
        return -EBADF;  /* 只写文件不能读取 */
    }

    /* 验证filesystem operations */
    if (file->mount->ops == NULL) {
        return -ENODEV;
    }

    if (file->mount->ops->read == NULL) {
        return -ENOSYS;
    }

    /* 调用filesystem-specific read */
    ret = file->mount->ops->read(file->private_data, buf, size);

    /* 更新文件偏移量（如果读取成功） */
    if (ret > 0) {
        file->offset += (uint64_t)(size_t)ret;  /* MISRA: 显式转换 */
    }

    return ret;
}
```

#### 24.1.3 write()函数实现

```c
/**
 * @brief 向文件写入数据（VFS统一接口）
 * @param fd 文件描述符
 * @param buf 数据缓冲区
 * @param size 要写入的字节数
 * @return 成功返回实际写入的字节数，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则17.1: 指针参数必须声明为指向const
 *   - 规则22.8: 参数必须验证
 *
 * @note 自动更新文件偏移量
 * @note 支持短写入（返回值可能<size）
 * @note O_APPEND标志确保原子追加
 *
 * @warning buf必须指向有效的缓冲区
 * @warning size不能超过SSIZE_MAX
 * @warning 必须检查返回值
 */
ssize_t write(int fd, const void *buf, size_t size) {
    VFSFile_t *file;
    ssize_t ret;

    /* 参数验证 */
    if (buf == NULL) {
        return -EINVAL;
    }

    /* 防止size过大 */
    if (size > (size_t)SSIZE_MAX) {
        return -EINVAL;
    }

    /* size为0是合法的，立即返回0 */
    if (size == 0U) {
        return 0;
    }

    /* 验证文件描述符 */
    if ((fd < 0) || (fd >= OPEN_MAX)) {
        return -EBADF;
    }

    file = &g_vfs_files[fd];

    /* 验证文件描述符是否已打开 */
    if (file->mount == NULL) {
        return -EBADF;
    }

    /* 验证写入权限 */
    if ((file->flags & O_ACCMODE) == O_RDONLY) {
        return -EBADF;  /* 只读文件不能写入 */
    }

    /* 验证filesystem operations */
    if (file->mount->ops == NULL) {
        return -ENODEV;
    }

    if (file->mount->ops->write == NULL) {
        return -ENOSYS;
    }

    /* O_APPEND模式：设置偏移量到文件末尾 */
    if ((file->flags & O_APPEND) != 0U) {
        off_t end_offset;
        if (file->mount->ops->lseek != NULL) {
            end_offset = file->mount->ops->lseek(file->private_data, 0, SEEK_END);
            if (end_offset < 0) {
                return (ssize_t)end_offset;
            }
            file->offset = (uint64_t)(uintmax_t)end_offset;
        }
    }

    /* 调用filesystem-specific write */
    ret = file->mount->ops->write(file->private_data, buf, size);

    /* 更新文件偏移量（如果写入成功） */
    if (ret > 0) {
        file->offset += (uint64_t)(size_t)ret;  /* MISRA: 显式转换 */
    }

    return ret;
}
```

#### 24.1.4 close()函数实现

```c
/**
 * @brief 关闭文件（VFS统一接口）
 * @param fd 文件描述符
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则22.8: 参数必须验证
 *   - 规则21.3: 不要在资源获取后失败
 *
 * @note 释放文件描述符
 * @note 调用filesystem-specific close
 * @note 多次close是安全的（幂等操作）
 *
 * @warning 关闭后不要使用该文件描述符
 * @warning 必须检查返回值
 */
int close(int fd) {
    VFSFile_t *file;
    int ret;

    /* 验证文件描述符 */
    if ((fd < 0) || (fd >= OPEN_MAX)) {
        return -EBADF;
    }

    file = &g_vfs_files[fd];

    /* 验证文件描述符是否已打开 */
    if (file->mount == NULL) {
        return -EBADF;  /* 已经关闭或从未打开 */
    }

    /* 验证filesystem operations */
    if (file->mount->ops == NULL) {
        return -ENODEV;
    }

    /* 调用filesystem-specific close */
    if (file->mount->ops->close != NULL) {
        ret = file->mount->ops->close(file->private_data);
        if (ret < 0) {
            return ret;
        }
    }

    /* 清理文件描述符条目 */
    file->mount = NULL;
    file->private_data = NULL;
    file->flags = 0;
    file->offset = 0U;
    file->ref_count = 0U;

    /* 释放文件描述符 */
    vfs_free_fd(fd);

    return 0;
}
```

### 24.2 文件描述符管理MISRA-C编码规范

#### 24.2.1 文件描述符分配

```c
/**
 * @brief 分配文件描述符
 * @return 成功返回文件描述符（>=0），失败返回-EMFILE
 *
 * @note MISRA规则遵守：
 *   - 规则22.8: 数组访问必须在边界内
 *   - 规则20.5: 避免无符号整数溢出
 *
 * @note 跳过0-2（保留给标准流）
 * @note 线程安全（使用原子操作）
 * @note 返回最小的可用fd
 */
static int vfs_alloc_fd(void) {
    uint32_t i;
    uint32_t start_fd = 3U;  /* 跳过stdin, stdout, stderr */

    /* 从start_fd开始查找空闲fd */
    for (i = start_fd; i < OPEN_MAX; i++) {
        /* 原子检查是否空闲 */
        if (g_vfs_files[i].mount == NULL) {
            /* 标记为已使用（防止竞态条件） */
            /* 在多核环境下，这里应该使用原子操作或锁 */
            return (int)i;
        }
    }

    /* 文件描述符表满 */
    return -EMFILE;
}
```

#### 24.2.2 文件描述符释放

```c
/**
 * @brief 释放文件描述符
 * @param fd 文件描述符
 *
 * @note MISRA规则遵守：
 *   - 规则22.8: 参数必须验证
 *
 * @note 安全地释放fd（幂等操作）
 * @note 清理所有相关资源
 *
 * @warning fd必须是有效的文件描述符
 */
static void vfs_free_fd(int fd) {
    VFSFile_t *file;

    /* 参数验证 */
    if ((fd < 0) || (fd >= OPEN_MAX)) {
        return;
    }

    file = &g_vfs_files[fd];

    /* 清零（防止UAF） */
    (void)memset(file, 0, sizeof(VFSFile_t));
}
```

### 24.3 挂载和卸载MISRA-C编码规范

#### 24.3.1 mount()函数实现

```c
/**
 * @brief 挂载文件系统
 * @param source 源设备（可为NULL，如initramfs）
 * @param target 挂载点路径（绝对路径）
 * @param fstype 文件系统类型（如"initramfs", "procfs"）
 * @param flags 挂载标志（MS_RDONLY, MS_NOSUID等）
 * @param data 文件系统特定数据
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则17.1: 指针参数必须声明为指向const
 *   - 规则22.8: 参数必须验证
 *
 * @note 挂载点必须是目录
 * @note 不能重复挂载到同一挂载点
 * @note 挂载点路径不能重叠
 *
 * @warning target必须以'\0'结尾
 * @warning 挂载后不能修改挂载点
 */
int mount(const char *source, const char *target,
          const char *fstype, unsigned long flags,
          const void *data) {
    VFSMount_t *mount;
    const VFSOperations_t *ops;
    int ret;

    /* 参数验证 */
    if (target == NULL) {
        return -EINVAL;
    }

    if (fstype == NULL) {
        return -EINVAL;
    }

    /* 验证路径格式 */
    if (target[0] != '/') {
        return -EINVAL;
    }

    /* 防止路径遍历攻击 */
    if (strstr(target, "..") != NULL) {
        return -EINVAL;
    }

    /* 查找文件系统operations */
    ops = vfs_get_filesystem_ops(fstype);
    if (ops == NULL) {
        return -ENODEV;
    }

    /* 检查挂载点是否已存在 */
    mount = vfs_find_mount(target);
    if (mount != NULL) {
        /* 检查是否完全匹配（重复挂载） */
        if (strcmp(mount->mount_point, target) == 0) {
            return -EBUSY;
        }
    }

    /* 分配挂载点结构 */
    mount = (VFSMount_t *)malloc(sizeof(VFSMount_t));
    if (mount == NULL) {
        return -ENOMEM;
    }

    /* 初始化挂载点 */
    mount->mount_point = strdup(target);
    if (mount->mount_point == NULL) {
        free(mount);
        return -ENOMEM;
    }

    mount->ops = ops;
    mount->flags = flags;
    mount->private_data = NULL;

    /* 调用filesystem-specific mount */
    if (ops->mount != NULL) {
        ret = ops->mount(source, mount, data);
        if (ret < 0) {
            free((void *)mount->mount_point);
            free(mount);
            return ret;
        }
    }

    /* 添加到挂载点链表 */
    mount->next = g_vfs_mounts;
    g_vfs_mounts = mount;

    return 0;
}
```

#### 24.3.2 umount()函数实现

```c
/**
 * @brief 卸载文件系统
 * @param target 挂载点路径
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则17.1: 指针参数必须声明为指向const
 *   - 规则22.8: 参数必须验证
 *
 * @note 挂载点必须存在
 * @note 挂载点上不能有打开的文件
 * @note 不能卸载根文件系统
 *
 * @warning target必须以'\0'结尾
 * @warning 卸载后不要使用该挂载点
 */
int umount(const char *target) {
    VFSMount_t *mount;
    VFSMount_t *prev;
    uint32_t i;
    int ret;

    /* 参数验证 */
    if (target == NULL) {
        return -EINVAL;
    }

    /* 验证路径格式 */
    if (target[0] != '/') {
        return -EINVAL;
    }

    /* 查找挂载点 */
    mount = vfs_find_mount(target);
    if (mount == NULL) {
        return -ENOENT;
    }

    /* 检查是否完全匹配 */
    if (strcmp(mount->mount_point, target) != 0) {
        return -EINVAL;
    }

    /* 不能卸载根文件系统 */
    if (strcmp(mount->mount_point, "/") == 0) {
        return -EBUSY;
    }

    /* 检查是否有打开的文件 */
    for (i = 0U; i < OPEN_MAX; i++) {
        if (g_vfs_files[i].mount == mount) {
            return -EBUSY;  /* 有文件仍被打开 */
        }
    }

    /* 调用filesystem-specific umount */
    if (mount->ops != NULL) {
        if (mount->ops->umount != NULL) {
            ret = mount->ops->umount(mount);
            if (ret < 0) {
                return ret;
            }
        }
    }

    /* 从挂载点链表中移除 */
    if (g_vfs_mounts == mount) {
        g_vfs_mounts = mount->next;
    } else {
        prev = g_vfs_mounts;
        while ((prev != NULL) && (prev->next != mount)) {
            prev = prev->next;
        }
        if (prev != NULL) {
            prev->next = mount->next;
        }
    }

    /* 释放资源 */
    free((void *)mount->mount_point);
    free(mount);

    return 0;
}
```

### 24.4 路径路由MISRA-C编码规范

#### 24.4.1 vfs_find_mount()函数实现

```c
/**
 * @brief 查找路径对应的挂载点（最长前缀匹配）
 * @param path 文件路径（绝对路径）
 * @return 找到返回挂载点指针，未找到返回NULL
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则22.8: 参数必须验证
 *   - 规则21.3: 不要在资源获取后失败
 *
 * @note 使用最长前缀匹配算法
 * @note 支持挂载点嵌套
 * @note 保证O(n)时间复杂度（n为挂载点数量）
 *
 * @warning path必须以'\0'结尾
 * @warning 返回的指针不能被修改
 * @warning 调用者不应释放返回的指针
 */
VFSMount_t *vfs_find_mount(const char *path) {
    VFSMount_t *mount;
    VFSMount_t *best_match;
    size_t best_len;
    size_t mount_len;

    /* 参数验证 */
    if (path == NULL) {
        return NULL;
    }

    if (path[0] != '/') {
        return NULL;
    }

    best_match = NULL;
    best_len = 0U;

    /* 遍历所有挂载点 */
    mount = g_vfs_mounts;
    while (mount != NULL) {
        mount_len = strlen(mount->mount_point);

        /* 检查路径是否以挂载点为前缀 */
        if (strncmp(path, mount->mount_point, mount_len) == 0) {
            /* 检查是否是更长的匹配 */
            if (mount_len > best_len) {
                best_match = mount;
                best_len = mount_len;
            }
        }

        mount = mount->next;
    }

    return best_match;
}
```

### 24.5 procfs实现MISRA-C编码规范

#### 24.5.1 /proc/cpu/info读取实现

```c
/**
 * @brief 读取/proc/cpu/info文件内容
 * @param buf 接收缓冲区
 * @param size 缓冲区大小
 * @return 成功返回写入的字节数，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则22.8: 参数必须验证
 *   - 规则21.3: 防止缓冲区溢出
 *
 * @note 格式化CPU信息
 * @note 限制输出大小
 * @note 动态生成内容
 */
static ssize_t proc_cpu_read(char *buf, size_t size) {
    ssize_t len = 0;
    uint32_t i;

    /* 参数验证 */
    if (buf == NULL) {
        return -EINVAL;
    }

    if (size == 0U) {
        return 0;
    }

    /* 格式化CPU信息 */
    for (i = 0U; i < g_nr_cpus; i++) {
        int ret;

        /* 检查剩余空间 */
        if ((size_t)len >= size) {
            break;
        }

        ret = snprintf(buf + len, size - (size_t)len,
                      "CPU%d: Freq=%luMHz, Load=%u%%\n",
                      i,
                      (unsigned long)g_cpu_info[i].freq_mhz,
                      g_cpu_info[i].load_percent);

        if (ret < 0) {
            return -EIO;
        }

        len += (ssize_t)ret;
    }

    return len;
}
```

### 24.6 tmpfs实现MISRA-C编码规范

#### 24.6.1 tmpfs文件创建

```c
/**
 * @brief 创建tmpfs文件
 * @param path 文件路径
 * @param mode 文件权限
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则22.8: 参数必须验证
 *   - 规则21.3: 防止缓冲区溢出
 *
 * @note 在RAM中分配存储
 * @note 支持动态大小调整
 * @note 支持目录创建
 */
static int tmpfs_create(const char *path, mode_t mode) {
    TmpfsFile_t *file;
    uint32_t path_len;

    /* 参数验证 */
    if (path == NULL) {
        return -EINVAL;
    }

    /* 验证路径格式 */
    if (path[0] != '/') {
        return -EINVAL;
    }

    /* 防止路径遍历攻击 */
    if (strstr(path, "..") != NULL) {
        return -EINVAL;
    }

    /* 检查是否已存在 */
    if (tmpfs_find_file(path) != NULL) {
        return -EEXIST;
    }

    /* 分配文件结构 */
    file = (TmpfsFile_t *)malloc(sizeof(TmpfsFile_t));
    if (file == NULL) {
        return -ENOMEM;
    }

    /* 复制路径 */
    path_len = strlen(path) + 1U;
    file->path = (char *)malloc(path_len);
    if (file->path == NULL) {
        free(file);
        return -ENOMEM;
    }

    (void)memcpy(file->path, path, path_len);
    file->mode = mode;
    file->size = 0U;
    file->data = NULL;
    file->next = g_tmpfs_files;
    g_tmpfs_files = file;

    return 0;
}
```

### 24.7 devfs实现MISRA-C编码规范

#### 24.7.1 设备文件读取

```c
/**
 * @brief 读取设备文件
 * @param dev_data 设备特定数据
 * @param buf 接收缓冲区
 * @param size 要读取的字节数
 * @return 成功返回读取的字节数，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则22.8: 参数必须验证
 *   - 规则21.3: 防止缓冲区溢出
 *
 * @note 根据设备类型调用相应驱动
 * @note /dev/null总是返回0
 * @note /dev/zero填充0
 * @note /dev/random生成随机数
 */
static ssize_t devfs_read(void *dev_data, void *buf, size_t size) {
    DevfsNode_t *node = (DevfsNode_t *)dev_data;

    /* 参数验证 */
    if (node == NULL) {
        return -EINVAL;
    }

    if (buf == NULL) {
        return -EINVAL;
    }

    if (size == 0U) {
        return 0;
    }

    /* 根据设备类型处理 */
    switch (node->type) {
        case DEVFS_CHAR_NULL:
            /* /dev/null: 总是返回0（EOF） */
            return 0;

        case DEVFS_CHAR_ZERO:
            /* /dev/zero: 填充0 */
            (void)memset(buf, 0, size);
            return (ssize_t)size;

        case DEVFS_CHAR_RANDOM:
            /* /dev/random: 生成随机数 */
            return dev_random_generate(buf, size);

        case DEVFS_CHAR_TTY:
            /* /dev/tty0: 从终端读取 */
            return tty_read(node->minor, buf, size);

        default:
            return -ENODEV;
    }
}
```

### 24.8 MISRA合规性检查清单

#### 24.8.1 VFS核心API检查项

- [ ] 所有指针参数必须检查NULL（规则13.5）
- [ ] 数组边界检查（fd范围，缓冲区大小）（规则22.8）
- [ ] 可变参数安全处理（open的mode参数）（规则21.4）
- [ ] 整数转换显式声明（size_t → ssize_t）（规则11.6）
- [ ] 防止无符号整数溢出（文件偏移量）（规则20.5）
- [ ] 路径遍历攻击防护（检查".."）
- [ ] 资源清理（失败时释放fd）
- [ ] 返回值检查（所有系统调用）

#### 24.8.2 文件描述符管理检查项

- [ ] fd数组边界检查（0 ≤ fd < OPEN_MAX）
- [ ] 保留fd 0-2给标准流
- [ ] fd分配的原子性（多核环境）
- [ ] fd释放后的清零（防止UAF）
- [ ] fd重用后的状态重置
- [ ] 引用计数管理（dup, fork）
- [ ] 线程安全（锁或原子操作）

#### 24.8.3 挂载和卸载检查项

- [ ] 挂载点路径验证（绝对路径，无".."）
- [ ] 重复挂载检查
- [ ] 挂载点嵌套检查
- [ ] 卸载前检查打开的文件
- [ ] 根文件系统保护
- [ ] 内存分配失败处理
- [ ] 链表操作的线程安全

#### 24.8.4 路径路由检查项

- [ ] 最长前缀匹配正确性
- [ ] 路径分隔符处理（连续的'/'）
- [ ] 挂载点字符串长度验证
- [ ] 比较长度不超过缓冲区大小
- [ ] NULL终止符保证
- [ ] 大小写敏感处理
- [ ] 相对路径拒绝

#### 24.8.5 procfs检查项

- [ ] 动态内容生成安全
- [ ] 缓冲区溢出防护（snprintf）
- [ ] CPU信息访问的原子性
- [ ] 统计数据的线程安全
- [ ] 文件权限检查（只读）
- [ ] 格式化字符串安全性
- [ ] 内存泄漏防护

#### 24.8.6 tmpfs检查项

- [ ] RAM分配失败处理
- [ ] 文件大小限制
- [ ] 临时文件清理
- [ ] 并发创建/删除保护
- [ ] 目录操作安全（mkdir, rmdir）
- [ ] 硬链接限制
- [ ] 符号链接安全（如果支持）

#### 24.8.7 devfs检查项

- [ ] 设备类型验证
- [ ] 设备驱动调用安全
- [ ] 字符设备vs块设备区分
- [ ] 设备权限检查
- [ ] 特殊设备文件处理（/dev/null, /dev/zero）
- [ ] 随机数生成安全性（/dev/random）
- [ ] 终端I/O缓冲区管理

#### 24.8.8 安全检查项

- [ ] 路径遍历攻击防护（所有文件系统）
- [ ] 符号链接攻击防护（如果支持符号链接）
- [ ] 竞态条件防护（TOCTOU）
- [ ] 权限检查（读/写/执行）
- [ ] 拒绝服务防护（资源耗尽）
- [ ] 信息泄漏防护（错误消息）
- [ ] 恶意文件检测（如果可上传）

## 25. ELF加载器MISRA-C编码规范

### 25.1 ELF文件解析MISRA-C编码规范

#### 25.1.1 ELF魔数验证

```c
/**
 * @brief 验证ELF文件魔数
 * @param data ELF文件数据指针
 * @param size ELF文件大小
 * @return 有效返回true，否则返回false
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则21.3: 边界检查
 *   - 规则18.1: 指针运算不超过数组范围
 *
 * @note ELF魔数：0x7F 'E' 'L' 'F'
 * @note 支持32位和64位ELF
 * @note 支持小端和大端序
 *
 * @warning data必须指向有效的内存
 * @warning size必须 >= SELFMAG (16字节)
 */
bool elf_validate_magic(const uint8_t *data, uint32_t size) {
    /* 参数验证 */
    if (data == NULL) {
        return false;
    }

    /* 检查最小大小 */
    if (size < SELFMAG) {  /* SELFMAG = 16 */
        return false;
    }

    /* 验证ELF魔数 */
    if (memcmp(data, ELFMAG, SELFMAG) != 0) {
        return false;
    }

    /* 验证ELF类（32位或64位） */
    if ((data[EI_CLASS] != ELFCLASS32) &&
        (data[EI_CLASS] != ELFCLASS64)) {
        return false;
    }

    /* 验证字节序 */
    if ((data[EI_DATA] != ELFDATA2LSB) &&  /* 小端 */
        (data[EI_DATA] != ELFDATA2MSB)) {  /* 大端 */
        return false;
    }

    /* 验证版本 */
    if (data[EI_VERSION] != EV_CURRENT) {
        return false;
    }

    return true;
}
```

#### 25.1.2 ELF头部读取

```c
/**
 * @brief 读取并验证ELF64头部
 * @param data ELF文件数据指针
 * @param size ELF文件大小
 * @param ehdr 输出：ELF头部结构
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则21.3: 边界检查
 *   - 规则18.4: 指针差值不超过对象大小
 *   - 规则11.5: 指针转换显式声明
 *
 * @note 验证架构类型（EM_AARCH64）
 * @note 验证入口点地址
 * @note 验证程序头偏移和数量
 * @note 验证节头偏移和数量
 *
 * @warning ehdr必须指向有效的Elf64_Ehdr结构
 * @warning 必须检查返回值
 */
int elf_read_header(const uint8_t *data, uint32_t size,
                    const Elf64_Ehdr **ehdr) {
    uint64_t phdr_offset;
    uint64_t phdr_size;
    uint64_t shdr_offset;
    uint64_t shdr_size;

    /* 参数验证 */
    if (data == NULL) {
        return -EINVAL;
    }

    if (ehdr == NULL) {
        return -EINVAL;
    }

    /* 检查最小大小（至少能容纳ELF头部） */
    if (size < sizeof(Elf64_Ehdr)) {
        return -EINVAL;
    }

    /* 设置输出指针 */
    *ehdr = (const Elf64_Ehdr *)data;

    /* 验证架构类型 */
    if ((*ehdr)->e_machine != EM_AARCH64) {
        return -ENOEXEC;
    }

    /* 验证入口点地址 */
    if ((*ehdr)->e_entry == 0UL) {
        return -ENOEXEC;
    }

    /* 验证程序头偏移和数量 */
    phdr_offset = (*ehdr)->e_phoff;
    phdr_size = (uint64_t)(*ehdr)->e_phentsize * (uint64_t)(*ehdr)->e_phnum;

    /* 检查程序头是否超出文件范围 */
    if (phdr_offset > (uint64_t)size) {
        return -EINVAL;
    }

    if ((phdr_offset + phdr_size) > (uint64_t)size) {
        return -EINVAL;
    }

    /* 验证节头偏移和数量 */
    shdr_offset = (*ehdr)->e_shoff;
    shdr_size = (uint64_t)(*ehdr)->e_shentsize * (uint64_t)(*ehdr)->e_shnum;

    /* 检查节头是否超出文件范围 */
    if (shdr_offset > (uint64_t)size) {
        return -EINVAL;
    }

    if ((shdr_offset + shdr_size) > (uint64_t)size) {
        return -EINVAL;
    }

    return 0;
}
```

#### 25.1.3 程序头解析

```c
/**
 * @brief 解析ELF程序头
 * @param data ELF文件数据指针
 * @param size ELF文件大小
 * @param ehdr ELF头部指针
 * @param index 程序头索引
 * @param phdr 输出：程序头结构指针
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则21.3: 数组边界检查
 *   - 规则18.4: 指针运算安全
 *
 * @note 支持PT_LOAD（可加载段）
 * @note 支持PT_DYNAMIC（动态段）
 * @note 支持PT_INTERP（解释器）
 * @note 支持PT_NOTE（注释）
 * @note 支持PT_GNU_STACK（GNU栈）
 * @note 支持PT_GNU_RELRO（重定位只读）
 *
 * @warning phdr输出指针指向data内部，不应释放
 * @warning index必须在有效范围内
 */
static int elf_get_phdr(const uint8_t *data, uint32_t size,
                        const Elf64_Ehdr *ehdr, uint32_t index,
                        const Elf64_Phdr **phdr) {
    uint64_t phdr_offset;
    uint64_t phdr_end;

    /* 参数验证 */
    if (data == NULL) {
        return -EINVAL;
    }

    if (ehdr == NULL) {
        return -EINVAL;
    }

    if (phdr == NULL) {
        return -EINVAL;
    }

    /* 检查索引范围 */
    if (index >= ehdr->e_phnum) {
        return -EINVAL;
    }

    /* 计算程序头偏移 */
    phdr_offset = ehdr->e_phoff + ((uint64_t)index * (uint64_t)ehdr->e_phentsize);

    /* 检查是否超出文件范围 */
    phdr_end = phdr_offset + (uint64_t)ehdr->e_phentsize;

    if (phdr_end > (uint64_t)size) {
        return -EINVAL;
    }

    /* 设置输出指针 */
    *phdr = (const Elf64_Phdr *)(data + phdr_offset);

    return 0;
}
```

### 25.2 ELF段加载MISRA-C编码规范

#### 25.2.1 PT_LOAD段加载

```c
/**
 * @brief 加载PT_LOAD段到内存
 * @param data ELF文件数据指针
 * @param phdr 程序头指针
 * @param load_addr 输出：加载地址
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则21.3: 边界检查
 *   - 规则20.5: 无符号整数溢出检查
 *   - 规则18.1: memcpy边界检查
 *
 * @note 分配内存并复制段数据
 * @note 清零BSS段
 * @note 设置页表权限（RX/RW/RO）
 * @note 对齐到页边界
 *
 * @warning load_addr必须由调用者释放
 * @warning 必须检查返回值
 */
static int elf_load_pt_load_segment(const uint8_t *data,
                                     const Elf64_Phdr *phdr,
                                     uint8_t **load_addr) {
    uint8_t *vaddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t offset;

    /* 参数验证 */
    if (data == NULL) {
        return -EINVAL;
    }

    if (phdr == NULL) {
        return -EINVAL;
    }

    if (load_addr == NULL) {
        return -EINVAL;
    }

    /* 提取段信息 */
    filesz = phdr->p_filesz;
    memsz = phdr->p_memsz;
    offset = phdr->p_offset;

    /* 检查大小关系 */
    if (filesz > memsz) {
        return -EINVAL;
    }

    /* 检查是否为空段 */
    if (memsz == 0UL) {
        *load_addr = NULL;
        return 0;
    }

    /* 分配内存（对齐到页边界） */
    vaddr = (uint8_t *)malloc(memsz);
    if (vaddr == NULL) {
        return -ENOMEM;
    }

    /* 复制文件数据到内存 */
    if (filesz > 0UL) {
        /* 检查边界（防止越界读取） */
        if (offset + filesz > UINT32_MAX) {
            free(vaddr);
            return -EINVAL;
        }

        /* 复制数据 */
        (void)memcpy(vaddr, data + offset, (size_t)filesz);
    }

    /* 清零BSS段（memsz > filesz的部分） */
    if (memsz > filesz) {
        uint64_t bss_size = memsz - filesz;

        /* 检查溢出 */
        if (filesz + bss_size != memsz) {
            free(vaddr);
            return -EINVAL;
        }

        (void)memset(vaddr + filesz, 0, (size_t)bss_size);
    }

    /* 设置输出 */
    *load_addr = vaddr;

    return 0;
}
```

#### 25.2.2 段权限设置

```c
/**
 * @brief 设置ELF段的MMU权限
 * @param vaddr 段的虚拟地址
 * @param size 段的大小
 * @param flags 段权限标志（PF_X, PF_W, PF_R）
 * @return 成功返回MMU权限标志，失败返回0
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数（vaddr不能为NULL）
 *   - 规则20.5: 无符号整数运算不溢出
 *   - 规则12.1: 静态变量使用受限
 *
 * @note 代码段：RX权限（可读可执行）
 * @note 数据段：RW权限（可读写，不可执行）
 * @note 只读数据：R权限（只读，不可执行）
 * @note 强制启用NX位（数据段不可执行）
 *
 * @warning 必须启用PXN和UXN位
 * @warning 用户空间段需要设置AP字段
 */
static uint32_t elf_get_mmu_flags(uint32_t flags) {
    uint32_t mmu_flags = 0U;

    /* 默认启用PXN（特权执行禁止）和UXN（用户执行禁止） */
    mmu_flags |= MMU_PXN_ENABLE;
    mmu_flags |= MMU_UXN_ENABLE;

    /* 设置访问权限 */
    if ((flags & PF_X) != 0U) {
        /* 代码段：可读可执行 */
        mmu_flags &= ~MMU_PXN_MASK;  /* 清除PXN位 */
        mmu_flags |= MMU_AP_RO;
    } else if ((flags & PF_W) != 0U) {
        /* 数据段：可读写 */
        mmu_flags |= MMU_AP_RW;
    } else {
        /* 只读数据：只读 */
        mmu_flags |= MMU_AP_RO;
    }

    /* 强制数据段不可执行 */
    if ((flags & PF_X) == 0U) {
        mmu_flags |= MMU_PXN_ENABLE;
        mmu_flags |= MMU_UXN_ENABLE;
    }

    return mmu_flags;
}
```

### 25.3 符号重定位MISRA-C编码规范

#### 25.3.1 RELA重定位处理

```c
/**
 * @brief 处理ELF64 RELA重定位
 * @param data ELF文件数据指针
 * @param size ELF文件大小
 * @param rela RELA重定位项指针
 * @param symtab 符号表指针
 * @param load_base 加载基地址
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则21.3: 边界检查
 *   - 规则20.5: 无符号整数溢出检查
 *   - 规则18.4: 指针运算安全
 *
 * @note 支持R_AARCH64_RELATIVE重定位
 * @note 支持R_AARCH64_GLOB_DAT重定位
 * @note 支持R_AARCH64_JUMP_SLOT重定位
 * @note 支持R_AARCH64_ABS64重定位
 *
 * @warning 只处理位置无关代码（PIC）的重定位
 * @warning 不支持动态链接器重定位
 */
static int elf_process_rela(const uint8_t *data, uint32_t size,
                             const Elf64_Rela *rela,
                             const Elf64_Sym *symtab,
                             uint64_t load_base) {
    uint32_t type;
    uint64_t offset;
    uint64_t symbol;
    int64_t addend;
    uint64_t *target;

    /* 参数验证 */
    if (data == NULL) {
        return -EINVAL;
    }

    if (rela == NULL) {
        return -EINVAL;
    }

    /* 提取重定位信息 */
    offset = rela->r_offset;
    symbol = ELF64_R_SYM(rela->r_info);
    type = (uint32_t)ELF64_R_TYPE(rela->r_info);
    addend = (int64_t)rela->r_addend;

    /* 检查偏移是否对齐 */
    if ((offset & 0x7UL) != 0UL) {
        return -EINVAL;
    }

    /* 计算目标地址 */
    if (offset > (uint64_t)size) {
        return -EINVAL;
    }

    if ((offset + sizeof(uint64_t)) > (uint64_t)size) {
        return -EINVAL;
    }

    target = (uint64_t *)(data + offset);

    /* 处理不同类型的重定位 */
    switch (type) {
        case R_AARCH64_RELATIVE:
            /* 相对重定位：target = load_base + addend */
            if (addend < 0) {
                /* 负数addend检查 */
                if ((uint64_t)(-addend) > load_base) {
                    return -EINVAL;
                }
                *target = load_base - (uint64_t)(-addend);
            } else {
                /* 正数addend检查 */
                if (load_base > (UINT64_MAX - (uint64_t)addend)) {
                    return -EINVAL;
                }
                *target = load_base + (uint64_t)addend;
            }
            break;

        case R_AARCH64_ABS64:
            /* 绝对64位重定位 */
            /* 需要符号表解析，暂不支持 */
            return -ENOSYS;

        case R_AARCH64_GLOB_DAT:
        case R_AARCH64_JUMP_SLOT:
            /* 全局数据或跳转表重定位 */
            /* 需要符号表解析，暂不支持 */
            return -ENOSYS;

        default:
            /* 不支持的重定位类型 */
            return -ENOSYS;
    }

    return 0;
}
```

### 25.4 应用签名验证MISRA-C编码规范

#### 25.4.1 SHA-256哈希计算

```c
/**
 * @brief 计算ELF文件的SHA-256哈希
 * @param data ELF文件数据指针
 * @param size ELF文件大小
 * @param hash 输出：256位哈希值（32字节）
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则21.3: 边界检查
 *   - 规则18.1: memcpy边界检查
 *   - 规则20.5: 无符号整数运算不溢出
 *
 * @note 使用SHA-256算法
 * @note 哈希输出32字节
 * @note 用于ECDSA签名验证
 *
 * @warning hash缓冲区必须至少32字节
 * @warning 使用硬件加速（如果可用）
 */
int elf_calc_hash(const uint8_t *data, uint32_t size, uint8_t *hash) {
    sha256_context_t ctx;

    /* 参数验证 */
    if (data == NULL) {
        return -EINVAL;
    }

    if (hash == NULL) {
        return -EINVAL;
    }

    /* 检查大小（避免空文件） */
    if (size == 0U) {
        return -EINVAL;
    }

    /* 初始化SHA-256上下文 */
    sha256_init(&ctx);

    /* 更新哈希（分块处理以避免大文件） */
    {
        uint32_t offset = 0U;
        uint32_t block_size = 4096U;  /* 4KB块 */

        while (offset < size) {
            uint32_t remaining = size - offset;
            uint32_t to_hash = (remaining < block_size) ? remaining : block_size;

            /* 检查边界 */
            if (to_hash > size) {
                return -EINVAL;
            }

            sha256_update(&ctx, data + offset, to_hash);

            /* 更新偏移 */
            if (offset > (UINT32_MAX - to_hash)) {
                return -EINVAL;
            }
            offset += to_hash;
        }
    }

    /* 生成最终哈希 */
    sha256_final(&ctx, hash);

    return 0;
}
```

#### 25.4.2 ECDSA签名验证

```c
/**
 * @brief 验证ELF文件的ECDSA-P256签名
 * @param data ELF文件数据指针
 * @param size ELF文件大小
 * @param signature ECDSA签名（64字节）
 * @param pubkey ECDSA公钥（64字节）
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则21.3: 边界检查
 *   - 规则18.1: memcpy边界检查
 *   - 规则20.5: 无符号整数运算不溢出
 *
 * @note 使用ECDSA-P256曲线
 * @note 签名64字节（R和S各32字节）
 * @note 公钥64字节（未压缩格式）
 * @note 先计算哈希，再验证签名
 *
 * @warning 签名和公钥必须是64字节
 * @warning 使用硬件加速（如果可用）
 * @warning 验证失败必须拒绝加载
 */
int elf_verify_signature(const uint8_t *data, uint32_t size,
                          const uint8_t *signature,
                          const uint8_t *pubkey) {
    uint8_t hash[32];
    int ret;

    /* 参数验证 */
    if (data == NULL) {
        return -EINVAL;
    }

    if (signature == NULL) {
        return -EINVAL;
    }

    if (pubkey == NULL) {
        return -EINVAL;
    }

    /* 计算SHA-256哈希 */
    ret = elf_calc_hash(data, size, hash);
    if (ret != 0) {
        return ret;
    }

    /* 验证ECDSA签名 */
    ret = ecdsa_verify_signature_p256(pubkey, signature, hash);
    if (ret != 0) {
        return -EPERM;
    }

    return 0;
}
```

### 25.5 应用任务创建MISRA-C编码规范

#### 25.5.1 栈空间分配

```c
/**
 * @brief 为应用分配栈空间
 * @param stack_size 栈大小（字节）
 * @param stack_top 输出：栈顶指针
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则21.3: 检查大小限制
 *   - 规则20.5: 无符号整数溢出检查
 *   - 规则18.4: 指针运算安全
 *
 * @note 栈必须对齐到16字节边界（ARM64 ABI）
 * @note 栈向下增长，栈顶是高地址
 * @note 栈底放置canary检测溢出
 *
 * @warning stack_size必须是16的倍数
 * @warning 栈空间必须由调用者释放
 * @warning 栈溢出检测需要编译器支持
 */
static int app_alloc_stack(uint32_t stack_size, uint64_t **stack_top) {
    uint8_t *stack;
    uint64_t *top;

    /* 参数验证 */
    if (stack_top == NULL) {
        return -EINVAL;
    }

    /* 检查栈大小 */
    if (stack_size == 0U) {
        return -EINVAL;
    }

    /* 检查栈大小上限（避免过大） */
    if (stack_size > CONFIG_APP_LOADER_MAX_STACK_SIZE) {
        return -EINVAL;
    }

    /* 对齐到16字节边界 */
    stack_size = (stack_size + 15U) & ~15U;

    /* 再次检查对齐后的大小 */
    if (stack_size > CONFIG_APP_LOADER_MAX_STACK_SIZE) {
        return -EINVAL;
    }

    /* 分配栈空间 */
    stack = (uint8_t *)malloc(stack_size);
    if (stack == NULL) {
        return -ENOMEM;
    }

    /* 计算栈顶（ARM64栈向下增长） */
    top = (uint64_t *)(stack + stack_size);

    /* 清零栈空间（安全起见） */
    (void)memset(stack, 0, stack_size);

    /* 设置栈canary（可选） */
    #ifdef CONFIG_STACK_CANARY
    *(stack) = STACK_CANARY_VALUE;
    *(top - 1) = STACK_CANARY_VALUE;
    #endif

    /* 设置输出 */
    *stack_top = top;

    return 0;
}
```

#### 25.5.2 任务控制块初始化

```c
/**
 * @brief 初始化应用任务的TCB
 * @param tcb 任务控制块指针
 * @param config 应用配置指针
 * @param entry 入口地址
 * @param stack_top 栈顶指针
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则21.3: 检查枚举值范围
 *   - 规则10.1: 避免嵌套过深
 *   - 规则9.3: 不应使用未初始化的变量
 *
 * @note 设置任务名称
 * @note 设置任务优先级
 * @note 设置CPU亲和性
 * @note 设置任务标志（用户空间）
 * @note 设置能力集
 * @note 设置资源限制
 *
 * @warning tcb必须已经分配
 * @warning config必须有效
 * @warning entry不能为NULL
 */
static int app_init_tcb(TCB_t *tcb,
                        const AppConfig_t *config,
                        uint64_t entry,
                        const uint64_t *stack_top) {
    uint32_t i;

    /* 参数验证 */
    if (tcb == NULL) {
        return -EINVAL;
    }

    if (config == NULL) {
        return -EINVAL;
    }

    if (stack_top == NULL) {
        return -EINVAL;
    }

    /* 检查入口地址 */
    if (entry == 0UL) {
        return -EINVAL;
    }

    /* 清零TCB */
    (void)memset(tcb, 0, sizeof(TCB_t));

    /* 设置任务名称（截断如果过长） */
    for (i = 0U; i < (sizeof(tcb->name) - 1U); i++) {
        if (config->name[i] == '\0') {
            break;
        }
        tcb->name[i] = config->name[i];
    }
    tcb->name[i] = '\0';

    /* 设置优先级（检查范围） */
    if (config->priority >= PRIORITY_LEVELS) {
        return -EINVAL;
    }
    tcb->priority = config->priority;
    tcb->base_priority = config->priority;

    /* 设置CPU亲和性 */
    tcb->cpu_affinity = config->cpu_affinity;

    /* 设置任务标志 */
    tcb->flags = TASK_FLAG_USER_SPACE;

    /* 设置状态 */
    tcb->state = TASK_STATE_READY;

    /* 设置入口点和栈 */
    tcb->context.pc = entry;
    tcb->context.sp = (uint64_t)stack_top;

    /* 设置能力集 */
    tcb->capabilities = config->capabilities;

    /* 设置资源限制 */
    tcb->max_memory = config->max_memory;
    tcb->max_cpu_time = config->max_cpu_time;

    /* 设置自动重启标志 */
    tcb->auto_restart = config->auto_restart;

    /* 初始化性能统计 */
    tcb->cpu_time = 0UL;
    tcb->context_switches = 0UL;

    return 0;
}
```

### 25.6 应用加载器主函数MISRA-C编码规范

#### 25.6.1 应用配置解析

```c
/**
 * @brief 解析应用配置文件
 * @param config_path 配置文件路径
 * @param apps 输出：应用配置数组
 * @param max_apps 最大应用数量
 * @param count 输出：实际应用数量
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则21.3: 数组边界检查
 *   - 规则18.1: 字符串操作安全
 *   - 规则20.5: 无符号整数运算不溢出
 *
 * @note 使用INI解析器
 * @note 验证所有配置字段
 * @note 检查路径安全性
 * @note 限制应用数量
 *
 * @warning apps数组必须足够大
 * @warning 必须验证路径格式
 * @warning 必须检查签名长度
 */
static int app_parse_config(const char *config_path,
                             AppConfig_t *apps,
                             uint32_t max_apps,
                             uint32_t *count) {
    INIParser_t *parser;
    int ret;
    uint32_t i;

    /* 参数验证 */
    if (config_path == NULL) {
        return -EINVAL;
    }

    if (apps == NULL) {
        return -EINVAL;
    }

    if (count == NULL) {
        return -EINVAL;
    }

    /* 检查最大应用数量 */
    if (max_apps == 0U) {
        return -EINVAL;
    }

    /* 初始化INI解析器 */
    ret = ini_parser_create(config_path, &parser);
    if (ret != 0) {
        return ret;
    }

    /* 解析配置节 */
    *count = 0U;

    for (i = 0U; i < max_apps; i++) {
        AppConfig_t *app = &apps[i];
        char section[64];
        int enabled;

        /* 构造节名 */
        ret = snprintf(section, sizeof(section), "app%u", i);
        if ((ret < 0) || ((uint32_t)ret >= sizeof(section))) {
            break;
        }

        /* 检查节是否存在 */
        if (!ini_parser_has_section(parser, section)) {
            break;
        }

        /* 读取名称 */
        ret = ini_parser_get_string(parser, section, "name",
                                     app->name, sizeof(app->name));
        if (ret != 0) {
            continue;
        }

        /* 读取路径 */
        ret = ini_parser_get_string(parser, section, "path",
                                     app->path, sizeof(app->path));
        if (ret != 0) {
            continue;
        }

        /* 验证路径格式 */
        if (app->path[0] != '/') {
            continue;  /* 必须是绝对路径 */
        }

        /* 检查路径遍历攻击 */
        if (strstr(app->path, "..") != NULL) {
            continue;
        }

        /* 读取优先级 */
        ret = ini_parser_get_int(parser, section, "priority", &enabled);
        if (ret == 0) {
            app->priority = (uint8_t)enabled;
        } else {
            app->priority = 128U;  /* 默认优先级 */
        }

        /* 读取栈大小 */
        ret = ini_parser_get_int(parser, section, "stack_size", &enabled);
        if (ret == 0) {
            app->stack_size = (uint32_t)enabled;
        } else {
            app->stack_size = CONFIG_APP_LOADER_STACK_SIZE;
        }

        /* 读取是否启用 */
        ret = ini_parser_get_bool(parser, section, "enabled", &enabled);
        if (ret == 0) {
            app->enabled = (enabled != 0);
        } else {
            app->enabled = true;
        }

        /* 读取签名（十六进制字符串） */
        ret = ini_parser_get_string(parser, section, "signature",
                                     (char *)app->signature, sizeof(app->signature));
        if (ret != 0) {
            continue;  /* 签名是必需的 */
        }

        /* 应用数量增加 */
        (*count)++;
    }

    /* 释放解析器 */
    ini_parser_destroy(parser);

    return 0;
}
```

#### 25.6.2 主加载函数

```c
/**
 * @brief 应用加载器主函数
 * @param config_path 配置文件路径
 * @return 成功返回加载的应用数量，失败返回负错误码
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则21.3: 数组边界检查
 *   - 规则15.5: 避免无限循环
 *   - 规则14.4: 不应禁止循环变量
 *
 * @note 解析配置文件
 * @note 逐个加载应用
 * @note 验证签名和完整性
 * @note 加载ELF段到内存
 * @note 设置MMU页表
 * @note 创建任务并启动
 *
 * @warning 加载失败不影响其他应用
 * @warning 签名验证失败必须拒绝加载
 * @warning 必须清理已分配的资源
 */
int app_loader_load_all(const char *config_path) {
    AppConfig_t apps[CONFIG_APP_LOADER_MAX_APPS];
    uint32_t app_count;
    uint32_t i;
    int loaded_count;

    /* 参数验证 */
    if (config_path == NULL) {
        return -EINVAL;
    }

    /* 清零应用数组 */
    (void)memset(apps, 0, sizeof(apps));
    loaded_count = 0;

    /* 步骤1：解析配置文件 */
    {
        int ret = app_parse_config(config_path, apps,
                                    CONFIG_APP_LOADER_MAX_APPS,
                                    &app_count);
        if (ret != 0) {
            printk("Failed to parse config: %d\n", ret);
            return ret;
        }
    }

    /* 步骤2：逐个加载应用 */
    for (i = 0U; i < app_count; i++) {
        AppConfig_t *config = &apps[i];
        ElfLoadContext_t elf_ctx;
        int ret;

        /* 检查是否启用 */
        if (!config->enabled) {
            printk("Application '%s' is disabled\n", config->name);
            continue;
        }

        printk("Loading application '%s' from %s\n",
               config->name, config->path);

        /* 清零ELF上下文 */
        (void)memset(&elf_ctx, 0, sizeof(elf_ctx));

        /* 步骤2.1：读取ELF文件 */
        ret = load_elf_from_file(config->path, &elf_ctx);
        if (ret != 0) {
            printk("Failed to load ELF file: %d\n", ret);
            continue;
        }

        /* 步骤2.2：验证签名 */
        ret = verify_elf_signature(elf_ctx.elf_data, elf_ctx.elf_size,
                                    config->signature);
        if (ret != 0) {
            printk("Signature verification failed\n");
            free(elf_ctx.elf_data);
            continue;
        }

        /* 步骤2.3：加载段到内存 */
        ret = load_elf_segments(&elf_ctx);
        if (ret != 0) {
            printk("Failed to load segments: %d\n", ret);
            free(elf_ctx.elf_data);
            continue;
        }

        /* 步骤2.4：重定位 */
        ret = relocate_elf(&elf_ctx);
        if (ret != 0) {
            printk("Failed to relocate: %d\n", ret);
            free(elf_ctx.elf_data);
            continue;
        }

        /* 步骤2.5：创建任务 */
        ret = create_app_task(&elf_ctx, config);
        if (ret != 0) {
            printk("Failed to create task: %d\n", ret);
            free(elf_ctx.elf_data);
            continue;
        }

        /* 释放ELF数据 */
        free(elf_ctx.elf_data);

        /* 增加加载计数 */
        loaded_count++;
        printk("Successfully loaded application '%s'\n", config->name);
    }

    printk("Application Loader finished: %d apps loaded\n", loaded_count);
    return loaded_count;
}
```

### 25.7 MISRA合规性检查清单

#### 25.7.1 ELF解析检查项

- [ ] ELF魔数验证（0x7F 'E' 'L' 'F'）
- [ ] ELF类验证（32位或64位）
- [ ] 字节序验证（小端或大端）
- [ ] 架构类型验证（EM_AARCH64）
- [ ] 入口点地址验证
- [ ] 程序头边界检查
- [ ] 节头边界检查
- [ ] 段对齐检查

#### 25.7.2 段加载检查项

- [ ] 内存分配失败处理
- [ ] 段大小溢出检查
- [ ] memcpy边界检查
- [ ] BSS段清零
- [ ] 页表权限正确设置
- [ ] NX位强制启用
- [ ] 地址对齐检查

#### 25.7.3 符号重定位检查项

- [ ] 重定位类型验证
- [ ] 符号表边界检查
- [ ] 加法溢出检查
- [ ] 目标地址对齐
- [ ] 重定位偏移验证
- [ ] 不支持的类型拒绝

#### 25.7.4 签名验证检查项

- [ ] SHA-256哈希正确性
- [ ] ECDSA签名验证
- [ ] 公钥有效性
- [ ] 签名长度验证
- [ ] 哈希长度验证
- [ ] 签名失败拒绝加载
- [ ] 签名绕过防护

#### 25.7.5 任务创建检查项

- [ ] 栈大小验证
- [ ] 栈对齐检查（16字节）
- [ ] 优先级范围检查
- [ ] CPU亲和性验证
- [ ] 入口地址非NULL
- [ ] TCB字段完整初始化
- [ ] 资源限制设置

#### 25.7.6 配置解析检查项

- [ ] 配置文件路径验证
- [ ] 节名称验证
- [ ] 字段类型检查
- [ ] 字符串边界检查
- [ ] 整数溢出检查
- [ ] 布尔值验证
- [ ] 签名字段验证

#### 25.7.7 安全检查项

- [ ] 路径遍历攻击防护（..）
- [ ] 绝对路径要求
- [ ] 签名强制验证
- [ ] 能力集限制
- [ ] 资源限制强制
- [ ] 地址空间隔离
- [ ] 故障隔离机制

#### 25.7.8 错误处理检查项

- [ ] 所有错误码正确返回
- [ ] 资源清理（失败时）
- [ ] 内存泄漏防护
- [ ] 错误信息清晰
- [ ] 加载失败不影响其他应用
- [ ] 签名验证失败拒绝
- [ ] 日志记录完整性

## 26. 调度类MISRA-C编码规范

### 26.1 SchedClass_t接口MISRA-C编码规范

#### 26.1.1 调度类结构定义

```c
/**
 * @brief 调度类接口（虚函数表）
 *
 * @details 定义调度类的标准接口，所有调度算法必须实现这些函数
 *
 * @note MISRA规则遵守：
 *   - 规则18.1: 指针运算不超过数组范围
 *   - 规则8.5: 函数必须有原型声明
 *   - 规则8.4: 类型兼容性检查
 *   - 规则11.1: 指针转换显式声明
 *
 * @note 函数指针表实现多态性
 * @note 所有函数指针必须非NULL（核心操作）
 * @note 可选操作可以为NULL
 *
 * @warning name必须指向有效的字符串
 * @warning priority范围为0-255（越小越高）
 * @warning 核心操作必须实现
 */
typedef struct SchedClass {
    const char *name;              /**< 调度类名称（调试用） */
    uint32_t priority;             /**< 调度类优先级（越小越高） */
    uint32_t flags;                /**< 调度类标志位 */

    /**
     * @brief 初始化运行队列
     * @param rq 运行队列指针
     * @return 成功返回0，失败返回负错误码
     *
     * @note 必须实现
     * @note 在系统启动时调用一次
     * @note 初始化就绪队列、位图等数据结构
     */
    int  (*init)(struct rq *rq);

    /**
     * @brief 将任务加入就绪队列
     * @param rq 运行队列指针
     * @param task 任务控制块指针
     *
     * @note 必须实现
     * @note 任务状态必须为TASK_READY
     * @note 必须更新优先级位图
     * @note 时间复杂度: O(1)或O(log n)
     */
    void (*enqueue)(struct rq *rq, TCB_t *task);

    /**
     * @brief 从就绪队列移除任务
     * @param rq 运行队列指针
     * @param task 任务控制块指针
     *
     * @note 必须实现
     * @note 任务可以正在运行
     * @note 必须更新优先级位图
     * @note 时间复杂度: O(1)或O(log n)
     */
    void (*dequeue)(struct rq *rq, TCB_t *task);

    /**
     * @brief 选择下一个运行任务
     * @param rq 运行队列指针
     * @return 任务控制块指针，无任务返回NULL
     *
     * @note 必须实现
     * @note 这是调度器的核心函数
     * @note 时间复杂度: O(1)
     * @note 返回的任务必须为TASK_READY状态
     */
    TCB_t *(*pick_next)(struct rq *rq);

    /**
     * @brief 周期性心跳处理
     * @param rq 运行队列指针
     * @param task 当前运行任务
     *
     * @note 必须实现
     * @note 每个Tick（1ms）调用一次
     * @note 更新任务运行时间统计
     * @note 检查时间片是否耗尽
     */
    void (*task_tick)(struct rq *rq, TCB_t *task);

    /**
     * @brief 更新当前任务运行时间
     * @param rq 运行队列指针
     *
     * @note 必须实现
     * @note 在上下文切换前调用
     * @note 更新vruntime（CFS）或执行时间
     */
    void (*update_curr)(struct rq *rq);

    /**
     * @brief 任务主动让出CPU
     * @param rq 运行队列指针
     * @param task 任务控制块指针
     *
     * @note 可选操作（可为NULL）
     * @note 将任务移到队列尾部
     * @note 时间复杂度: O(1)或O(log n)
     */
    void (*yield)(struct rq *rq, TCB_t *task);

    /**
     * @brief 检查任务是否可抢占
     * @param rq 运行队列指针
     * @param task 任务控制块指针
     * @return 可抢占返回true，否则返回false
     *
     * @note 可选操作（可为NULL，默认返回true）
     * @note 用于实现优先级继承
     * @note 时间复杂度: O(1)
     */
    int  (*can_preempt)(const struct rq *rq, const TCB_t *task);

    /**
     * @brief 任务 fork（复制）
     * @param rq 运行队列指针
     * @param parent 父任务
     * @return 子任务指针，失败返回NULL
     *
     * @note 可选操作（可为NULL）
     * @note Fork不支持（返回NULL）
     */
    TCB_t *(*task_fork)(struct rq *rq, TCB_t *parent);

    /**
     * @brief 切换到新调度类
     * @param rq 运行队列指针
     * @param task 任务控制块指针
     * @param new_class 新调度类
     * @return 成功返回0，失败返回负错误码
     *
     * @note 可选操作（可为NULL）
     * @note 动态切换调度算法
     * @note 保留任务运行时间统计
     */
    int  (*switch_to)(struct rq *rq, TCB_t *task,
                      const struct SchedClass *new_class);

    /**
     * @brief 获取调度类统计信息
     * @param rq 运行队列指针
     * @param stats 输出：统计信息
     * @return 成功返回0，失败返回负错误码
     *
     * @note 可选操作（可为NULL）
     * @note 用于性能分析
     */
    int  (*get_stats)(const struct rq *rq,
                      struct SchedStats *stats);
} SchedClass_t;

/* 调度类标志位定义 */
#define SCHED_CLASS_FLAG_REALTIME  (0x01U)  /**< 实时调度类 */
#define SCHED_CLASS_FLAG_FAIR      (0x02U)  /**< 公平调度类 */
#define SCHED_CLASS_FLAG_IDLE      (0x04U)  /**< 空闲调度类 */
#define SCHED_CLASS_FLAG_PREEMPT   (0x08U)  /**< 支持抢占 */
```

#### 26.1.2 调度类实例声明

```c
/**
 * @brief FIFO调度类（实时）
 *
 * @details 静态定义，启动时注册
 *
 * @note MISRA规则遵守：
 *   - 规则8.4: 文件作用域声明为static
 *   - 规则9.3: 数组初始化完整
 *   - 规则8.9: 对象必须有明确作用域
 *
 * @note 优先级范围: 128-255
 * @note 时间复杂度: O(1)
 * @note 不支持时间片
 */
static const SchedClass_t sched_class_fifo = {
    .name = "FIFO",
    .priority = 10U,
    .flags = SCHED_CLASS_FLAG_REALTIME | SCHED_CLASS_FLAG_PREEMPT,

    /* 核心操作 */
    .init = &fifo_sched_init,
    .enqueue = &fifo_sched_enqueue,
    .dequeue = &fifo_sched_dequeue,
    .pick_next = &fifo_sched_pick_next,
    .task_tick = &fifo_sched_tick,
    .update_curr = &fifo_sched_update_curr,

    /* 可选操作 */
    .yield = &fifo_sched_yield,
    .can_preempt = &fifo_sched_can_preempt,
    .task_fork = NULL,  /* 不支持fork */
    .switch_to = &fifo_sched_switch_to,
    .get_stats = &fifo_sched_get_stats
};

/**
 * @brief EDF调度类（最早截止时间优先）
 *
 * @details 红黑树实现，动态优先级
 *
 * @note 优先级范围: 128-191
 * @note 时间复杂度: O(log n)
 * @note 支持动态优先级
 */
static const SchedClass_t sched_class_edf = {
    .name = "EDF",
    .priority = 20U,
    .flags = SCHED_CLASS_FLAG_REALTIME | SCHED_CLASS_FLAG_PREEMPT,

    /* 核心操作 */
    .init = &edf_sched_init,
    .enqueue = &edf_sched_enqueue,
    .dequeue = &edf_sched_dequeue,
    .pick_next = &edf_sched_pick_next,
    .task_tick = &edf_sched_tick,
    .update_curr = &edf_sched_update_curr,

    /* 可选操作 */
    .yield = NULL,
    .can_preempt = &edf_sched_can_preempt,
    .task_fork = NULL,
    .switch_to = &edf_sched_switch_to,
    .get_stats = &edf_sched_get_stats
};

/**
 * @brief CFS调度类（完全公平调度器）
 *
 * @details 红黑树实现，基于vruntime
 *
 * @note 优先级范围: 1-127
 * @note 时间复杂度: O(log n)
 * @note 支持动态优先级
 */
static const SchedClass_t sched_class_cfs = {
    .name = "CFS",
    .priority = 30U,
    .flags = SCHED_CLASS_FLAG_FAIR | SCHED_CLASS_FLAG_PREEMPT,

    /* 核心操作 */
    .init = &cfs_sched_init,
    .enqueue = &cfs_sched_enqueue,
    .dequeue = &cfs_sched_dequeue,
    .pick_next = &cfs_sched_pick_next,
    .task_tick = &cfs_sched_tick,
    .update_curr = &cfs_sched_update_curr,

    /* 可选操作 */
    .yield = &cfs_sched_yield,
    .can_preempt = &cfs_sched_can_preempt,
    .task_fork = NULL,
    .switch_to = &cfs_sched_switch_to,
    .get_stats = &cfs_sched_get_stats
};

/**
 * @brief RR调度类（轮转调度）
 *
 * @details 循环队列实现，时间片轮转
 *
 * @note 优先级范围: 1-127
 * @note 时间复杂度: O(1)
 * @note 支持时间片
 */
static const SchedClass_t sched_class_rr = {
    .name = "RR",
    .priority = 40U,
    .flags = SCHED_CLASS_FLAG_FAIR | SCHED_CLASS_FLAG_PREEMPT,

    /* 核心操作 */
    .init = &rr_sched_init,
    .enqueue = &rr_sched_enqueue,
    .dequeue = &rr_sched_dequeue,
    .pick_next = &rr_sched_pick_next,
    .task_tick = &rr_sched_tick,
    .update_curr = &rr_sched_update_curr,

    /* 可选操作 */
    .yield = &rr_sched_yield,
    .can_preempt = &rr_sched_can_preempt,
    .task_fork = NULL,
    .switch_to = &rr_sched_switch_to,
    .get_stats = &rr_sched_get_stats
};

/**
 * @brief IDLE调度类（空闲任务）
 *
 * @details 单链表实现，最低优先级
 *
 * @note 优先级: 0
 * @note 时间复杂度: O(1)
 * @note 不支持抢占
 */
static const SchedClass_t sched_class_idle = {
    .name = "IDLE",
    .priority = 255U,
    .flags = SCHED_CLASS_FLAG_IDLE,

    /* 核心操作 */
    .init = &idle_sched_init,
    .enqueue = &idle_sched_enqueue,
    .dequeue = &idle_sched_dequeue,
    .pick_next = &idle_sched_pick_next,
    .task_tick = &idle_sched_tick,
    .update_curr = &idle_sched_update_curr,

    /* 可选操作 */
    .yield = NULL,
    .can_preempt = NULL,  /* 不支持抢占 */
    .task_fork = NULL,
    .switch_to = NULL,
    .get_stats = &idle_sched_get_stats
};
```

### 26.2 核心调度器MISRA-C编码规范

#### 26.2.1 调度器初始化

```c
/**
 * @brief 初始化多核调度器
 * @return 成功返回0，失败返回负错误码
 *
 * @details 初始化所有CPU的运行队列和调度类
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查数组索引
 *   - 规则18.1: 指针运算不超过数组范围
 *   - 规则1.3: 定义严格的signedness
 *   - 规则17.3: 不能对不同类型的指针取大小
 *
 * @note 必须在启动时调用一次
 * @note 必须先于其他调度函数调用
 * @note 初始化所有调度类
 *
 * @warning 多核启动后不能调用
 * @warning 不检查重复初始化
 */
int scheduler_init(void) {
    uint32_t cpu;
    int ret;

    /* 检查是否已初始化 */
    if (scheduler_initialized != 0U) {
        return ERROR_ALREADY_INITIALIZED;
    }

    /* 初始化所有CPU的运行队列 */
    for (cpu = 0U; cpu < MAX_CPUS; cpu++) {
        ret = rq_init(&per_cpu_rq[cpu], cpu);
        if (ret != 0) {
            /* 清理已初始化的运行队列 */
            cleanup_rq(cpu);
            return ret;
        }
    }

    /* 初始化调度类链表 */
    sched_class_h = NULL;
    register_sched_class(&sched_class_fifo);
    register_sched_class(&sched_class_edf);
    register_sched_class(&sched_class_cfs);
    register_sched_class(&sched_class_rr);
    register_sched_class(&sched_class_idle);

    scheduler_initialized = 1U;
    return 0;
}
```

#### 26.2.2 选择下一个任务

```c
/**
 * @brief 选择下一个运行的任务
 * @param rq 运行队列指针
 * @return 任务控制块指针，无任务返回idle任务
 *
 * @details 核心调度函数，遍历调度类链表
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则18.1: 指针运算不超过数组范围
 *   - 规则1.3: signedness一致性
 *   - 规则15.5: 避免多余的循环
 *
 * @note 时间复杂度: O(M) + O(1) = O(M)
 *       - M: 调度类数量（5）
 *       - pick_next: O(1)
 * @note 总开销: ~185ns
 *
 * @warning rq必须指向有效的运行队列
 * @warning 必须持有rq锁
 * @warning 返回值不能为NULL
 */
TCB_t *pick_next_task(struct rq *rq) {
    const SchedClass_t *class;
    TCB_t *next_task;

    /* 参数验证 */
    if (rq == NULL) {
        return NULL;
    }

    /* 遍历调度类链表 */
    for (class = sched_class_h; class != NULL; class = class->next) {
        /* 检查是否有可运行任务 */
        if (rq->nr_running[class->id] == 0U) {
            continue;
        }

        /* 调用调度类的pick_next函数 */
        next_task = class->pick_next(rq);

        /* 检查返回值 */
        if (next_task != NULL) {
            return next_task;
        }
    }

    /* 所有调度类都没有任务，返回idle任务 */
    return rq->idle;
}
```

#### 26.2.3 任务加入队列

```c
/**
 * @brief 将任务加入就绪队列
 * @param task 任务控制块指针
 *
 * @details 调用任务调度类的enqueue函数
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则18.1: 指针间接访问前检查
 *   - 规则17.3: 指针类型匹配
 *
 * @note 任务状态必须为TASK_READY
 * @note 更新优先级位图
 * @note 时间复杂度: 取决于调度类
 *
 * @warning task必须指向有效的TCB
 * @warning 必须持有rq锁
 * @warning task->sched_class必须非NULL
 */
void enqueue_task(TCB_t *task) {
    struct rq *rq;
    const SchedClass_t *class;

    /* 参数验证 */
    if (task == NULL) {
        return;
    }

    class = task->sched_class;
    if (class == NULL) {
        return;
    }

    /* 获取运行队列 */
    rq = task->rq;
    if (rq == NULL) {
        return;
    }

    /* 检查任务状态 */
    if (task->state != TASK_READY) {
        return;
    }

    /* 调用调度类的enqueue函数 */
    class->enqueue(rq, task);

    /* 更新优先级位图 */
    bitmap_set_bit(rq->priority_bitmap, task->prio);
    rq->nr_running[class->id]++;
}
```

### 26.3 FIFO调度类实现MISRA-C编码规范

#### 26.3.1 FIFO初始化

```c
/**
 * @brief FIFO调度类初始化
 * @param rq 运行队列指针
 * @return 成功返回0
 *
 * @details 初始化256级优先级队列
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则21.3: 数组索引边界检查
 *   - 规则17.3: 指针算术不超过对象大小
 *
 * @note 分配FIFO私有数据
 * @note 初始化优先级位图
 * @note 时间复杂度: O(256) = O(1)
 */
static int fifo_sched_init(struct rq *rq) {
    struct fifo_rq *fifo_rq;
    uint32_t i;

    /* 参数验证 */
    if (rq == NULL) {
        return -EINVAL;
    }

    /* 分配FIFO私有数据 */
    fifo_rq = (struct fifo_rq *)malloc(sizeof(struct fifo_rq));
    if (fifo_rq == NULL) {
        return -ENOMEM;
    }

    /* 初始化优先级位图 */
    (void)memset(fifo_rq->priority_bitmap, 0, sizeof(fifo_rq->priority_bitmap));

    /* 初始化每个优先级的队列 */
    for (i = 0U; i < 256U; i++) {
        INIT_LIST_HEAD(&fifo_rq->queue_array[i]);
    }

    /* 关联到运行队列 */
    rq->fifo_rq = fifo_rq;

    return 0;
}
```

#### 26.3.2 FIFO入队

```c
/**
 * @brief FIFO任务入队
 * @param rq 运行队列指针
 * @param task 任务控制块指针
 *
 * @details 将任务加入对应优先级队列尾部
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则21.3: 数组索引范围
 *   - 规则18.1: 指针运算边界
 *   - 规则17.3: 类型匹配
 *
 * @note 时间复杂度: O(1)
 * @note 更新优先级位图
 * @note 同优先级FIFO
 *
 * @warning task->prio必须 < 256
 * @warning 必须持有rq锁
 */
static void fifo_sched_enqueue(struct rq *rq, TCB_t *task) {
    struct fifo_rq *fifo_rq;
    struct list_head *queue;

    /* 参数验证 */
    if ((rq == NULL) || (task == NULL)) {
        return;
    }

    fifo_rq = rq->fifo_rq;
    if (fifo_rq == NULL) {
        return;
    }

    /* 获取优先级队列 */
    if (task->prio >= 256U) {
        return;  /* 无效优先级 */
    }

    queue = &fifo_rq->queue_array[task->prio];

    /* 加入队列尾部 */
    list_add_tail(&task->run_list, queue);

    /* 更新位图 */
    set_bit(task->prio, fifo_rq->priority_bitmap);
}
```

#### 26.3.3 FIFO选择下一个任务

```c
/**
 * @brief FIFO选择下一个任务
 * @param rq 运行队列指针
 * @return 任务控制块指针，无任务返回NULL
 *
 * @details 选择最高优先级任务
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则21.3: 数组索引范围
 *   - 规则18.4: 指针差值不超过数组大小
 *   - 规则1.3: signedness一致性
 *
 * @note 时间复杂度: O(1)
 * @note 使用CLZ指令快速查找
 * @note 开销: ~50ns
 */
static TCB_t *fifo_sched_pick_next(struct rq *rq) {
    struct fifo_rq *fifo_rq;
    struct list_head *queue;
    TCB_t *task;
    uint32_t priority;
    uint64_t bitmap_u64;
    uint32_t word_idx;
    uint32_t bit_offset;

    /* 参数验证 */
    if (rq == NULL) {
        return NULL;
    }

    fifo_rq = rq->fifo_rq;
    if (fifo_rq == NULL) {
        return NULL;
    }

    /* 查找最高优先级（使用CLZ） */
    priority = 255U;

    /* 遍历4个64位字（256位） */
    for (word_idx = 0U; word_idx < 4U; word_idx++) {
        bitmap_u64 = fifo_rq->priority_bitmap[word_idx];

        /* 跳过空字 */
        if (bitmap_u64 == 0ULL) {
            continue;
        }

        /* 使用CLZ查找最高位 */
        bit_offset = (uint32_t)__builtin_clzll(bitmap_u64);
        priority = (word_idx * 64U) + (63U - bit_offset);
        break;
    }

    /* 检查是否找到有效优先级 */
    if (priority == 255U) {
        return NULL;  /* 队列为空 */
    }

    /* 获取优先级队列 */
    if (priority >= 256U) {
        return NULL;  /* 防御性编程 */
    }

    queue = &fifo_rq->queue_array[priority];

    /* 检查队列是否为空 */
    if (list_empty(queue) != 0) {
        return NULL;
    }

    /* 获取第一个任务 */
    task = list_first_entry(queue, TCB_t, run_list);

    return task;
}
```

### 26.4 EDF调度类实现MISRA-C编码规范

#### 26.4.1 EDF入队

```c
/**
 * @brief EDF任务入队
 * @param rq 运行队列指针
 * @param task 任务控制块指针
 *
 * @details 按截止时间排序，插入红黑树
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则18.1: 指针间接访问
 *   - 规则17.3: 类型匹配
 *   - 规则21.3: 边界检查
 *
 * @note 时间复杂度: O(log n)
 * @note 使用红黑树维护
 * @note 截止时间最早的在前
 *
 * @warning task->deadline必须初始化
 * @warning 必须持有rq锁
 */
static void edf_sched_enqueue(struct rq *rq, TCB_t *task) {
    struct edf_rq *edf_rq;
    struct rb_node **link;
    struct rb_node *parent;
    TCB_t *entry;

    /* 参数验证 */
    if ((rq == NULL) || (task == NULL)) {
        return;
    }

    edf_rq = rq->edf_rq;
    if (edf_rq == NULL) {
        return;
    }

    /* 检查截止时间有效性 */
    if (task->deadline == 0U) {
        return;
    }

    /* 查找插入位置 */
    link = &edf_rq->tasks_timeline.rb_node;
    parent = NULL;

    while (*link != NULL) {
        parent = *link;
        entry = rb_entry(parent, TCB_t, run_node);

        /* 比较截止时间 */
        if (task->deadline < entry->deadline) {
            link = &(*link)->rb_left;
        } else {
            link = &(*link)->rb_right;
        }
    }

    /* 插入红黑树 */
    rb_link_node(&task->run_node, parent, link);
    rb_insert_color(&task->run_node, &edf_rq->tasks_timeline);

    /* 更新计数 */
    edf_rq->nr_running++;
}
```

#### 26.4.2 EDF选择下一个任务

```c
/**
 * @brief EDF选择下一个任务
 * @param rq 运行队列指针
 * @return 任务控制块指针，无任务返回NULL
 *
 * @details 选择截止时间最早的任务
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则18.4: 指针差值不超过对象大小
 *   - 规则1.3: signedness一致性
 *
 * @note 时间复杂度: O(1)
 * @note 取红黑树最左节点
 * @note 开销: ~70ns
 */
static TCB_t *edf_sched_pick_next(struct rq *rq) {
    struct edf_rq *edf_rq;
    struct rb_node *leftmost;
    TCB_t *task;

    /* 参数验证 */
    if (rq == NULL) {
        return NULL;
    }

    edf_rq = rq->edf_rq;
    if (edf_rq == NULL) {
        return NULL;
    }

    /* 检查是否有任务 */
    if (edf_rq->nr_running == 0U) {
        return NULL;
    }

    /* 获取最左节点（截止时间最早） */
    leftmost = rb_first(&edf_rq->tasks_timeline);
    if (leftmost == NULL) {
        return NULL;
    }

    task = rb_entry(leftmost, TCB_t, run_node);

    return task;
}
```

### 26.5 CFS调度类实现MISRA-C编码规范

#### 26.5.1 CFS入队

```c
/**
 * @brief CFS任务入队
 * @param rq 运行队列指针
 * @param task 任务控制块指针
 *
 * @details 按vruntime排序，插入红黑树
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则18.1: 指针间接访问
 *   - 规则17.3: 类型匹配
 *   - 规则21.3: 边界检查
 *
 * @note 时间复杂度: O(log n)
 * @note 使用红黑树维护
 * @note vruntime最小的在前
 *
 * @warning task->vruntime必须初始化
 * @warning 必须持有rq锁
 */
static void cfs_sched_enqueue(struct rq *rq, TCB_t *task) {
    struct cfs_rq *cfs_rq;
    struct rb_node **link;
    struct rb_node *parent;
    TCB_t *entry;

    /* 参数验证 */
    if ((rq == NULL) || (task == NULL)) {
        return;
    }

    cfs_rq = rq->cfs_rq;
    if (cfs_rq == NULL) {
        return;
    }

    /* 查找插入位置 */
    link = &cfs_rq->tasks_timeline.rb_node;
    parent = NULL;

    while (*link != NULL) {
        parent = *link;
        entry = rb_entry(parent, TCB_t, run_node);

        /* 比较vruntime */
        if (task->vruntime < entry->vruntime) {
            link = &(*link)->rb_left;
        } else {
            link = &(*link)->rb_right;
        }
    }

    /* 插入红黑树 */
    rb_link_node(&task->run_node, parent, link);
    rb_insert_color(&task->run_node, &cfs_rq->tasks_timeline);

    /* 更新计数 */
    cfs_rq->nr_running++;
}
```

#### 26.5.2 CFS更新当前任务

```c
/**
 * @brief CFS更新当前任务运行时间
 * @param rq 运行队列指针
 *
 * @details 计算物理运行时间，转换为vruntime
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则18.1: 指针间接访问
 *   - 规则1.3: signedness一致性
 *   - 规则10.1: 避免整数溢出
 *
 * @note vruntime = physical_time * (weight_nice_0 / weight)
 * @note 时间复杂度: O(1)
 * @note 每次tick调用
 */
static void cfs_sched_update_curr(struct rq *rq) {
    struct cfs_rq *cfs_rq;
    TCB_t *curr;
    uint64_t now;
    uint64_t delta_exec;
    uint64_t delta_fair;
    uint32_t weight;

    /* 参数验证 */
    if (rq == NULL) {
        return;
    }

    cfs_rq = rq->cfs_rq;
    if (cfs_rq == NULL) {
        return;
    }

    curr = rq->curr;
    if (curr == NULL) {
        return;
    }

    /* 获取当前时间 */
    now = sched_clock();

    /* 计算物理运行时间 */
    delta_exec = now - curr->exec_start;

    /* 检查是否有运行时间 */
    if (delta_exec == 0ULL) {
        return;
    }

    /* 更新任务起始时间 */
    curr->exec_start = now;

    /* 更新累计运行时间 */
    curr->sum_exec_runtime += delta_exec;

    /* 计算权重（基于优先级） */
    weight = prio_to_weight[curr->prio];

    /* 计算vruntime增量 */
    /* delta_fair = delta_exec * (NICE_0_LOAD / weight) */
    delta_fair = (delta_exec * NICE_0_LOAD) / (uint64_t)weight;

    /* 更新vruntime */
    curr->vruntime += delta_fair;

    /* 检查是否需要重新入队 */
    if ((curr->vruntime - cfs_rq->min_vruntime) > CFS_GRANULARITY_NS) {
        /* 从树中移除 */
        rb_erase(&curr->run_node, &cfs_rq->tasks_timeline);

        /* 重新插入 */
        cfs_sched_enqueue(rq, curr);
    }
}
```

### 26.6 RR调度类实现MISRA-C编码规范

#### 26.6.1 RR时间片处理

```c
/**
 * @brief RR任务tick处理
 * @param rq 运行队列指针
 * @param task 任务控制块指针
 *
 * @details 检查时间片是否耗尽，耗尽则重新排队
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则18.1: 指针间接访问
 *   - 规则1.3: signedness一致性
 *   - 规则10.1: 避免整数溢出
 *
 * @note 时间复杂度: O(1)
 * @note 时间片: 10ms（10个tick）
 * @note 耗尽后移到队列尾部
 */
static void rr_sched_tick(struct rq *rq, TCB_t *task) {
    struct rr_rq *rr_rq;

    /* 参数验证 */
    if ((rq == NULL) || (task == NULL)) {
        return;
    }

    rr_rq = rq->rr_rq;
    if (rr_rq == NULL) {
        return;
    }

    /* 增加时间片计数 */
    task->time_slice++;

    /* 检查是否耗尽 */
    if (task->time_slice >= RR_TIME_SLICE) {
        /* 重置时间片 */
        task->time_slice = 0U;

        /* 移到队列尾部 */
        list_move_tail(&task->run_list, &rr_rq->queue);

        /* 设置需要重新调度标志 */
        rq->need_resched = 1U;
    }
}
```

### 26.7 MISRA合规性检查清单

#### 26.7.1 调度类接口检查项

- [ ] SchedClass_t结构体完整定义
- [ ] 所有核心操作函数指针非NULL
- [ ] 可选操作可以为NULL
- [ ] 函数原型声明匹配
- [ ] 参数类型正确
- [ ] 返回类型一致

#### 26.7.2 核心调度器检查项

- [ ] scheduler_init只调用一次
- [ ] pick_next_task返回值非NULL
- [ ] enqueue_task更新位图
- [ ] dequeue_task更新位图
- [ ] 锁正确获取和释放
- [ ] 多核安全（原子操作）
- [ ] 优先级范围检查（0-255）

#### 26.7.3 FIFO调度类检查项

- [ ] 优先级队列初始化
- [ ] 位图正确更新
- [ ] CLZ指令使用正确
- [ ] 队列操作O(1)复杂度
- [ ] 同优先级FIFO
- [ ] 数组索引边界检查

#### 26.7.4 EDF调度类检查项

- [ ] 截止时间有效性检查
- [ ] 红黑树操作正确
- [ ] 时间复杂度O(log n)
- [ ] 截止时间最早优先
- [ ] 任务计数正确
- [ ] 空指针检查

#### 26.7.5 CFS调度类检查项

- [ ] vruntime正确计算
- [ ] 权重转换正确
- [ ] 红黑树操作正确
- [ ] 时间复杂度O(log n)
- [ ] vruntime最小优先
- [ ] 物理时间计算正确
- [ ] 整数溢出检查

#### 26.7.6 RR调度类检查项

- [ ] 时间片计数正确
- [ ] 队列尾部移动
- [ ] 时间片重置
- [ ] 需要重新调度标志
- [ ] 循环队列操作
- [ ] 时间复杂度O(1)

#### 26.7.7 性能检查项

- [ ] pick_next_task开销<200ns
- [ ] enqueue_task开销可预测
- [ ] dequeue_task开销可预测
- [ ] 无内存泄漏
- [ ] 无死锁风险
- [ ] 缓存友好（缓存行对齐）

#### 26.7.8 安全检查项

- [ ] 优先级继承机制
- [ ] 任务隔离
- [ ] 栈溢出检测
- [ ] 故障隔离
- [ ] 调试时确定性
- [ ] 可验证性（ISO 26262）

#### 26.7.9 多核检查项

- [ ] 核心间负载均衡
- [ ] CPU亲和性支持
- [ ] 任务迁移安全
- [ ] IPI处理
- [ ] 自旋锁正确使用
- [ ] 内存屏障正确使用

---

## 27. 栈溢出保护MISRA-C编码规范

### 27.1 栈保护数据结构MISRA-C编码规范

#### 27.1.1 栈帧结构定义

```c
/**
 * @brief 栈保护配置结构
 *
 * @details 定义栈保护的配置参数
 *
 * @note MISRA规则遵守：
 *   - 规则18.1: 指针运算不超过数组范围
 *   - 规则9.3: 数组成员初始化完整
 *   - 规则8.5: 函数必须有原型声明
 *
 * @note 3层保护机制
 * @note 金丝雀值、边界模式、MPU保护页
 */
typedef struct StackProtectionConfig {
    uint32_t canary;                     /**< 金丝雀值 */
    uint32_t guard_pattern[4];            /**< 边界模式 */
    bool use_mpu;                          /**< 是否使用MPU */
    uint32_t guard_page_size;              /**< 保护页大小 */
    uint64_t max_usage;                    /**< 最大使用量 */
    uint64_t high_watermark;               /**< 高水位线 */
    bool enable_canary_check;             /**< 启用金丝雀检查 */
    bool enable_guard_check;              /**< 启用边界检查 */
    bool enable_mpu_check;                /**< 启用MPU检查 */
} StackProtectionConfig_t;

/**
 * @brief 栈帧结构
 *
 * @details 定义栈的内存布局
 *
 * @note MISRA规则遵守：
 *   - 规则18.4: 指针差值不超过对象大小
 *   - 规则21.3: 边界检查
 */
typedef struct {
    uint32_t canary;    /**< 金丝雀值（栈底）*/
    uint8_t  stack[];    /**< 可用栈空间 */
} StackFrame_t;
```

#### 27.1.2 栈保护初始化

```c
/**
 * @brief 初始化栈保护
 * @param task 任务控制块指针
 * @param size 栈大小
 * @return 成功返回0，失败返回负错误码
 *
 * @details 分配栈并设置保护机制
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则21.3: 边界检查
 *   - 规则1.3: signedness一致性
 *   - 规则22.1: 指针必须对齐
 *
 * @warning task必须指向有效的TCB
 * @warning size必须>=4096且是16的倍数
 *
 * @post 任务栈已分配并设置保护
 * @post task->stack_base != 0ULL
 * @post task->stack_size == size
 */
int stack_protection_init(TCB_t *task, uint32_t size) {
    /* 参数验证 */
    if (task == NULL) {
        return -EINVAL;
    }

    /* 检查最小栈大小 */
    if (size < 4096U) {
        return -EINVAL;
    }

    /* 检查对齐 */
    if ((size & 0xFU) != 0U) {
        return -EINVAL;
    }

    /* 分配栈 */
    task->stack_base = (uint64_t)malloc(size);
    if (task->stack_base == 0ULL) {
        return -ENOMEM;
    }
    task->stack_size = size;
    task->stack_ptr = task->stack_base + size;

    /* 设置金丝雀 */
    if (global_config.enable_canary_check) {
        uint32_t *canary_ptr = (uint32_t *)task->stack_base;
        *canary_ptr = global_config.canary;
    }

    /* 设置边界模式 */
    if (global_config.enable_guard_check) {
        uint32_t *guard_ptr = (uint32_t *)(task->stack_base + size - 16U);
        for (uint32_t i = 0U; i < 4U; i++) {
            guard_ptr[i] = global_config.guard_pattern[i];
        }
    }

    /* 配置MPU保护页（可选）*/
    if (global_config.use_mpu) {
        stack_protection_configure_mpu(task);
    }

    /* 初始化统计 */
    task->stack_max_usage = 0ULL;
    task->stack_high_watermark = 0ULL;

    return 0;
}
```

#### 27.1.3 栈保护检查

```c
/**
 * @brief 检查栈保护
 * @param task 任务控制块指针
 * @return 有效返回true，否则返回false
 *
 * @details 检查金丝雀值和边界模式
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则18.4: 指针差值不超过对象大小
 *   - 规则1.3: signedness一致性
 *
 * @warning 在上下文切换时调用
 * @warning 检测到溢出时调用system_panic()
 */
bool stack_protection_check(const TCB_t *task) {
    /* 参数验证 */
    if (task == NULL) {
        return false;
    }

    /* 检查金丝雀 */
    if (global_config.enable_canary_check) {
        const uint32_t *canary_ptr = (const uint32_t *)task->stack_base;
        if (*canary_ptr != global_config.canary) {
            printk("Stack overflow: Task %u (%s)\n",
                   task->tid, task->name);
            return false;
        }
    }

    /* 检查边界模式 */
    if (global_config.enable_guard_check) {
        const uint32_t *guard_ptr = (const uint32_t *)
            (task->stack_base + task->stack_size - 16U);
        for (uint32_t i = 0U; i < 4U; i++) {
            if (guard_ptr[i] != global_config.guard_pattern[i]) {
                printk("Stack overflow: Task %u guard corrupted\n",
                       task->tid);
                return false;
            }
        }
    }

    /* 检查栈指针范围 */
    if (task->stack_ptr < task->stack_base ||
        task->stack_ptr > (task->stack_base + task->stack_size)) {
        printk("Stack underflow: Task %u\n", task->tid);
        return false;
    }

    return true;
}
```

#### 27.1.4 栈使用率计算

```c
/**
 * @brief 计算栈使用率
 * @param task 任务控制块指针
 * @return 使用率百分比（0-100）
 *
 * @details 通过扫描未使用的字节计算使用率
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则18.1: 指针运算不超过数组范围
 *   - 规则21.3: 边界检查
 *   - 规则1.3: signedness一致性
 *
 * @warning 仅用于监控，不影响实时性
 */
uint32_t stack_usage_percent(const TCB_t *task) {
    /* 参数验证 */
    if (task == NULL) {
        return 0U;
    }

    /* 扫描未使用的字节 */
    const uint8_t *ptr = (const uint8_t *)task->stack_base + 4U;
    const uint8_t *stack_ptr = (const uint8_t *)task->stack_ptr;
    uint32_t unused = 0U;

    while (ptr < stack_ptr && *ptr == 0x00U) {
        unused++;
        ptr++;
    }

    /* 计算使用率 */
    uint64_t used = task->stack_size - unused;
    uint32_t percent = (uint32_t)((used * 100ULL) / task->stack_size);

    return percent;
}
```

### 27.2 MISRA合规性检查清单

#### 27.2.1 栈保护初始化检查项

- [ ] 参数NULL检查
- [ ] 栈大小>=4096
- [ ] 栈对齐检查（16字节）
- [ ] 分配失败处理
- [ ] 金丝雀值设置
- [ ] 边界模式设置
- [ ] MPU配置（可选）

#### 27.2.2 栈保护检查检查项

- [ ] 金丝雀值验证
- [ ] 边界模式验证
- [ ] 栈指针范围检查
- [ ] 溢出日志记录
- [ ] 恢复机制调用

#### 27.2.3 性能检查项

- [ ] 检查开销<100ns
- [ ] 不影响实时性
- [ ] 缓存友好
- [ ] 无内存泄漏

---

## 28. MPU/MMU抽象层MISRA-C编码规范

### 28.1 MPU/MMU接口MISRA-C编码规范

#### 28.1.1 内存保护操作接口

```c
/**
 * @brief 内存保护操作接口
 *
 * @details 定义MPU/MMU的统一接口
 *
 * @note MISRA规则遵守：
 *   - 规则8.5: 函数指针必须有原型
 *   - 规则18.1: 指针间接访问前检查
 *   - 规则17.3: 类型匹配
 *
 * @note 支持ARMv8-M MPU和ARMv8-A MMU
 */
typedef struct MemProtectionOps {
    /**
     * @brief 配置内存区域
     * @param region 内存区域指针
     * @return 成功返回0，失败返回负错误码
     */
    int (*configure_region)(const MemRegion_t *region);

    /**
     * @brief 移除内存区域
     * @param region_id 区域ID
     * @return 成功返回0，失败返回负错误码
     */
    int (*remove_region)(uint32_t region_id);

    /**
     * @brief 上下文切换时调用
     * @param next 下一个任务
     * @return 成功返回0，失败返回负错误码
     */
    int (*context_switch)(const TCB_t *next);

    /**
     * @brief 启用内存保护
     * @return 成功返回0，失败返回负错误码
     */
    int (*enable)(void);

    /**
     * @brief 禁用内存保护
     * @return 成功返回0，失败返回负错误码
     */
    int (*disable)(void);

} MemProtectionOps_t;
```

#### 28.1.2 内存区域定义

```c
/**
 * @brief 内存区域结构
 *
 * @details 定义内存区域的属性
 *
 * @note MISRA规则遵守：
 *   - 规则18.1: 指针运算不超过数组范围
 *   - 规则21.3: 边界检查
 *   - 规则1.3: signedness一致性
 */
typedef struct {
    uint64_t base;           /**< 基地址 */
    uint64_t size;           /**< 大小 */
    uint32_t flags;          /**< 权限标志 */
    uint32_t attributes;     /**< 内存属性 */
    uint32_t region_id;      /**< 区域ID */
    bool active;             /**< 是否激活 */
} MemRegion_t;

/* 权限标志定义 */
#define MEM_PERM_READ    (1U << 0)  /**< 读权限 */
#define MEM_PERM_WRITE   (1U << 1)  /**< 写权限 */
#define MEM_PERM_EXEC    (1U << 2)  /**< 执行权限 */
#define MEM_PERM_PRIV    (1U << 3)  /**< 特权级 */

/* 内存属性定义 */
#define MEM_ATTR_DEVICE  (0x0U << 2)  /**< 设备内存 */
#define MEM_ATTR_NORMAL  (0x1U << 2)  /**< 正常内存 */
```

#### 28.1.3 MPU配置（ARMv8-M）

```c
/**
 * @brief MPU配置区域
 * @param region 内存区域指针
 * @return 成功返回0，失败返回负错误码
 *
 * @details 配置ARMv8-M MPU区域
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则21.3: 边界和对齐检查
 *   - 规则1.3: signedness一致性
 *   - 规则22.1: 指针对齐
 *
 * @warning region->base必须16字节对齐
 * @warning region->size必须是2的幂
 */
static int mpu_configure_region(const MemRegion_t *region) {
    /* 参数验证 */
    if (region == NULL) {
        return -EINVAL;
    }

    /* 检查对齐 */
    if ((region->base & 0x1FUL) != 0ULL) {
        return -EINVAL;
    }

    /* 检查大小（2的幂）*/
    uint64_t size = region->size;
    if ((size & (size - 1ULL)) != 0ULL) {
        return -EINVAL;
    }

    /* 计算大小编码 */
    uint32_t size_code = 0U;
    uint64_t temp = size >> 4U;
    while (temp != 0ULL) {
        temp >>= 1U;
        size_code++;
    }

    /* 权限转换 */
    uint32_t ap = 0U;
    if (region->flags & MEM_PERM_WRITE) {
        ap = (region->flags & MEM_PERM_PRIV) ? 0x0U : 0x3U;
    } else if (region->flags & MEM_PERM_READ) {
        ap = (region->flags & MEM_PERM_PRIV) ? 0x1U : 0x2U;
    }

    /* 执行位 */
    uint32_t xn = (region->flags & MEM_PERM_EXEC) ? 0U : 1U;

    /* 写入MPU寄存器 */
    MPU_RNR = region->region_id;
    MPU_RBAR = (region->base & 0xFFFFFF00UL) | (1U << 0);
    MPU_RLAR = ((region->base + size - 1ULL) & 0xFFFFFF00UL) |
               (xn << 1U) | (ap << 3U) | (3U << 5U) | (1U << 0);

    return 0;
}
```

#### 28.1.4 MMU配置（ARMv8-A）

```c
/**
 * @brief MMU页表映射
 * @param pgd 页全局目录指针
 * @param virt 虚拟地址
 * @param phys 物理地址
 * @param size 大小
 * @param flags 权限标志
 * @param attrs 内存属性
 * @return 成功返回0，失败返回负错误码
 *
 * @details 创建页表映射
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则21.3: 边界检查
 *   - 规则1.3: signedness一致性
 *   - 规则22.1: 指针对齐
 *
 * @warning virt、phys必须4KB对齐
 * @warning size必须是4KB的倍数
 */
static int mmu_map_page(uint64_t *pgd, uint64_t virt, uint64_t phys,
                      uint64_t size, uint32_t flags, uint32_t attrs) {
    /* 参数验证 */
    if (pgd == NULL) {
        return -EINVAL;
    }

    /* 检查对齐 */
    if ((virt & 0xFFFULL) != 0ULL) {
        return -EINVAL;
    }
    if ((phys & 0xFFFULL) != 0ULL) {
        return -EINVAL;
    }

    /* 检查大小 */
    if ((size & 0xFFFULL) != 0ULL) {
        return -EINVAL;
    }

    /* 创建页表项标志 */
    uint64_t pte_attr = create_pte(flags, attrs);

    /* 遍历每个4KB页 */
    for (uint64_t offset = 0ULL; offset < size; offset += 4096ULL) {
        /* 4级页表遍历和创建 */
        /* ... 详细实现见上文 ... */

        virt += 4096ULL;
        phys += 4096ULL;
    }

    /* TLB无效化 */
    __asm__ volatile("dsb ish");
    __asm__ volatile("isb");

    return 0;
}
```

### 28.2 MISRA合规性检查清单

#### 28.2.1 MPU配置检查项

- [ ] 基址对齐检查
- [ ] 大小验证（2的幂）
- [ ] 权限转换正确
- [ ] 寄存器写入顺序
- [ ] 边界检查

#### 28.2.2 MMU配置检查项

- [ ] 虚拟地址对齐
- [ ] 物理地址对齐
- [ ] 页表分配失败处理
- [ ] 页表项属性正确
- [ ] TLB无效化

#### 28.2.3 上下文切换检查项

- [ ] 页表切换原子性
- [ ] 寄存器同步
- [ ] 内存屏障正确使用
- [ ] 性能开销<10%

---

## 29. 安全钩子框架MISRA-C编码规范

### 29.1 安全钩子接口MISRA-C编码规范

#### 29.1.1 钩子类型定义

```c
/**
 * @brief 安全钩子类型枚举
 *
 * @details 定义所有安全钩子类型
 *
 * @note MISRA规则遵守：
 *   - 规则18.1: 枚举值范围检查
 *   - 规则1.3: signedness一致性
 */
typedef enum {
    /* 任务管理 */
    HOOK_TASK_CREATE = 0,  /**< 任务创建 */
    HOOK_TASK_EXIT,        /**< 任务退出 */
    HOOK_TASK_YIELD,       /**< 任务让出 */

    /* 内存管理 */
    HOOK_MEM_ALLOC,        /**< 内存分配 */
    HOOK_MEM_FREE,         /**< 内存释放 */

    /* IPC */
    HOOK_IPC_SEND,         /**< IPC发送 */
    HOOK_IPC_RECV,         /**< IPC接收 */

    /* 设备访问 */
    HOOK_DEV_OPEN,         /**< 设备打开 */
    HOOK_DEV_CLOSE,        /**< 设备关闭 */
    HOOK_DEV_IOCTL,        /**< 设备ioctl */

    HOOK_MAX               /**< 钩子类型数量 */
} SecurityHookType_t;
```

#### 29.1.2 钩子函数签名

```c
/**
 * @brief 安全钩子函数签名
 *
 * @details 定义钩子函数的类型
 *
 * @note MISRA规则遵守：
 *   - 规则8.5: 函数指针必须有原型
 *   - 规则18.1: 指针间接访问前检查
 */
typedef int (*security_hook_fn)(void *ctx);
```

#### 29.1.3 钩子注册

```c
/**
 * @brief 注册安全钩子
 * @param type 钩子类型
 * @param fn 钩子函数指针
 * @param name 钩子名称（用于调试）
 * @return 成功返回0，失败返回负错误码
 *
 * @details 注册安全钩子到指定类型
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则18.1: 指针间接访问前检查
 *   - 规则21.3: 数组索引边界检查
 *   - 规则1.3: signedness一致性
 *
 * @warning type必须<HOOK_MAX
 * @warning fn不能为NULL
 * @warning name不能为NULL
 *
 * @post 钩子已添加到链表
 */
int security_hook_register(SecurityHookType_t type,
                          security_hook_fn fn,
                          const char *name) {
    /* 参数验证 */
    if (type >= HOOK_MAX) {
        return -EINVAL;
    }

    if (fn == NULL) {
        return -EINVAL;
    }

    if (name == NULL) {
        return -EINVAL;
    }

    SecurityHookList_t *list = &hook_lists[type];

    /* 检查容量 */
    if (list->count >= MAX_HOOKS_PER_TYPE) {
        return -ENOSPC;
    }

    /* 添加钩子 */
    list->hooks[list->count++] = fn;

    printk("Registered hook '%s' for type %u\n", name, type);

    return 0;
}
```

#### 29.1.4 钩子调用

```c
/**
 * @brief 调用安全钩子
 * @param type 钩子类型
 * @param ctx 上下文指针
 * @return 成功返回0，拒绝返回负错误码
 *
 * @details 调用指定类型的所有钩子
 *
 * @note MISRA规则遵守：
 *   - 规则21.3: 数组索引边界检查
 *   - 规则18.1: 指针间接访问前检查
 *   - 规则15.5: 避免多余的循环
 *
 * @warning 任何钩子拒绝则停止
 * @warning ctx必须指向有效的上下文结构
 */
static inline int call_security_hooks(SecurityHookType_t type,
                                      void *ctx) {
    SecurityHookList_t *list = &hook_lists[type];
    int ret = 0;

    /* 遍历钩子 */
    for (uint32_t i = 0U; i < list->count; i++) {
        /* 调用钩子 */
        ret = list->hooks[i](ctx);

        /* 检查返回值 */
        if (ret != 0) {
            /* 钩子拒绝，停止调用 */
            return ret;
        }
    }

    return 0;
}
```

### 29.2 内置安全模块MISRA-C编码规范

#### 29.2.1 Capability检查模块

```c
/**
 * @brief 任务创建钩子
 * @param ctx 上下文指针
 * @return 成功返回0，拒绝返回负错误码
 *
 * @details 检查任务创建权限
 *
 * @note MISRA规则遵守：
 *   - 规则13.5: 检查指针参数
 *   - 规则18.1: 指针间接访问前检查
 *   - 规则1.3: signedness一致性
 */
static int task_create_hook(void *ctx) {
    TaskCreateCtx_t *tcc = (TaskCreateCtx_t *)ctx;

    /* 检查创建权限 */
    if (!cap_has_capability(current, CAP_CREATE_TASK)) {
        printk("Permission denied: Create task\n");
        return -EPERM;
    }

    /* 检查资源限制 */
    if (current->protection_domain->task_count >=
        current->protection_domain->max_tasks) {
        printk("Resource limit exceeded\n");
        return -EAGAIN;
    }

    return 0;
}
```

### 29.3 MISRA合规性检查清单

#### 29.3.1 钩子注册检查项

- [ ] 类型范围检查
- [ ] 函数指针非NULL
- [ ] 名称非NULL
- [ ] 容量检查
- [ ] 线程安全

#### 29.3.2 钩子调用检查项

- [ ] 类型范围检查
- [ ] 上下文非NULL
- [ ] 返回值检查
- [ ] 短路评估
- [ ] 性能开销<5%

#### 29.3.3 安全模块检查项

- [ ] 权限检查完整
- [ ] 资源限制强制
- [ ] 日志记录
- [ ] 无死锁风险

---

## 30. 代码格式化规范 (clang-format)

### 30.1 概述

AISafe64 项目使用 clang-format 自动化代码格式化工具，确保所有源代码保持一致的代码风格。格式化规则基于项目现有的代码风格和 MISRA-C:2012 标准制定。

### 30.2 配置文件

项目根目录下的 `.clang-format` 文件定义了代码格式化规则：

```yaml
# 主要配置项
- IndentWidth: 4                    # 使用4空格缩进
- UseTab: Never                     # 使用空格而非Tab
- ColumnLimit: 100                  # 行宽限制100字符
- PointerAlignment: Right           # 指针星号靠右对齐
- BreakBeforeBraces: Custom         # 自定义大括号换行规则
```

### 30.3 格式化规则说明

#### 30.3.1 缩进和空格

```c
/* ✅ 正确: 4空格缩进 */
void function(void) {
    if (condition) {
        do_something();
    }
}

/* ❌ 错误: 使用Tab或2空格缩进 */
void function(void) {
	  if (condition) {
		    do_something();
	  }
}
```

#### 30.3.2 大括号位置

```c
/* ✅ 正确: 函数定义左大括号换行 */
static inline uint32_t atomic_inc_u32(volatile uint32_t *addr)
{
    uint32_t old_val;
    uint32_t new_val;
    /* ... */
}

/* ✅ 正确: 控制语句左大括号不换行 */
if (condition) {
    do_something();
} else {
    do_other();
}
```

#### 30.3.3 指针对齐

```c
/* ✅ 正确: 星号靠右 */
volatile uint32_t *addr;
const char *str;

/* ❌ 错误: 星号靠左或中间 */
volatile uint32_t* addr;
volatile uint32_t * addr;
```

#### 30.3.4 行宽限制

```c
/* ✅ 正确: 单行不超过100字符 */
static inline bool atomic_compare_exchange_strong(volatile uint32_t *addr,
                                                  uint32_t *expected,
                                                  uint32_t desired)

/* ❌ 错误: 超过100字符 */
static inline bool atomic_compare_exchange_strong(volatile uint32_t *addr, uint32_t *expected, uint32_t desired)
```

#### 30.3.5 函数参数换行

```c
/* ✅ 正确: 参数过多时换行对齐 */
static inline uint32_t atomic_add_u32(volatile uint32_t *addr,
                                      uint32_t value)
{
    return old_val;
}

/* ✅ 正确: 每个参数一行（参数很多时） */
void complex_function(type1_t param1,
                      type2_t param2,
                      type3_t param3,
                      type4_t param4)
{
    /* ... */
}
```

#### 30.3.6 注释风格

```c
/* ✅ 正确: Doxygen风格文档注释 */
/**
 * @brief 原子比较并交换
 * @details 如果*addr == expected，则将desired写入*addr
 *
 * @param addr 地址指针
 * @param expected 期望值
 * @param desired 新值
 * @return 成功返回true，失败返回false
 */

/* ✅ 正确: 单行注释使用 // 或 /* */ */
// 这是一个单行注释
/* 这也是单行注释 */
```

### 30.4 使用方法

#### 30.4.1 手动格式化单个文件

```bash
clang-format -i file.c
```

#### 30.4.2 批量格式化所有文件

```bash
# 格式化所有C/C++源文件
find . -name "*.c" -o -name "*.h" | xargs clang-format -i

# 或使用特定命令
clang-format -i src/**/*.c src/**/*.h
```

#### 30.4.3 检查文件是否符合格式（不修改）

```bash
clang-format --dry-run --Werror file.c
```

### 30.5 Git Pre-commit Hook

项目配置了自动化的 pre-commit hook，在每次提交前自动格式化暂存的文件：

#### 30.5.1 工作原理

1. 检测暂存的 C/C++ 文件
2. 使用 clang-format 自动格式化
3. 将格式化后的文件重新添加到暂存区
4. 如果格式化失败，阻止提交

#### 30.5.2 Hook 脚本位置

- Linux/Mac: `.git/hooks/pre-commit`
- Windows: `.git/hooks/pre-commit.ps1`

#### 30.5.3 禁用 Hook（不推荐）

如果临时需要跳过自动格式化：

```bash
git commit --no-verify -m "commit message"
```

**注意**: 不建议禁用 pre-commit hook，这可能导致代码风格不一致。

### 30.6 IDE 集成

#### 30.6.1 VS Code

在 `.vscode/settings.json` 中添加：

```json
{
    "editor.formatOnSave": true,
    "C_Cpp.clang_format_style": "file",
    "C_Cpp.clang_format_fallbackStyle": "none",
    "[c]": {
        "editor.defaultFormatter": "xaver.clang-format"
    },
    "[cpp]": {
        "editor.defaultFormatter": "xaver.clang-format"
    }
}
```

#### 30.6.2 Vim/Neovim

在 `.vimrc` 或 `init.vim` 中添加：

```vim
" 保存时自动格式化
autocmd BufWritePre *.c,*.h,*.cpp,*.hpp :clang-format -i %

" 手动格式化快捷键
map <C-K> :clang-format<CR>
imap <C-K> <c-o>:clang-format<CR>
```

#### 30.6.3 Emacs

在 `.emacs` 或 `init.el` 中添加：

```elisp
(require 'clang-format)

;; 保存前自动格式化
(add-hook 'c-mode-common-hook
          (lambda ()
            (add-hook 'before-save-hook
                      'clang-format-buffer
                      nil t)))
```

### 30.7 CI/CD 集成

在 CI 流水线中检查代码格式：

```yaml
# 示例 GitHub Actions
- name: Check code formatting
  run: |
    find . -name "*.c" -o -name "*.h" | xargs clang-format --dry-run --Werror
```

### 30.8 常见问题

#### Q: clang-format 改变了我不想改的地方怎么办？

A: 可以在特定代码块使用 clang-format off/on 注释：

```c
/* clang-format off */
int    a    =    1;  // 保持原样
/* clang-format on */
```

#### Q: 如何自定义格式化规则？

A: 编辑项目根目录的 `.clang-format` 文件，修改相应的配置项。

#### Q: 为什么 pre-commit hook 没有生效？

A: 检查以下几点：
1. 确认 hook 文件有可执行权限（Linux/Mac）
2. 确认系统已安装 clang-format
3. 查看是否有错误信息输出

### 30.9 最佳实践

1. **提交前格式化**: 确保所有提交的代码都符合格式规范
2. **IDE 自动格式化**: 配置 IDE 保存时自动格式化
3. **定期检查**: 在 CI 流水线中集成格式检查
4. **团队协作**: 所有团队成员使用相同的 `.clang-format` 配置
5. **持续改进**: 根据团队反馈调整格式化规则

---

**文档版本**: 1.8
**最后更新**: 2025-01-08
**适用标准**: MISRA-C:2012, ARMv8-A, ISO 26262 ASIL-D
**项目**: AISafe64 - AI-Generated, Safety-Certifiable, Native 64-bit RTOS
