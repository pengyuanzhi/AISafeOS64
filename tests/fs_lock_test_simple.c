/**
 * @file    fs_lock_test_simple.c
 * @brief   文件锁分片锁简化测试
 * @author  AISafe64 Team
 * @date    2026-05-09
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#define FS_MAX_LOCKS         128
#define FS_SHARDS_COUNT      8
#define TEST_THREADS         16
#define TEST_ITERATIONS      1000

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

static int32_t select_shard(uint32_t mount_id) {
    return mount_id & (FS_SHARDS_COUNT - 1);
}

static int32_t init_shards(void) {
    uint32_t i;

    for (i = 0U; i < FS_SHARDS_COUNT; i++) {
        pthread_mutex_init(&s_lock_shards[i].lock, NULL);
        s_lock_shards[i].index = i;
        s_lock_shards[i].locks = (fs_lock_entry_t *)calloc(FS_MAX_LOCKS / FS_SHARDS_COUNT,
                                                           sizeof(fs_lock_entry_t));
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

static int32_t test_shard(uint32_t thread_id) {
    uint32_t mount_id = thread_id % 8;
    uint32_t i;

    for (i = 0U; i < TEST_ITERATIONS; i++) {
        uint32_t ino = (thread_id * TEST_ITERATIONS + i) % FS_MAX_LOCKS;
        uint32_t shard_id = select_shard(mount_id);
        uint32_t base = s_lock_shards[shard_id].index * (FS_MAX_LOCKS / FS_SHARDS_COUNT);

        pthread_mutex_lock(&s_lock_shards[shard_id].lock);

        fs_lock_entry_t *lock = &s_lock_shards[shard_id].locks[base + ino];
        lock->lock.locked = true;
        lock->lock.lock_type = 2;
        lock->lock.owner_tid = thread_id;
        lock->lock.lock_count = 1;
        lock->in_use = true;

        pthread_mutex_unlock(&s_lock_shards[shard_id].lock);
    }

    return 0;
}

static void *test_thread(void *arg) {
    uint32_t tid = (uint32_t)(uintptr_t)arg;
    test_shard(tid);
    return NULL;
}

int main(void) {
    pthread_t threads[TEST_THREADS];
    uint32_t i;

    printf("初始化分片锁...\n");
    init_shards();

    printf("开始测试（%d 线程 × %d 迭代）...\n", TEST_THREADS, TEST_ITERATIONS);

    for (i = 0U; i < TEST_THREADS; i++) {
        pthread_create(&threads[i], NULL, test_thread, (void *)(uintptr_t)i);
    }

    for (i = 0U; i < TEST_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("测试完成！\n");
    cleanup_shards();

    return 0;
}
