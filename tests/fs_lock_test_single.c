/**
 * @file    fs_lock_test_single.c
 * @brief   单线程测试分片锁
 * @author  AISafe64 Team
 * @date    2026-05-09
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

#define FS_MAX_LOCKS         128
#define FS_SHARDS_COUNT      8
#define TEST_ITERATIONS      100

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

static int32_t init_shards(void) {
    uint32_t i;
    fs_lock_entry_t *shard_locks[FS_SHARDS_COUNT];

    for (i = 0U; i < FS_SHARDS_COUNT; i++) {
        pthread_mutex_init(&s_lock_shards[i].lock, NULL);
        s_lock_shards[i].index = i;
        shard_locks[i] = (fs_lock_entry_t *)calloc(16, sizeof(fs_lock_entry_t));
        s_lock_shards[i].locks = shard_locks[i];
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

static int32_t test_shard_single(void) {
    uint32_t shard_id = 0;
    uint32_t base = 0;
    uint32_t entries_per_shard = 16;
    uint32_t i;

    for (i = 0U; i < TEST_ITERATIONS; i++) {
        uint32_t ino = i % FS_MAX_LOCKS;
        uint32_t lock_idx = base + (ino % entries_per_shard);

        pthread_mutex_lock(&s_lock_shards[shard_id].lock);

        fs_lock_entry_t *lock = &s_lock_shards[shard_id].locks[lock_idx];
        lock->mount_id = shard_id;
        lock->ino = ino;
        lock->in_use = 1;

        pthread_mutex_unlock(&s_lock_shards[shard_id].lock);
    }

    return 0;
}

int main(void) {
    printf("初始化分片锁（%d 分片）...\n", FS_SHARDS_COUNT);
    init_shards();

    printf("开始单线程测试（%d 迭代）...\n", TEST_ITERATIONS);

    test_shard_single();

    printf("单线程测试完成！\n");
    cleanup_shards();

    return 0;
}
