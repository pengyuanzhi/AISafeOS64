/**
 * @file spinlock.h
 * @brief AISafe64 RTOS - 自旋锁包装
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 自旋锁包装（为兼容性）
 *          - 实际实现在sync.h中
 *
 * @note MISRA-C:2012合规
 */

#ifndef SPINLOCK_H
#define SPINLOCK_H

#include "sync.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 类型别名 */
#define spinlock_t spinlock_t

/* 函数别名 */
#define spin_lock_init(lock) spinlock_init(lock)
#define spin_lock(lock) spinlock_lock(lock)
#define spin_trylock(lock) spinlock_trylock(lock)
#define spin_unlock(lock) spinlock_unlock(lock)
#define spin_lock_init(lock) spinlock_init(lock)

/* 用于sched.c的宏 */
#define SPIN_LOCK_UNLOCKED {0, 0}

static inline void spin_lock_init(spinlock_t *lock)
{
    spinlock_init(lock);
}

#ifdef __cplusplus
}
#endif

#endif /* SPINLOCK_H */
