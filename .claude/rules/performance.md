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

