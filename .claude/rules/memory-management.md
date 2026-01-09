## 5. 内存管理规范

### 5.1 动态内存分配

#### 5.1.1 分配和释放
```c
/* ✅ 正确: 检查返回值 */
void *ptr = malloc(100);
if (ptr == NULL) 
{
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
if (task == NULL) 
{
    return ERROR_OUT_OF_MEMORY;
}

/* ❌ 错误: 内存泄漏 */
void function(void) 
{
    void *ptr = malloc(100);
    return;  /* 忘记释放ptr */
}
```

#### 5.1.2 对齐分配
```c
/* 分配对齐的内存 */
void *aligned_alloc(uint32_t size, uint32_t alignment) 
{
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

void aligned_free(void *ptr) 
{
    if (ptr != NULL) 
    {
        free(*((void **)ptr - 1));
    }
}
```

### 5.2 栈使用规范

#### 5.2.1 限制栈大小
```c
/* ❌ 错误: 大数组在栈上 */
void function(void) 
{
    uint8_t buffer[10000];  /* 10KB，可能栈溢出 */
}

/* ✅ 正确: 使用静态或动态分配 */
void function(void) 
{
    static uint8_t buffer[10000];  /* 静态分配 */
    /* 或 */
    uint8_t *buffer = malloc(10000);  /* 动态分配 */
    if (buffer != NULL) 
    {
        /* 使用buffer */
        free(buffer);
    }
}
```

#### 5.2.2 栈检查
```c
/* 使用栈水印检测栈使用 */
#define STACK_CANARY 0xABCD1234U

void stack_init(TCB_t *task, void *stack, uint32_t size) 
{
    uint32_t *stack_bottom = (uint32_t *)stack;

    /* 设置栈魔数 */
    for (uint32_t i = 0U; i < (size / sizeof(uint32_t)); i++) 
    {
        stack_bottom[i] = STACK_CANARY;
    }

    task->stack_base = stack;
    task->stack_size = size;
}

bool stack_check(TCB_t *task) 
{
    uint32_t *stack_bottom = (uint32_t *)task->stack_base;
    uint32_t used = 0U;

    /* 计算栈使用量 */
    for (uint32_t i = 0U; i < (task->stack_size / sizeof(uint32_t)); i++) 
    {
        if (stack_bottom[i] != STACK_CANARY) 
        {
            used = (i + 1U) * sizeof(uint32_t);
            break;
        }
    }

    uint32_t usage_percent = (used * 100U) / task->stack_size;

    if (usage_percent > 80U) 
    {
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

