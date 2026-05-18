#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
/**
 * @file    fs_lock_shard_stress.c
 * @brief   文件锁分片锁多线程压力测试
 * @author  AISafe64 Team
 * @date    2026-05-09
 * @version 1.0
 *
 * @details 测试文件锁分片锁在多线程环境下的性能和正确性：
 *          - 多个线程同时访问不同挂载点的文件
 *          - 测试锁竞争、锁死、嵌套锁等场景
 *          - 验证分片锁的正确性
 *
 * @note MISRA-C:2012 合规
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#define FS_MAX_LOCKS         128
#define FS_SHARDS_COUNT      8
#define TEST_THREADS         64
#define TEST_ITERATIONS      10000
#define TEST_MOUNTS          8

/* 文件锁状态 */
typedef struct {
    bool locked;
    uint32_t lock_type;
    uint32_t owner_tid;
    uint32_t lock_count;
} fs_file_lock_t;

/* 文件锁条目 */
typedef struct {
    uint32_t mount_id;
    uint32_t ino;
    fs_file_lock_t lock;
    bool in_use;
} fs_lock_entry_t;

/* 分片锁结构 */
typedef struct {
    pthread_mutex_t lock;
    fs_lock_entry_t *locks;
    uint32_t lock_count;
    uint32_t index;
} fs_lock_shard_t;

/* 全局分片锁 */
static fs_lock_shard_t s_lock_shards[FS_SHARDS_COUNT];

/* 测试统计信息 */
typedef struct {
    uint64_t lock_success;
    uint64_t lock_failed;
    uint64_t unlock_success;
    uint64_t unlock_failed;
    uint64_t deadlock_count;
    uint64_t timeout_count;
} test_stats_t;

static test_stats_t g_stats = {0};

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 延迟函数（用于模拟真实场景）
 */
static inline void sleep_ms(uint64_t ms) {
    usleep(ms * 1000);
}

/**
 * @brief 获取当前线程 ID
 */
static inline uint32_t get_current_tid(void) {
    return (uint32_t)pthread_self();
}

/**
 * @brief 分片选择（位运算）
 */
static inline uint32_t select_lock_shard(uint32_t mount_id) {
    return mount_id & (FS_SHARDS_COUNT - 1);
}

/**
 * @brief 获取分片指针
 */
static inline fs_lock_shard_t *get_lock_shard(uint32_t mount_id) {
    uint32_t shard_id = select_lock_shard(mount_id);
    return &s_lock_shards[shard_id];
}

/* ========================================================================
 * 文件锁操作
 * ======================================================================== */

/**
 * @brief 查找文件锁（分片内）
 */
static fs_lock_entry_t *find_lock_in_shard(fs_lock_shard_t *shard,
                                            uint32_t mount_id,
                                            uint32_t ino) {
    uint32_t i;
    uint32_t base = shard->index * (FS_MAX_LOCKS / FS_SHARDS_COUNT);

    for (i = 0U; i < (FS_MAX_LOCKS / FS_SHARDS_COUNT); i++) {
        fs_lock_entry_t *lock = &s_lock_shards[shard->index].locks[base + i];
        if (lock->in_use &&
            lock->mount_id == mount_id &&
            lock->ino == ino) {
            return lock;
        }
    }
    return NULL;
}

/**
 * @brief 分配文件锁（分片内）
 */
static fs_lock_entry_t *alloc_lock_in_shard(fs_lock_shard_t *shard) {
    uint32_t i;
    uint32_t base = shard->index * (FS_MAX_LOCKS / FS_SHARDS_COUNT);

    for (i = 0U; i < (FS_MAX_LOCKS / FS_SHARDS_COUNT); i++) {
        fs_lock_entry_t *lock = &s_lock_shards[shard->index].locks[base + i];
        if (!lock->in_use) {
            memset(lock, 0, sizeof(fs_lock_entry_t));
            return lock;
        }
    }
    return NULL;
}

/**
 * @brief 加锁（模拟 fs_flock）
 */
