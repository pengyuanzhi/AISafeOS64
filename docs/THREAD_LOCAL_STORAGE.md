# Thread-Local Storage (TLS) Implementation Guide

## Overview

This document describes the thread-local storage (TLS) implementation in AISafe64 RTOS, particularly the `__thread` storage class and `errno` variable.

## Current Status

### ✅ Implemented
- POSIX `errno.h` header file with standard error codes
- `strerror()` function (thread-safe, uses static buffer)
- `perror()` function (uses `strerror()` and `errno`)
- `errno` variable declaration (currently global, needs TLS)

### ⏳ TODO: True Thread-Local Storage
The current `errno` implementation uses a global variable:
```c
__thread int errno = 0;
```

This is a **placeholder** that needs proper TLS support.

## Implementation Plan

### Phase 1: Compiler Support (Current)

GCC and Clang support `__thread` storage class specifier:
```c
__thread int errno = 0;  /* Per-thread variable */
```

**Requirements:**
- GCC 4.8+ or Clang 3.7+
- ARMv8-A architecture
- `-ftls-model=local-exec` compile flag

### Phase 2: Linker Support

The linker must support TLS relocations:
- **R_AARCH64_TLSLE_MOVW_TPREL_G1**
- **R_AARCH64_TLSLE_MOVW_TPREL_G0_NC**
- **R_AARCH64_TLSLE_ADD_TPREL_HI12**

**Build System Changes:**
```makefile
# Add TLS support flags
CFLAGS += -ftls-model=local-exec
LDFLAGS += -Wl,--tls-get-addr=gd
```

### Phase 3: Runtime TLS Setup

**Kernel Requirements:**
1. **TPIDR_EL0** register for userspace TLS base
2. **TPIDR_EL1** register for kernel TLS base
3. **Thread creation** must initialize TPIDR_EL1
4. **Context switch** must preserve TPIDR_EL1

**Implementation:**
```c
// In task creation (sched.c)
void init_tls(TCB_t *task) {
    // Allocate TLS region
    task->tls_base = kmalloc(TLS_SIZE);

    // Set TPIDR_EL1 register
    write_sysreg(task->tls_base, TPIDR_EL1);
}

// In context switch
void switch_to(TCB_t *prev, TCB_t *next) {
    // Save/restore TPIDR_EL1
    uint64_t prev_tls = read_sysreg(TPIDR_EL1);
    prev->tls_base = prev_tls;

    write_sysreg(next->tls_base, TPIDR_EL1);
}
```

### Phase 4: True __thread Implementation

Once runtime TLS is ready, the placeholder code becomes fully functional:

```c
// errno.c - Future implementation
#define __thread __attribute__((tls))

extern __thread int errno = 0;  /* Truly per-thread */
```

## Usage Examples

### Current Usage (Works with Global errno)

```c
#include "errno.h"

int example_function(void) {
    if (some_operation() < 0) {
        // errno is set by the failing function
        printf("Error: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}
```

### Future Usage (with True TLS)

```c
#include "errno.h"
#include <pthread.h>

void *thread1(void *arg) {
    errno = 0;  // Thread 1's errno
    some_operation();
    printf("Thread 1 errno: %d\n", errno);
    return NULL;
}

void *thread2(void *arg) {
    errno = 0;  // Thread 2's errno (independent!)
    some_operation();
    printf("Thread 2 errno: %d\n", errno);
    return NULL;
}
```

## Comparison with Linux

| Feature | Linux (glibc) | AISafe64 (Current) | AISafe64 (Future) |
|---------|----------------|-------------------|-------------------|
| `errno` variable | ✅ Thread-local | ⚠️ Global | ✅ Thread-local |
| `__thread` support | ✅ Full | ⚠️ Placeholder | ✅ Full |
| `strerror()` | ✅ Thread-safe | ✅ Thread-safe | ✅ Thread-safe |
| `perror()` | ✅ Thread-safe | ✅ Thread-safe | ✅ Thread-safe |
| TLS relocations | ✅ All | ❌ None | ✅ ARMv8 TLS |

## Testing

### Unit Test

```c
// test_errno.c
#include "errno.h"
#include "assert.h"

void test_errno(void) {
    errno = EAGAIN;
    assert(errno == EAGAIN);

    char *msg = strerror(EINVAL);
    assert(msg != NULL);

    perror("test");  // Output: test: Invalid argument
}

void test_strerror_coverage(void) {
    // Test known error codes
    assert(strerror(EPERM) != NULL);
    assert(strerror(ETIMEDOUT) != NULL);

    // Test unknown error code
    char *msg = strerror(9999);
    assert(msg != NULL);
    assert(strstr(msg, "Unknown error") != NULL);
}
```

### Integration Test

```c
// test_semaphore.c
#include "sync.h"
#include "errno.h"

void test_semaphore_errors(void) {
    semaphore_t sem;

    // Test invalid parameter
    int ret = semaphore_init(NULL, 0, 1);
    assert(ret == -EINVAL);
    assert(errno == EINVAL);

    printf("Error: %s\n", strerror(errno));
    perror("semaphore_init");
}
```

## Performance Considerations

### Current Implementation (Global errno)
- **Pros**: Simple, no overhead
- **Cons**: Not thread-safe, race conditions in multi-threaded code

### Future Implementation (True TLS)
- **Pros**: Thread-safe, POSIX compliant
- **Cons**: TPIDR_EL1 access latency (~1-2 cycles)

### Optimization Tips
```c
// Use local variable to avoid repeated errno access
int saved_errno = errno;
some_function_that_may_modify_errno();
errno = saved_errno;
```

## Porting from Linux

Most Linux code using `errno` will work without modification:

```c
// Linux code (portable to AISafe64)
if (open("file.txt", O_RDONLY) < 0) {
    if (errno == ENOENT) {
        printf("File not found\n");
    } else {
        perror("open");
    }
}
```

## References

1. **POSIX Standard**: IEEE Std 1003.1-2008
2. **Linux Kernel**: `include/uapi/asm-generic/errno.h`
3. **glibc Source**: `string/strerror.c` and `sysdeps/unix/sysv/linux/x86_64/errno.c`
4. **ARMv8-A Architecture Reference Manual**: TPIDR_EL1 register
5. **GCC Documentation**: Thread-Local Storage (https://gcc.gnu.org/onlinedocs/gcc/Thread-Local.html)

## Summary

The current implementation provides a **compatibility layer** that works for single-threaded code. Multi-threaded applications should wait for Phase 3 (Runtime TLS Setup) to be completed before relying on `errno` for thread-local behavior.

**Next Steps:**
1. Implement TPIDR_EL1 initialization in scheduler
2. Add TLS context switching
3. Update build system for TLS support
4. Add multi-threaded unit tests

Until then, consider `errno` as a **process-global** variable in single-threaded contexts.
