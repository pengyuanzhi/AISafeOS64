/**
 * @file    fs_lock_test_no_free.c
 * @brief   文件锁分片锁测试（不手动 free）
 * @author  AISafe64 Team
 * @date    2026-05-09
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
#define TEST_THREADS         4
#define TEST_ITERATIONS      100

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
    uint32_t index;
} fs_lock_shard_t;

static fs_lock_shard_t s_lock_shards[FS_SHARDS_COUNT];
static fs_lock_entry_t *s_shard_locks[FS_SHARDS_COUNT];

static inline uint32_t select_lock_shard(uint32_t mount_id) {
    return mount_id & (FS_SHARDS_COUNT - 1);
}

static inline fs_lock_shard_t *get_lock_shard(uint32_t mount_id) {
    uint32_t shard_id = select_lock_shard(mount_id);
    return &s_lock_shards[shard_id];
}

static int32_t init_shards(void) {
    uint32_t i;
    uint32_t entries_per_shard = FS_MAX_LOCKS / FS_SHARDS_COUNT;

    printf("初始化分片锁（%d 分片）...\n", FS_SHARDS_COUNT);

    for (i = 0U; i < FS_SHARDS_COUNT; i++) {
        pthread_mutex_init(&s_lock_shards[i].lock, NULL);
        s_lock_shards[i].index = i;
        s_shard_locks[i] = (fs_lock_entry_t *)calloc(entries_per_shard,
                                                           sizeof(fs_lock_entry_t));
        s_lock_shards[i].locks = s_shard_locks[i];
    }

    return 0;
}

static int32_t test_lock_operation(uint32_t mount_id, uint32_t ino,
                                 uint32_t lock_type, uint32_t owner_tid) {
    uint32_t i;
    fs_lock_entry_t *lock;
    fs_lock_shard_t *shard = get_lock_shard(mount_id);
    uint32_t base = shard->index * (FS_MAX_LOCKS / FS_SHARDS_COUNT);

    pthread_mutex_lock(&shard->lock);

    if (lock_type == 0x08U) {
        for (i = 0U; i < (FS_MAX_LOCKS / FS_SHARDS_COUNT); i++) {
            lock = &shard->locks[base + i];
            if (lock->in_use && lock->mount_id == mount_id &&
                lock->ino == ino) {
                if (lock->lock.owner_tid == owner_tid) {
                    lock->lock.lock_count--;
                    if (lock->lock.lock_count == 0U) {
                        lock->in_use = false;
                    }
                    pthread_mutex_unlock(&shard->lock);
                    return 0;
                }
            }
        }
        pthread_mutex_unlock(&shard->lock);
        return -1;
    }
    else if (lock_type == 0x01U || lock_type == 0x02U) {
        for (i = 0U; i < (FS_MAX_LOCKS / FS_SHARDS_COUNT); i++) {
            lock = &shard->locks[base + i];
            if (lock->in_use && lock->mount_id == mount_id &&
                lock->ino == ino) {
                if (lock->lock.owner_tid == owner_tid) {
                    if (lock->lock.lock_type == lock_type) {
                        lock->lock.lock_count++;
                        pthread_mutex_unlock(&shard->lock);
                        return 0;
                    }
                }
                pthread_mutex_unlock(&shard->lock);
                return -1;
            }
        }
        for (i = 0U; i < (FS_MAX_LOCKS / FS_SHARDS_COUNT); i++) {
            lock = &shard->locks[base + i];
            if (!lock->in_use) {
                memset(lock, 0, sizeof(fs_lock_entry_t));
                lock->mount_id = mount_id;
                lock->ino = ino;
                lock->lock.locked = true;
                lock->lock.lock_type = lock_type;
                lock->lock.owner_tid = owner_tid;
                lock->lock.lock_count = 1U;
                lock->in_use = true;
                pthread_mutex_unlock(&shard->lock);
                return 0;
            }
        }
        pthread_mutex_unlock(&shard->lock);
        return -1;
    }

    pthread_mutex_unlock(&shard->lock);
    return -1;
}

static void *test_thread(void *arg) {
    uint32_t tid = (uint32_t)(uintptr_t)arg;
    uint32_t mount_id = tid % 8;
    uint32_t i;

    for (i = 0U; i < TEST_ITERATIONS; i++) {
        uint32_t ino = (tid * TEST_ITERATIONS + i) % FS_MAX_LOCKS;
        uint32_t lock_type = (i % 3) == 0 ? 0x01U : ((i % 3) == 1 ? 0x02U : 0x08U);
        test_lock_operation(mount_id, ino, lock_type, tid);
        usleep(1000);
    }

    return NULL;
}

int main(void) {
    pthread_t threads[TEST_THREADS];
    uint32_t i;
    struct timespec start, end;

    printf("========================================\n");
    printf("文件锁分片锁测试（不手动 free）\n");
    printf("========================================\n");
    printf("FS_MAX_LOCKS: %d, FS_SHARDS_COUNT: %d\n",
           FS_MAX_LOCKS, FS_SHARDS_COUNT);

    init_shards();

    printf("开始测试（%d 线程 × %d 迭代）...\n",
           TEST_THREADS, TEST_ITERATIONS);

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (i = 0U; i < TEST_THREADS; i++) {
        pthread_create(&threads[i], NULL, test_thread, (void *)(uintptr_t)i);
    }

    for (i = 0U; i < TEST_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_sec = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    uint64_t total_ops = TEST_THREADS * TEST_ITERATIONS;

    printf("总耗时: %.3f 秒\n", elapsed_sec);
    printf("总操作数: %lu\n", total_ops);
    printf("吞吐量: %.2f ops/秒\n", total_ops / elapsed_sec);

    printf("\n========================================\n");
    printf("测试完成！\n");
    printf("========================================\n");

    return 0;
}
