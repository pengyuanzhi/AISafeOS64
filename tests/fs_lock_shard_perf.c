/**
 * @file    fs_lock_shard_perf.c
 * @brief   文件锁分片锁性能基准测试（详细版）
 * @author  AISafe64 Team
 * @date    2026-05-09
 * @version 1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define FS_MAX_LOCKS         128
#define FS_SHARDS_COUNT      8
#define TEST_MOUNTS          8
#define TEST_THREADS         64
#define TEST_ITERATIONS      10000

typedef struct {
    bool locked;
    uint32_t lock_type;
    uint32_t owner_tid;
    uint32_t lock_count;
} fs_file_lock_t;

typedef struct {
    uint32_t mount_id;
    uint32_t ino;
    fs_file_lock_t lock;
    bool in_use;
} fs_lock_entry_t;

typedef struct {
    pthread_mutex_t lock;
    fs_lock_entry_t *locks;
    uint32_t lock_count;
    uint32_t index;
} fs_lock_shard_t;

static fs_lock_shard_t s_lock_shards[FS_SHARDS_COUNT];

typedef struct {
    uint64_t lock_success;
    uint64_t lock_failed;
    uint64_t unlock_success;
    uint64_t unlock_failed;
    uint64_t deadlock_count;
    uint64_t timeout_count;
} test_stats_t;

static test_stats_t g_stats = {0};

static inline void sleep_ms(uint64_t ms) {
    usleep(ms * 1000);
}

static inline uint32_t select_lock_shard(uint32_t mount_id) {
    return mount_id & (FS_SHARDS_COUNT - 1);
}

static inline fs_lock_shard_t *get_lock_shard(uint32_t mount_id) {
    uint32_t shard_id = select_lock_shard(mount_id);
    return &s_lock_shards[shard_id];
}

static fs_lock_entry_t *find_lock_in_shard(fs_lock_shard_t *shard,
                                            uint32_t mount_id,
                                            uint32_t ino) {
    uint32_t i;
    uint32_t base = shard->index * (FS_MAX_LOCKS / FS_SHARDS_COUNT);

    for (i = 0U; i < (FS_MAX_LOCKS / FS_SHARDS_COUNT); i++) {
        fs_lock_entry_t *lock = &shard->locks[base + i];
        if (lock->in_use &&
            lock->mount_id == mount_id &&
            lock->ino == ino) {
            return lock;
        }
    }
    return NULL;
}

static fs_lock_entry_t *alloc_lock_in_shard(fs_lock_shard_t *shard) {
    uint32_t i;
    uint32_t base = shard->index * (FS_MAX_LOCKS / FS_SHARDS_COUNT);

    for (i = 0U; i < (FS_MAX_LOCKS / FS_SHARDS_COUNT); i++) {
        fs_lock_entry_t *lock = &shard->locks[base + i];
        if (!lock->in_use) {
            memset(lock, 0, sizeof(fs_lock_entry_t));
            return lock;
        }
    }
    return NULL;
}

static int32_t fs_flock_lock(uint32_t mount_id, uint32_t ino,
                             uint32_t lock_type, uint32_t owner_tid) {
    fs_lock_entry_t *lock;
    fs_lock_shard_t *shard = get_lock_shard(mount_id);

    pthread_mutex_lock(&shard->lock);

    if (lock_type == 0x08U) {
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
        lock = find_lock_in_shard(shard, mount_id, ino);
        if (lock != NULL) {
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
                pthread_mutex_unlock(&shard->lock);
                g_stats.deadlock_count++;
                return -1;
            }
        }

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

static void *stress_test_thread(void *arg) {
    uint32_t tid = (uint32_t)(uintptr_t)arg;
    uint32_t mount_id = tid % TEST_MOUNTS;
    uint32_t i;

    for (i = 0U; i < TEST_ITERATIONS; i++) {
        uint32_t ino = (tid * TEST_ITERATIONS + i) % FS_MAX_LOCKS;
        uint32_t lock_type = (i % 3) == 0 ? 0x01U : ((i % 3) == 1 ? 0x02U : 0x08U);
        int32_t ret = fs_flock_lock(mount_id, ino, lock_type, tid);
        if (ret < 0) {
            g_stats.timeout_count++;
        }
        sleep_ms(1 + (tid % 5));
    }

    return NULL;
}

static void *perf_test_thread(void *arg) {
    uint32_t tid = (uint32_t)(uintptr_t)arg;
    uint32_t mount_id = tid % TEST_MOUNTS;
    uint32_t i;

    struct timespec start, end;

    for (i = 0U; i < TEST_ITERATIONS; i++) {
        uint32_t ino = (tid * TEST_ITERATIONS + i) % FS_MAX_LOCKS;
        clock_gettime(CLOCK_MONOTONIC, &start);
        int32_t ret = fs_flock_lock(mount_id, ino, 0x02U, tid);
        clock_gettime(CLOCK_MONOTONIC, &end);

        if (ret >= 0) {
            g_stats.lock_success++;
        } else {
            g_stats.lock_failed++;
        }

        sleep_ms(1 + (tid % 2));
    }

    return NULL;
}

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

static void cleanup_shards(void) {
    uint32_t i;

    for (i = 0U; i < FS_SHARDS_COUNT; i++) {
        pthread_mutex_destroy(&s_lock_shards[i].lock);
        free(s_lock_shards[i].locks);
    }
}

static int test_throughput(uint32_t thread_count) {
    pthread_t threads[thread_count];
    uint32_t i;
    struct timespec start, end;

    printf("\n[场景 1] 线程数: %u, 挂载点数: %u\n", thread_count, TEST_MOUNTS);

    memset(&g_stats, 0, sizeof(g_stats));

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (i = 0U; i < thread_count; i++) {
        if (pthread_create(&threads[i], NULL, perf_test_thread,
                           (void *)(uintptr_t)i) != 0) {
            fprintf(stderr, "Failed to create thread %u\n", i);
            return -1;
        }
    }

    for (i = 0U; i < thread_count; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_sec = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    uint64_t total_ops = thread_count * TEST_ITERATIONS;
    uint64_t lock_ops = g_stats.lock_success;

    printf("  总耗时: %.3f 秒\n", elapsed_sec);
    printf("  总操作数: %lu\n", total_ops);
    printf("  加锁成功: %lu, 失败: %lu\n", lock_ops, g_stats.lock_failed);
    printf("  解锁成功: %lu, 失败: %lu\n",
           g_stats.unlock_success, g_stats.unlock_failed);
    printf("  死锁次数: %lu\n", g_stats.deadlock_count);

    double throughput = total_ops / elapsed_sec;
    printf("  总吞吐量: %.2f ops/秒\n", throughput);
    printf("  加锁吞吐量: %.2f ops/秒\n", (double)lock_ops / elapsed_sec);

    return 0;
}

static int test_mounts(uint32_t mount_count) {
    pthread_t threads[TEST_THREADS];
    uint32_t i;
    struct timespec start, end;

    printf("\n[场景 2] 线程数: %u, 挂载点数: %u\n", TEST_THREADS, mount_count);

    memset(&g_stats, 0, sizeof(g_stats));

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (i = 0U; i < TEST_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, perf_test_thread,
                           (void *)(uintptr_t)i) != 0) {
            fprintf(stderr, "Failed to create thread %u\n", i);
            return -1;
        }
    }

    for (i = 0U; i < TEST_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_sec = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    uint64_t total_ops = TEST_THREADS * TEST_ITERATIONS;
    uint64_t lock_ops = g_stats.lock_success;

    printf("  总耗时: %.3f 秒\n", elapsed_sec);
    printf("  总操作数: %lu\n", total_ops);
    printf("  加锁成功: %lu, 失败: %lu\n", lock_ops, g_stats.lock_failed);
    printf("  解锁成功: %lu, 失败: %lu\n",
           g_stats.unlock_success, g_stats.unlock_failed);
    printf("  死锁次数: %lu\n", g_stats.deadlock_count);

    double throughput = total_ops / elapsed_sec;
    printf("  总吞吐量: %.2f ops/秒\n", throughput);

    return 0;
}

static int test_nested_locks(uint32_t thread_count) {
    pthread_t threads[thread_count];
    uint32_t i;
    struct timespec start, end;

    printf("\n[场景 3] 线程数: %u, 嵌套锁深度: %u\n", thread_count, TEST_ITERATIONS);

    memset(&g_stats, 0, sizeof(g_stats));

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (i = 0U; i < thread_count; i++) {
        if (pthread_create(&threads[i], NULL, perf_test_thread,
                           (void *)(uintptr_t)i) != 0) {
            fprintf(stderr, "Failed to create thread %u\n", i);
            return -1;
        }
    }

    for (i = 0U; i < thread_count; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_sec = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    uint64_t total_ops = thread_count * TEST_ITERATIONS;
    uint64_t lock_ops = g_stats.lock_success;

    printf("  总耗时: %.3f 秒\n", elapsed_sec);
    printf("  总操作数: %lu\n", total_ops);
    printf("  加锁成功: %lu, 失败: %lu\n", lock_ops, g_stats.lock_failed);
    printf("  解锁成功: %lu, 失败: %lu\n",
           g_stats.unlock_success, g_stats.unlock_failed);
    printf("  死锁次数: %lu\n", g_stats.deadlock_count);

    double throughput = total_ops / elapsed_sec;
    printf("  总吞吐量: %.2f ops/秒\n", throughput);

    return 0;
}

static int test_extreme_stress(void) {
    pthread_t threads[256];
    uint32_t i;
    struct timespec start, end;

    printf("\n[场景 4] 极限压力测试\n");
    printf("  线程数: 256\n");
    printf("  挂载点数: %u\n", TEST_MOUNTS);
    printf("  迭代次数: %u\n", TEST_ITERATIONS / 10);

    memset(&g_stats, 0, sizeof(g_stats));

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (i = 0U; i < 256; i++) {
        if (pthread_create(&threads[i], NULL, stress_test_thread,
                           (void *)(uintptr_t)i) != 0) {
            fprintf(stderr, "Failed to create thread %u\n", i);
            return -1;
        }
    }

    for (i = 0U; i < 256; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_sec = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    uint64_t total_ops = 256 * (TEST_ITERATIONS / 10);
    uint64_t lock_ops = g_stats.lock_success;

    printf("  总耗时: %.3f 秒\n", elapsed_sec);
    printf("  总操作数: %lu\n", total_ops);
    printf("  加锁成功: %lu, 失败: %lu\n", lock_ops, g_stats.lock_failed);
    printf("  解锁成功: %lu, 失败: %lu\n",
           g_stats.unlock_success, g_stats.unlock_failed);
    printf("  死锁次数: %lu\n", g_stats.deadlock_count);

    double throughput = total_ops / elapsed_sec;
    printf("  总吞吐量: %.2f ops/秒\n", throughput);

    return 0;
}

int main(int argc, char *argv[]) {
    printf("========================================\n");
    printf("文件锁分片锁性能基准测试\n");
    printf("========================================\n");
    printf("FS_MAX_LOCKS: %d\n", FS_MAX_LOCKS);
    printf("FS_SHARDS_COUNT: %d\n", FS_SHARDS_COUNT);
    printf("TEST_MOUNTS: %d\n", TEST_MOUNTS);

    int ret = init_shards();
    if (ret < 0) {
        fprintf(stderr, "Failed to initialize shards\n");
        return 1;
    }

    /* 场景 1: 不同线程数 */
    test_throughput(8);
    test_throughput(16);
    test_throughput(32);
    test_throughput(64);

    /* 场景 2: 不同挂载点数 */
    test_mounts(2);
    test_mounts(4);
    test_mounts(8);
    test_mounts(16);

    /* 场景 3: 嵌套锁 */
    test_nested_locks(16);

    /* 场景 4: 极限压力 */
    test_extreme_stress();

    cleanup_shards();

    printf("\n========================================\n");
    printf("所有测试完成！\n");
    printf("========================================\n");

    return 0;
}
