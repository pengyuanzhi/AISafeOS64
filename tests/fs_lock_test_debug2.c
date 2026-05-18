#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

#define FS_MAX_LOCKS         128
#define FS_SHARDS_COUNT      8
#define TEST_THREADS         4
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

static int32_t select_shard(uint32_t mount_id) {
    return mount_id & (FS_SHARDS_COUNT - 1);
}

static int32_t init_shards(void) {
    uint32_t i;
    fs_lock_entry_t *shard_locks[FS_SHARDS_COUNT];
    uint32_t entries_per_shard = FS_MAX_LOCKS / FS_SHARDS_COUNT;

    printf("初始化分片锁...\n");

    for (i = 0U; i < FS_SHARDS_COUNT; i++) {
        printf("分片 %u: 分配 %u 个锁项\n", i, entries_per_shard);
        pthread_mutex_init(&s_lock_shards[i].lock, NULL);
        s_lock_shards[i].index = i;
        shard_locks[i] = (fs_lock_entry_t *)calloc(entries_per_shard, sizeof(fs_lock_entry_t));
        s_lock_shards[i].locks = shard_locks[i];
        printf("分片 %u 锁表指针: %p\n", i, shard_locks[i]);
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
    uint32_t shard_id;
    uint32_t base;
    uint32_t entries_per_shard = FS_MAX_LOCKS / FS_SHARDS_COUNT;

    printf("线程 %u 开始 (mount_id=%u)...\n", thread_id, mount_id);

    for (i = 0U; i < TEST_ITERATIONS; i++) {
        uint32_t ino = (thread_id * TEST_ITERATIONS + i) % FS_MAX_LOCKS;
        shard_id = select_shard(mount_id);
        base = s_lock_shards[shard_id].index * entries_per_shard;
        uint32_t lock_idx = base + (ino % entries_per_shard);

        printf("线程 %u: shard_id=%u, ino=%u, base=%u, lock_idx=%u, lock=%p\n",
               thread_id, shard_id, ino, base, lock_idx,
               &s_lock_shards[shard_id].locks[lock_idx]);

        pthread_mutex_lock(&s_lock_shards[shard_id].lock);

        fs_lock_entry_t *lock = &s_lock_shards[shard_id].locks[lock_idx];
        lock->mount_id = mount_id;
        lock->ino = ino;
        lock->in_use = 1;

        pthread_mutex_unlock(&s_lock_shards[shard_id].lock);
    }

    printf("线程 %u 完成\n", thread_id);
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

    printf("初始化分片锁（%d 分片）...\n", FS_SHARDS_COUNT);
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