static int32_t fs_flock_lock(uint32_t mount_id, uint32_t ino,
                             uint32_t lock_type, uint32_t owner_tid) {
    fs_lock_entry_t *lock;
    fs_lock_shard_t *shard = get_lock_shard(mount_id);

    /* 获取分片锁 */
    pthread_mutex_lock(&shard->lock);

    if (lock_type == 0x08U) {
        /* 解锁 */
        lock = find_lock_in_shard(shard, mount_id, ino);
        if (lock == NULL || lock->lock.owner_tid != owner_tid) {
            pthread_mutex_unlock(&shard->lock);
            g_stats.unlock_failed++;
            return -1;
        }

        lock->lock.lock_count--;
        if (lock->lock.lock_count == 0U) {
            lock->in_use = false;
        }

        pthread_mutex_unlock(&shard->lock);
        g_stats.unlock_success++;
        return 0;
    }
    else if (lock_type == 0x01U || lock_type == 0x02U) {
        /* 加锁 */
        lock = find_lock_in_shard(shard, mount_id, ino);
        if (lock != NULL) {
            /* 已有锁，检查是否可以嵌套 */
            if (lock->lock.owner_tid == owner_tid) {
                if (lock->lock.lock_type != lock_type) {
                    pthread_mutex_unlock(&shard->lock);
                    g_stats.lock_failed++;
                    return -1;
                }
                lock->lock.lock_count++;
                pthread_mutex_unlock(&shard->lock);
                g_stats.lock_success++;
                return 0;
            }
            else {
                /* 不同线程的锁冲突 */
                pthread_mutex_unlock(&shard->lock);
                g_stats.deadlock_count++;
                return -1;
            }
        }

        /* 分配新锁 */
        lock = alloc_lock_in_shard(shard);
        if (lock == NULL) {
            pthread_mutex_unlock(&shard->lock);
            g_stats.lock_failed++;
            return -1;
        }

        lock->mount_id = mount_id;
        lock->ino = ino;
        lock->lock.locked = true;
        lock->lock.lock_type = lock_type;
        lock->lock.owner_tid = owner_tid;
        lock->lock.lock_count = 1U;
        lock->in_use = true;

        pthread_mutex_unlock(&shard->lock);
        g_stats.lock_success++;
        return 0;
    }

    pthread_mutex_unlock(&shard->lock);
    g_stats.lock_failed++;
    return -1;
}

/* ========================================================================
 * 测试线程
 * ======================================================================== */

/**
 * @brief 压力测试线程
 */
static void *stress_test_thread(void *arg) {
    uint32_t tid = (uint32_t)(uintptr_t)arg;
    uint32_t mount_id = tid % TEST_MOUNTS;
    uint32_t i;

    /* 每个线程使用不同的文件 */
    for (i = 0U; i < TEST_ITERATIONS; i++) {
        uint32_t ino = (tid * TEST_ITERATIONS + i) % FS_MAX_LOCKS;

        /* 随机加锁/解锁 */
        uint32_t lock_type = (i % 3) == 0 ? 0x01U : ((i % 3) == 1 ? 0x02U : 0x08U);

        int32_t ret = fs_flock_lock(mount_id, ino, lock_type, tid);
        if (ret < 0) {
            g_stats.timeout_count++;
        }

        /* 短暂延迟 */
        sleep_ms(1 + (tid % 5));
    }

    return NULL;
}

/* ========================================================================
 * 性能基准测试线程
 * ======================================================================== */

/**
 * @brief 性能基准测试线程
 */
