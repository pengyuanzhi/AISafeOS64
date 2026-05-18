/**
 * @file    fs_lock_test_basic.c
 * @brief   文件锁分片锁基础测试
 * @author  AISafe64 Team
 * @date    2026-05-09
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>

#define FS_MAX_LOCKS         128
#define FS_SHARDS_COUNT      8
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
static fs_lock_entry_t *s_shard_locks[FS_SHARDS_COUNT];

static uint64_t g_ops = 0;

static inline uint32_t select_lock_shard(uint32_t mount_id) {
    return mount_id & (FS_SHARDS_COUNT - 1);
}

static int32_t init_shards(void) {
    uint32_t i;
    uint32_t entries_per_shard = FS_MAX_LOCKS / FS_SHARDS_COUNT;

    printf("初始化分片锁（%d 分片）...\n", FS_SHARDS_COUNT);
    printf("每分片锁表大小: %u 字节\n", (uint32_t)sizeof(fs_lock_entry_t));
    printf("总分配: %u 字节\n", (uint32_t)(entries_per_shard * sizeof(fs_lock_entry_t) * FS_SHARDS_COUNT));

    for (i = 0U; i < FS_SHARDS_COUNT; i++) {
        pthread_mutex_init(&s_lock_shards[i].lock, NULL);
        s_lock_shards[i].index = i;
        s_shard_locks[i] = (fs_lock_entry_t *)calloc(entries_per_shard,
                                                           sizeof(fs_lock_entry_t));
        s_lock_shards[i].locks = s_shard_locks[i];
        printf("分片 %u: 分配 %u 个锁项，指针 %p\n",
               i, entries_per_shard, s_shard_locks[i]);
    }

    return 0;
}

static int32_t test_basic_lock(uint32_t mount_id, uint32_t ino) {
    uint32_t shard_id = select_lock_shard(mount_id);
    uint32_t base = s_lock_shards[shard_id].index * (FS_MAX_LOCKS / FS_SHARDS_COUNT);
    uint32_t lock_idx = base + (ino % (FS_MAX_LOCKS / FS_SHARDS_COUNT));

    pthread_mutex_lock(&s_lock_shards[shard_id].lock);

    fs_lock_entry_t *lock = &s_lock_shards[shard_id].locks[lock_idx];

    lock->mount_id = mount_id;
    lock->ino = ino;
    lock->lock.locked = true;
    lock->lock.lock_type = 1;
    lock->lock.owner_tid = 999;
    lock->lock.lock_count = 1;
    lock->in_use = true;

    pthread_mutex_unlock(&s_lock_shards[shard_id].lock);

    g_ops++;

    return 0;
}

static void test_single_thread(void) {
    uint32_t i;

    printf("开始单线程测试（%d 迭代）...\n", TEST_ITERATIONS);

    for (i = 0U; i < TEST_ITERATIONS; i++) {
        uint32_t mount_id = i % 8;
        uint32_t ino = i % FS_MAX_LOCKS;
        test_basic_lock(mount_id, ino);
    }

    printf("单线程测试完成！总操作数: %lu\n", g_ops);
}

int main(void) {
    struct timespec start, end;

    printf("========================================\n");
    printf("文件锁分片锁基础测试\n");
    printf("========================================\n");

    init_shards();

    clock_gettime(CLOCK_MONOTONIC, &start);

    test_single_thread();

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_sec = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("\n总耗时: %.3f 秒\n", elapsed_sec);
    printf("总操作数: %lu\n", g_ops);
    printf("吞吐量: %.2f ops/秒\n", (double)g_ops / elapsed_sec);

    printf("\n========================================\n");
    printf("测试完成！\n");
    printf("========================================\n");

    return 0;
}
