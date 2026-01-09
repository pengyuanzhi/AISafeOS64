## 7. 错误处理规范

### 7.1 POSIX错误码约定

**编码约束**: 系统必须使用标准POSIX错误码（定义在`<errno.h>`）进行错误返回，确保与POSIX规范兼容。

**核心POSIX错误码**：
```c
/* POSIX标准错误码（必须包含在src/include/errno.h） */
#define EPERM        1   /* 操作不允许 */
#define ENOENT       2   /* 文件或目录不存在 */
#define ESRCH        3   /* 没有该进程 */
#define EINTR        4   /* 系统调用被中断 */
#define EIO          5   /* I/O错误 */
#define ENXIO        6   /* 没有这个设备或地址 */
#define E2BIG        7   /* 参数列表过长 */
#define ENOEXEC      8   /* 执行格式错误 */
#define EBADF        9   /* 错误的文件描述符 */
#define ECHILD      10   /* 没有子进程 */
#define EAGAIN      11   /* 再试一次 */
#define ENOMEM      12   /* 内存不足 */
#define EACCES      13   /* 权限被拒绝 */
#define EFAULT      14   /* 错误的地址 */
#define ENOTBLK     15   /* 块设备要求 */
#define EBUSY       16   /* 设备或资源忙 */
#define EEXIST      17   /* 文件存在 */
#define EXDEV       18   /* 跨设备链接 */
#define ENODEV      19   /* 没有这个设备 */
#define ENOTDIR     20   /* 不是目录 */
#define EISDIR      21   /* 是目录 */
#define EINVAL      22   /* 无效的参数 */
#define ENFILE      23   /* 文件表溢出 */
#define EMFILE      24   /* 打开文件过多 */
#define ENOTTY      25   /* 不是终端 */
#define ETXTBSY     26   /* 文本文件忙 */
#define EFBIG       27   /* 文件过大 */
#define ENOSPC      28   /* 设备上没有空间 */
#define ESPIPE      29   /* 非法查找 */
#define EROFS       30   /* 只读文件系统 */
#define EMLINK      31   /* 链接过多的文件 */
#define EPIPE       32   /* 破损的管道 */
#define EDOM        33   /* 数学参数超出定义域 */
#define ERANGE      34   /* 结果过大 */
#define EDEADLK     35   /* 资源死锁避免 */
#define ENAMETOOLONG 36  /* 文件名过长 */
#define ENOLCK      37   /* 没有记录锁可用 */
#define ENOSYS      38   /* 功能未实现 */
#define ENOTEMPTY   39   /* 目录非空 */
#define ELOOP       40   /* 符号链接层级过多 */
#define ENOMSG      42   /* 没有指定类型的消息 */
#define EIDRM       43   /* 标识符被删除 */
#define EOVERFLOW   75   /* 值过大 */
#define ETIMEDOUT   116 /* 连接超时 */
```

**错误码使用约定**：

1. **参数验证错误必须使用`EINVAL`**：
   ```c
   /* ✅ 正确：参数无效返回EINVAL */
   if (param == NULL) {
       return -EINVAL;
   }
   
   /* ❌ 错误：不要使用自定义错误码 */
   if (param == NULL) {
       return -ERROR_INVALID_PARAM;  /* 违反POSIX约定 */
   }
   ```

2. **系统调用/内核函数返回负错误码**：
   ```c
   /* 系统调用约定：成功返回0或正数，失败返回负错误码 */
   long sys_read(int fd, void *buf, size_t count) {
       if (buf == NULL) {
           return -EINVAL;  /* 返回负的POSIX错误码 */
       }
       if (fd < 0 || fd >= MAX_OPEN_FILES) {
           return -EBADF;
       }
       /* ... */
       return bytes_read;  /* 成功返回读取字节数 */
   }
   ```

3. **POSIX兼容层设置`errno`并返回-1**：
   ```c
   /* pthread等POSIX API：设置errno并返回-1 */
   int pthread_mutex_lock(pthread_mutex_t *mutex) {
       if (mutex == NULL) {
           return EINVAL;  /* 直接返回错误码 */
       }
       /* ... */
       return 0;  /* 成功返回0 */
   }
   ```

4. **内部内核函数可使用`ErrorCode_t`类型**：
   ```c
   /* 内部内核函数使用自定义错误码 */
   ErrorCode_t page_alloc(uint64_t *addr) {
       if (addr == NULL) {
           return ERROR_INVALID_PARAM;
       }
       /* ... */
       return ERROR_SUCCESS;
   }
   ```

**错误码映射规则**：

| 场景 | 使用错误码 | 返回格式 |
|------|-----------|----------|
| 系统调用参数无效 | `EINVAL` | `-EINVAL` |
| 文件描述符无效 | `EBADF` | `-EBADF` |
| 内存不足 | `ENOMEM` | `-ENOMEM` |
| 权限不足 | `EPERM` | `-EPERM` |
| 功能未实现 | `ENOSYS` | `-ENOSYS` |
| 设备忙 | `EBUSY` | `-EBUSY` |
| POSIX pthread | `EINVAL`等 | 直接返回错误码 |
| 内核函数 | 自定义错误码 | `ErrorCode_t`类型 |

**MISRA-C:2012合规性**：
```c
/* 错误码比较使用显式转换 */
int ret = some_syscall();
if (ret < 0) {
    /* 明确转换避免符号扩展问题 */
    int err = -(int32_t)ret;
    if (err == (int32_t)EINVAL) {
        /* 处理EINVAL错误 */
    }
}
```

### 7.2 错误处理模式

#### 7.2.1 返回值检查
```c
int function(void) {
    int ret;

    ret = sub_function1();
    if (ret != 0) {
        return ret;
    }

    ret = sub_function2();
    if (ret != 0) {
        return ret;
    }

    return 0;
}

/* 调用者必须检查返回值 */
int ret = function();
if (ret != 0) {
    handle_error(ret);
}
```

#### 7.2.2 资源清理模式
```c
int complex_function(void) {
    void *resource1 = NULL;
    void *resource2 = NULL;
    int ret = 0;

    resource1 = malloc(100);
    if (resource1 == NULL) {
        return -ENOMEM;
    }

    resource2 = malloc(200);
    if (resource2 == NULL) {
        free(resource1);
        return -ENOMEM;
    }

    ret = process_resources(resource1, resource2);
    if (ret != 0) {
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