static void *perf_test_thread(void *arg) {
    uint32_t tid = (uint32_t)(uintptr_t)arg;
    uint32_t mount_id = tid % TEST_MOUNTS;
    uint32_t i;

    struct timespec start, end;

    /* 线程ID + 循环次数 = 模拟文件路径 */
    for (i = 0U; i < TEST_ITERATIONS; i++) {
        uint32_t ino = (tid * TEST_ITERATIONS + i) % FS_MAX_LOCKS;

        /* 加锁 */
        clock_gettime(CLOCK_MONOTONIC, &start);
        int32_t ret = fs_flock_lock(mount_id, ino, 0x02U, tid);
        clock_gettime(CLOCK_MONOTONIC, &end);

        if (ret >= 0) {
            g_stats.lock_success++;
        } else {
            g_stats.lock_failed++;
        }

        /* 短暂延迟 */
        sleep_ms(1 + (tid % 2));
    }

    return NULL;
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

/**
 * @brief 初始化分片锁
 */
static int init_shards(void) {
    uint32_t i;

    for (i = 0U; i < FS_SHARDS_COUNT; i++) {
        if (pthread_mutex_init(&s_lock_shards[i].lock, NULL) != 0) {
            fprintf(stderr, "Failed to init mutex for shard %u\n", i);
            return -1;
        }
        s_lock_shards[i].index = i;
        s_lock_shards[i].locks = (fs_lock_entry_t *)calloc(FS_MAX_LOCKS / FS_SHARDS_COUNT,
                                                           sizeof(fs_lock_entry_t));
        s_lock_shards[i].lock_count = 0U;
    }

    return 0;
}

/**
 * @brief 清理分片锁
 */
static void cleanup_shards(void) {
    uint32_t i;

    for (i = 0U; i < FS_SHARDS_COUNT; i++) {
        pthread_mutex_destroy(&s_lock_shards[i].lock);
        free(s_lock_shards[i].locks);
    }
}

/**
 * @brief 运行压力测试
 */
static int run_stress_test(void) {
    pthread_t threads[TEST_THREADS];
    uint32_t i;
    struct timespec start, end;

    printf("\n========== 压力测试 ==========\n");
    printf("线程数: %d\n", TEST_THREADS);
    printf("迭代次数: %d\n", TEST_ITERATIONS);
    printf("挂载点数: %d\n", TEST_MOUNTS);
    printf("开始时间: %ld.%09ld\n",
           start.tv_sec, start.tv_nsec);

    clock_gettime(CLOCK_MONOTONIC, &start);

    /* 创建测试线程 */
    for (i = 0U; i < TEST_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, stress_test_thread,
                           (void *)(uintptr_t)i) != 0) {
            fprintf(stderr, "Failed to create thread %u\n", i);
            return -1;
        }
    }

    /* 等待所有线程完成 */
    for (i = 0U; i < TEST_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    printf("结束时间: %ld.%09ld\n",
           end.tv_sec, end.tv_nsec);

    printf("总耗时: %.3f 秒\n",
           (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9);

    printf("\n========== 测试统计 ==========\n");
    printf("加锁成功: %lu\n", g_stats.lock_success);
    printf("加锁失败: %lu\n", g_stats.lock_failed);
    printf("解锁成功: %lu\n", g_stats.unlock_success);
    printf("解锁失败: %lu\n", g_stats.unlock_failed);
    printf("死锁次数: %lu\n", g_stats.deadlock_count);
    printf("超时次数: %lu\n", g_stats.timeout_count);

    return 0;
}

/**
 * @brief 运行性能基准测试
 */
static int run_perf_test(void) {
    pthread_t threads[TEST_THREADS];
    uint32_t i;
    struct timespec start, end;
    uint64_t total_ops = TEST_THREADS * TEST_ITERATIONS;
    uint64_t lock_ops = g_stats.lock_success;
    uint64_t unlock_ops = g_stats.unlock_success;

    printf("\n========== 性能基准测试 ==========\n");
    printf("线程数: %d\n", TEST_THREADS);
    printf("迭代次数: %d\n", TEST_ITERATIONS);
    printf("总操作数: %lu\n", total_ops);
    printf("加锁操作: %lu\n", lock_ops);
    printf("解锁操作: %lu\n", unlock_ops);

    /* 重置统计信息 */
    memset(&g_stats, 0, sizeof(g_stats));

    clock_gettime(CLOCK_MONOTONIC, &start);

    /* 创建测试线程 */
    for (i = 0U; i < TEST_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, perf_test_thread,
                           (void *)(uintptr_t)i) != 0) {
            fprintf(stderr, "Failed to create thread %u\n", i);
            return -1;
        }
    }

    /* 等待所有线程完成 */
    for (i = 0U; i < TEST_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_sec = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("\n========== 性能指标 ==========\n");
    printf("总耗时: %.3f 秒\n", elapsed_sec);

    /* 计算锁操作平均时间 */
    if (lock_ops > 0) {
        double lock_time_ms = (elapsed_sec * 1e6) / lock_ops;
        printf("加锁平均时间: %.6f 微秒\n", lock_time_ms);
    }

    if (unlock_ops > 0) {
        double unlock_time_ms = (elapsed_sec * 1e6) / unlock_ops;
        printf("解锁平均时间: %.6f 微秒\n", unlock_time_ms);
    }

    /* 计算吞吐量 */
    double throughput = total_ops / elapsed_sec;
    printf("总吞吐量: %.2f ops/秒\n", throughput);
    printf("加锁吞吐量: %.2f ops/秒\n", (double)lock_ops / elapsed_sec);
    printf("解锁吞吐量: %.2f ops/秒\n", (double)unlock_ops / elapsed_sec);

    /* 计算平均锁粒度 */
    if (total_ops > 0) {
        double avg_lock_ms = (elapsed_sec * 1e6) / total_ops;
        printf("平均锁操作时间: %.6f 微秒\n", avg_lock_ms);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    int ret;

    printf("========================================\n");
    printf("文件锁分片锁测试\n");
    printf("========================================\n");

    /* 初始化 */
    ret = init_shards();
    if (ret < 0) {
        fprintf(stderr, "Failed to initialize shards\n");
        return 1;
    }

    /* 运行压力测试 */
    ret = run_stress_test();
    if (ret < 0) {
        fprintf(stderr, "Stress test failed\n");
        cleanup_shards();
        return 1;
    }

    /* 运行性能基准测试 */
    ret = run_perf_test();
    if (ret < 0) {
        fprintf(stderr, "Perf test failed\n");
        cleanup_shards();
        return 1;
    }

    /* 清理 */
    cleanup_shards();

    printf("\n========================================\n");
    printf("所有测试完成！\n");
    printf("========================================\n");

    return 0;
}
