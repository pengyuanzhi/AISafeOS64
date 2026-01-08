/**
 * @file semaphore.c
 * @brief AISafe64 RTOS - 信号量实现
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 信号量实现
 *          - 支持二值信号量（max_count=1）
 *          - 支持计数信号量
 *          - 原子操作保证线程安全
 *
 * @note MISRA-C:2012合规
 * @note 后续集成任务管理器实现阻塞等待
 */

#include "sync.h"
#include "types.h"

/**
 * @brief 信号量初始化
 * @param sem 信号量指针
 * @param initial_count 初始计数
 * @param max_count 最大计数
 * @return 成功返回0，失败返回负错误码
 *
 * @details 初始化信号量
 *          - 二值信号量：max_count=1
 *          - 计数信号量：max_count>1
 */
int semaphore_init(semaphore_t *sem, int32_t initial_count, uint32_t max_count) {
    if (sem == NULL) {
        return -ERROR_INVALID_PARAM;
    }

    /* 参数验证 */
    if (initial_count < 0) {
        return -ERROR_INVALID_PARAM;
    }

    if (max_count == 0U) {
        return -ERROR_INVALID_PARAM;
    }

    if ((uint32_t)initial_count > max_count) {
        return -ERROR_INVALID_PARAM;
    }

    /* 初始化信号量 */
    sem->count = initial_count;
    sem->max_count = max_count;

    /* 内存屏障 */
    MEMORY_BARRIER();

    return ERROR_SUCCESS;
}

/**
 * @brief 信号量等待（P操作）
 * @param sem 信号量指针
 * @return 成功返回0，失败返回负错误码
 *
 * @details 如果计数为0，阻塞当前任务
 *          - 成功：计数减1
 *          - 失败：阻塞等待（TODO）
 */
int semaphore_wait(semaphore_t *sem) {
    if (sem == NULL) {
        return -ERROR_INVALID_PARAM;
    }

    while (1) {
        int32_t current_count = sem->count;

        /* 检查是否有可用资源 */
        if (current_count <= 0) {
            /* 计数为0，阻塞等待（TODO: 阻塞当前任务） */
            WFE();
            continue;
        }

        /* 尝试原子减1 */
        int32_t new_count = current_count - 1;
        int32_t old_val;
        int32_t result;

        __asm__ volatile(
            "ldxr %w0, [%2]\n"
            "cmp %w0, %w3\n"
            "b.ne 1f\n"
            "stxr %w1, %w4, [%2]\n"
            "1:"
            : "=&r"(old_val), "=&r"(result)
            : "r"(&sem->count), "r"(current_count), "r"(new_count)
            : "cc", "memory"
        );

        if ((old_val == current_count) && (result == 0)) {
            /* 成功获取信号量 */
            return ERROR_SUCCESS;
        }

        /* CAS失败，重试 */
        WFE();
    }
}

/**
 * @brief 信号量尝试等待
 * @param sem 信号量指针
 * @return 成功返回0，失败返回负错误码
 *
 * @details 非阻塞模式
 *          - 如果计数>0，计数减1并返回成功
 *          - 如果计数=0，立即返回失败
 */
int semaphore_trywait(semaphore_t *sem) {
    if (sem == NULL) {
        return -ERROR_INVALID_PARAM;
    }

    int32_t current_count = sem->count;

    /* 检查是否有可用资源 */
    if (current_count <= 0) {
        /* 计数为0，立即返回失败 */
        return -ERROR_WOULD_BLOCK;
    }

    /* 尝试原子减1 */
    int32_t new_count = current_count - 1;
    int32_t old_val;
    int32_t result;

    __asm__ volatile(
        "ldxr %w0, [%2]\n"
        "cmp %w0, %w3\n"
        "b.ne 1f\n"
        "stxr %w1, %w4, [%2]\n"
        "1:"
        : "=&r"(old_val), "=&r"(result)
        : "r"(&sem->count), "r"(current_count), "r"(new_count)
        : "cc", "memory"
    );

    if ((old_val == current_count) && (result == 0)) {
        /* 成功获取信号量 */
        return ERROR_SUCCESS;
    }

    /* CAS失败，返回错误 */
    return -ERROR_WOULD_BLOCK;
}

/**
 * @brief 信号量释放（V操作）
 * @param sem 信号量指针
 * @return 成功返回0，失败返回负错误码
 *
 * @details 释放资源，计数加1
 *          - 如果计数达到上限，返回错误
 *          - 唤醒等待的任务
 */
int semaphore_post(semaphore_t *sem) {
    if (sem == NULL) {
        return -ERROR_INVALID_PARAM;
    }

    while (1) {
        int32_t current_count = sem->count;

        /* 检查是否超过最大计数 */
        if ((uint32_t)current_count >= sem->max_count) {
            /* 计数已达到上限 */
            return -ERROR_OVERFLOW;
        }

        /* 尝试原子加1 */
        int32_t new_count = current_count + 1;
        int32_t old_val;
        int32_t result;

        __asm__ volatile(
            "ldxr %w0, [%2]\n"
            "cmp %w0, %w3\n"
            "b.ne 1f\n"
            "stxr %w1, %w4, [%2]\n"
            "1:"
            : "=&r"(old_val), "=&r"(result)
            : "r"(&sem->count), "r"(current_count), "r"(new_count)
            : "cc", "memory"
        );

        if ((old_val == current_count) && (result == 0)) {
            /* 成功释放信号量 */

            /* 内存屏障 */
            MEMORY_BARRIER();

            /* 唤醒等待的任务 */
            SEVL();

            return ERROR_SUCCESS;
        }

        /* CAS失败，重试 */
        WFE();
    }
}

/**
 * @brief 获取信号量计数
 * @param sem 信号量指针
 * @return 当前计数
 */
int32_t semaphore_getcount(semaphore_t *sem) {
    if (sem == NULL) {
        return -1;
    }

    /* 内存屏障确保读取最新值 */
    MEMORY_BARRIER();

    return sem->count;
}
