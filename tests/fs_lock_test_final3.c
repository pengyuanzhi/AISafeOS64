/**
 * @file    fs_lock_test_final3.c
 * @brief   文件锁分片锁最终测试（简化版）
 * @author  AISafe64 Team
 * @date    2026-05-09
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

#define FS_MAX_LOCKS         128
#define FS_SHARDS_COUNT      8
#define TEST_THREADS         16
#define TEST_ITERATIONS      1000

typedef struct {
    int dummy;
} fs_file_lock_t;

typedef struct {
    uint32_t mount_id;
    uint32_t ino;
    fs_file_lock_t lock;
    int in_use;
} fs_lock_entry_t;

typedef struct {
    pthread_mutex_t lock;
    fs_lock_entry_t *locks;
    uint32_t index;
} fs_lock_shard_t;

static fs_lock_shard_t s_lock_shards[FS_SHARDS_COUNT];
static fs_lock_entry_t *shard_locks[FS_SHARDS_COUNT];

static int32_t init_shards(void) {
    uint32_t i;
    uint32_t entries_per_shard = FS_MAX_LOCKS / FS_SHARDS_COUNT;

    for (i = 0U; i < FS_SHARDS_COUNT; i++) {
        pthread_mutex_init(&s_lock_shards[i].lock, NULL);
        s_lock_shards[i].index = i;
        shard_locks[i] = (fs_lock_entry_t *)calloc(entries_per_shard, sizeof(fs_lock_entry_t));
        s_lock_shards[i].locks = shard_locks[i];
    }

    return 0;
}

static void cleanup_shards(void) {
    uint32_t i;

    for (i = 0U; i < FS_SHARDS_COUNT; i++) {
        pthread_mutex_destroy(&s_lock_shards[i].lock);
        free(shard_locks[i]);
    }
}

static int32_t test_shard(uint32_t thread_id) {
    uint32_t mount_id = thread_id % 8;
    uint32_t shard_id = mount_id & 7;
    uint32_t base = s_lock_shards[shard_id].index * 16;
    fs_lock_entry_t *shard_locks_ptr = s_lock_shards[shard_id].locks;
    uint32_t entries_per_shard = 16;
    uint32_t i;

    for (i = 0U; i < TEST_ITERATIONS; i++) {
        uint32_t ino = (thread_id * TEST_ITERATIONS + i) % FS_MAX_LOCKS;
        uint32_t lock_idx = base + (ino % entries_per_shard);

        pthread_mutex_lock(&s_lock_shards[shard_id].lock);

        fs_lock_entry_t *lock = &shard_locks_ptr[lock_idx];
        lock->mount_id = mount_id;
        lock->ino = ino;
        lock->in_use = 1;

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

    init_shards();

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
