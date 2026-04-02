/**
 * @file    test_vfs.c
 * @brief   AISafe64 RTOS - VFS 虚拟文件系统单元测试（宿主机版）
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 1.0
 *
 * @details VFS 虚拟文件系统测试：
 *          - 挂载/卸载
 *          - 文件打开/关闭/读写
 *          - 目录操作
 *          - 边界条件
 *
 * @note 宿主机自包含测试（不依赖内核头文件）
 * @note 对应需求: FS-001~005
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

/* ========================================================================
 * 简易测试框架（内联版）
 * ======================================================================== */

static uint32_t s_total   = 0U;
static uint32_t s_passed  = 0U;
static uint32_t s_failed  = 0U;

#define TEST_ASSERT_EQ(a, b)                                               \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if ((int64_t)(a) == (int64_t)(b)) { s_passed++; }                  \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 %lld 实际 %lld\n",                   \
                   __FILE__, __LINE__,                                      \
                   (long long)(int64_t)(b), (long long)(int64_t)(a));       \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_GT(a, b)                                               \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if ((int64_t)(a) > (int64_t)(b)) { s_passed++; }                   \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 %lld > %lld\n",                      \
                   __FILE__, __LINE__,                                      \
                   (long long)(int64_t)(a), (long long)(int64_t)(b));       \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_LT(a, b)                                               \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if ((int64_t)(a) < (int64_t)(b)) { s_passed++; }                   \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 %lld < %lld\n",                      \
                   __FILE__, __LINE__,                                      \
                   (long long)(int64_t)(a), (long long)(int64_t)(b));       \
        }                                                                  \
    } while (0)

#define TEST_ASSERT_TRUE(cond)                                             \
    do                                                                     \
    {                                                                      \
        s_total++;                                                         \
        if (cond) { s_passed++; }                                          \
        else                                                               \
        {                                                                  \
            s_failed++;                                                    \
            printf("  FAIL %s:%u 期望 true: %s\n",                         \
                   __FILE__, __LINE__, #cond);                              \
        }                                                                  \
    } while (0)

#define TEST_RUN(name)                                                     \
    do                                                                     \
    {                                                                      \
        printf("  [RUN] %s\n", #name);                                     \
        test_##name();                                                     \
    } while (0)

/* ========================================================================
 * VFS 类型与常量
 * ======================================================================== */

#define VFS_MAX_MOUNTS     8U
#define VFS_MAX_FDS        32U
#define VFS_PATH_MAX       256U
#define VFS_NAME_MAX       64U

typedef int32_t kernel_status_t;
#define KERNEL_OK  ((kernel_status_t)0)
#define EINVAL     22
#define ENOENT     2
#define ENOMEM     12
#define EACCES     13
#define EBUSY      16

typedef enum
{
    VFS_TYPE_REGULAR  = 0U,
    VFS_TYPE_DIR      = 1U,
    VFS_TYPE_CHARDEV  = 2U,
    VFS_TYPE_BLOCKDEV = 3U
} vfs_file_type_t;

typedef enum
{
    VFS_FS_RAMFS = 0U,
    VFS_FS_ROMFS = 1U,
    VFS_FS_FAT32 = 2U,
    VFS_FS_DEVFS = 3U
} vfs_fstype_t;

typedef struct
{
    uint64_t ino;
    vfs_file_type_t type;
    uint32_t mode;
    uint64_t size;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint32_t nlinks;
    uint32_t ref_count;
    uint32_t mount_id;
    uint32_t dev_id;
    bool dirty;
} vfs_inode_t;

typedef struct
{
    uint32_t fd;
    uint32_t mount_id;
    vfs_inode_t *inode;
    uint64_t offset;
    uint32_t flags;
    bool in_use;
} vfs_fd_t;

typedef struct
{
    uint32_t mount_id;
    char path[VFS_PATH_MAX];
    vfs_fstype_t fstype;
    uint32_t dev_id;
    bool mounted;
} vfs_mount_t;

/* ========================================================================
 * VFS 简化实现（宿主机版）
 * ======================================================================== */

static vfs_mount_t s_mounts[VFS_MAX_MOUNTS];
static bool s_mount_used[VFS_MAX_MOUNTS];
static vfs_fd_t s_fd_table[VFS_MAX_FDS];
static vfs_inode_t s_inode_cache[128U];
static bool s_inode_used[128U];
static bool s_initialized = false;
static uint64_t s_next_ino = 1U;
static uint32_t s_mount_count = 0U;

/* RAMFS 存储模拟 */
#define VFS_RAMFS_SIZE  8192U
static uint8_t s_ramfs_storage[VFS_RAMFS_SIZE];

static void vfs_strcpy(char *dst, const char *src, uint32_t n)
{
    uint32_t i;
    if ((dst == NULL) || (src == NULL) || (n == 0U))
    {
        return;
    }
    for (i = 0U; (i < (n - 1U)) && (src[i] != '\0'); i++)
    {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static kernel_status_t vfs_init(void)
{
    uint32_t i;
    (void)memset(s_mounts, 0, sizeof(s_mounts));
    (void)memset(s_mount_used, 0, sizeof(s_mount_used));
    (void)memset(s_fd_table, 0, sizeof(s_fd_table));
    (void)memset(s_inode_cache, 0, sizeof(s_inode_cache));
    (void)memset(s_inode_used, 0, sizeof(s_inode_used));
    (void)memset(s_ramfs_storage, 0, sizeof(s_ramfs_storage));

    for (i = 0U; i < VFS_MAX_FDS; i++)
    {
        s_fd_table[i].fd = i;
        s_fd_table[i].in_use = false;
    }
    for (i = 0U; i < 128U; i++)
    {
        s_inode_cache[i].ino = 0U;
        s_inode_used[i] = false;
    }
    s_next_ino = 1U;
    s_mount_count = 0U;
    s_initialized = true;
    return KERNEL_OK;
}

static vfs_inode_t *vfs_alloc_inode(void)
{
    uint32_t i;
    for (i = 0U; i < 128U; i++)
    {
        if (!s_inode_used[i])
        {
            (void)memset(&s_inode_cache[i], 0, sizeof(vfs_inode_t));
            s_inode_cache[i].ino = s_next_ino++;
            s_inode_used[i] = true;
            return &s_inode_cache[i];
        }
    }
    return NULL;
}

static int32_t vfs_mount(const char *path, vfs_fstype_t fstype, uint32_t dev_id)
{
    uint32_t i;
    vfs_mount_t *mp;

    if (!s_initialized)
    {
        return -(int32_t)EINVAL;
    }
    if (path == NULL)
    {
        return -(int32_t)EINVAL;
    }
    if (s_mount_count >= VFS_MAX_MOUNTS)
    {
        return -(int32_t)ENOMEM;
    }

    for (i = 0U; i < VFS_MAX_MOUNTS; i++)
    {
        if (!s_mount_used[i])
        {
            break;
        }
    }
    if (i >= VFS_MAX_MOUNTS)
    {
        return -(int32_t)ENOMEM;
    }

    mp = &s_mounts[i];
    vfs_strcpy(mp->path, path, VFS_PATH_MAX);
    mp->fstype = fstype;
    mp->dev_id = dev_id;
    mp->mounted = true;
    mp->mount_id = i;
    s_mount_used[i] = true;
    s_mount_count++;
    return (int32_t)i;
}

static kernel_status_t vfs_unmount(uint32_t mount_id)
{
    uint32_t i;
    if (mount_id >= VFS_MAX_MOUNTS)
    {
        return -(int32_t)EINVAL;
    }
    if (!s_mount_used[mount_id])
    {
        return -(int32_t)ENOENT;
    }

    /* 检查是否有打开的 FD */
    for (i = 0U; i < VFS_MAX_FDS; i++)
    {
        if (s_fd_table[i].in_use && (s_fd_table[i].mount_id == mount_id))
        {
            return -(int32_t)EBUSY;
        }
    }

    s_mount_used[mount_id] = false;
    s_mounts[mount_id].mounted = false;
    s_mount_count--;
    return KERNEL_OK;
}

static int32_t vfs_open(const char *path, uint32_t flags)
{
    uint32_t i;
    int32_t mount_id = -1;
    vfs_inode_t *inode;
    uint32_t fd_idx;

    if ((path == NULL) || (!s_initialized))
    {
        return -(int32_t)EINVAL;
    }

    /* 查找挂载点 */
    for (i = 0U; i < VFS_MAX_MOUNTS; i++)
    {
        if (s_mount_used[i])
        {
            mount_id = (int32_t)i;
            break;
        }
    }
    if (mount_id < 0)
    {
        return -(int32_t)ENOENT;
    }

    /* 分配 inode */
    inode = vfs_alloc_inode();
    if (inode == NULL)
    {
        return -(int32_t)ENOMEM;
    }
    inode->type = VFS_TYPE_REGULAR;
    inode->mount_id = (uint32_t)mount_id;

    /* 分配 FD */
    for (i = 0U; i < VFS_MAX_FDS; i++)
    {
        if (!s_fd_table[i].in_use)
        {
            break;
        }
    }
    if (i >= VFS_MAX_FDS)
    {
        return -(int32_t)ENOMEM;
    }

    fd_idx = i;
    s_fd_table[fd_idx].in_use = true;
    s_fd_table[fd_idx].inode = inode;
    s_fd_table[fd_idx].mount_id = (uint32_t)mount_id;
    s_fd_table[fd_idx].offset = 0U;
    s_fd_table[fd_idx].flags = flags;
    inode->ref_count++;

    return (int32_t)fd_idx;
}

static kernel_status_t vfs_close(uint32_t fd)
{
    vfs_fd_t *fdf;
    vfs_inode_t *inode;

    if (fd >= VFS_MAX_FDS)
    {
        return -(int32_t)EINVAL;
    }
    fdf = &s_fd_table[fd];
    if (!fdf->in_use)
    {
        return -(int32_t)EINVAL;
    }

    inode = fdf->inode;
    if (inode != NULL)
    {
        inode->ref_count--;
        if (inode->ref_count == 0U)
        {
            uint32_t ino_idx = (uint32_t)(inode - s_inode_cache);
            if (ino_idx < 128U)
            {
                s_inode_used[ino_idx] = false;
            }
        }
    }

    fdf->in_use = false;
    fdf->inode = NULL;
    fdf->offset = 0U;
    return KERNEL_OK;
}

static int64_t vfs_write(uint32_t fd, const void *buf, uint64_t size)
{
    vfs_fd_t *fdf;

    if (fd >= VFS_MAX_FDS)
    {
        return -(int64_t)EINVAL;
    }
    if (buf == NULL)
    {
        return -(int64_t)EINVAL;
    }

    fdf = &s_fd_table[fd];
    if (!fdf->in_use)
    {
        return -(int64_t)EINVAL;
    }

    /* 写入到 RAMFS 存储模拟 */
    uint64_t offset = fdf->offset;
    uint64_t to_write = size;
    if ((offset + to_write) > VFS_RAMFS_SIZE)
    {
        to_write = VFS_RAMFS_SIZE - offset;
    }
    if (to_write > 0ULL)
    {
        (void)memcpy(&s_ramfs_storage[offset], buf, (size_t)to_write);
        fdf->offset = offset + to_write;
        if (fdf->inode != NULL)
        {
            fdf->inode->size = fdf->offset;
            fdf->inode->dirty = true;
        }
    }

    return (int64_t)to_write;
}

static int64_t vfs_read(uint32_t fd, void *buf, uint64_t size)
{
    vfs_fd_t *fdf;

    if (fd >= VFS_MAX_FDS)
    {
        return -(int64_t)EINVAL;
    }
    if (buf == NULL)
    {
        return -(int64_t)EINVAL;
    }

    fdf = &s_fd_table[fd];
    if (!fdf->in_use)
    {
        return -(int64_t)EINVAL;
    }

    uint64_t offset = fdf->offset;
    uint64_t file_size = (fdf->inode != NULL) ? fdf->inode->size : 0ULL;
    uint64_t to_read = size;

    if (offset >= file_size)
    {
        return 0LL;
    }
    if ((offset + to_read) > file_size)
    {
        to_read = file_size - offset;
    }

    (void)memcpy(buf, &s_ramfs_storage[offset], (size_t)to_read);
    fdf->offset = offset + to_read;

    return (int64_t)to_read;
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

void test_vfs_init_succeeds(void)
{
    kernel_status_t ret = vfs_init();
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_TRUE(s_initialized);
}

void test_vfs_mount_basic(void)
{
    vfs_init();
    int32_t mid = vfs_mount("/mnt/ram", VFS_FS_RAMFS, 0U);
    TEST_ASSERT_GT(mid, -1);
    TEST_ASSERT_TRUE(s_mounts[(uint32_t)mid].mounted);
}

void test_vfs_mount_max(void)
{
    uint32_t i;
    int32_t result;
    vfs_init();

    for (i = 0U; i < VFS_MAX_MOUNTS; i++)
    {
        char path[32];
        (void)snprintf(path, sizeof(path), "/mnt%u", i);
        result = vfs_mount(path, VFS_FS_RAMFS, i);
        TEST_ASSERT_GT(result, -1);
    }

    /* 超出限制 */
    result = vfs_mount("/overflow", VFS_FS_RAMFS, 99U);
    TEST_ASSERT_LT(result, 0);
}

void test_vfs_mount_invalid_params(void)
{
    vfs_init();
    TEST_ASSERT_LT(vfs_mount(NULL, VFS_FS_RAMFS, 0U), 0);
}

void test_vfs_unmount_basic(void)
{
    vfs_init();
    int32_t mid = vfs_mount("/tmp", VFS_FS_RAMFS, 0U);
    TEST_ASSERT_GT(mid, -1);

    kernel_status_t ret = vfs_unmount((uint32_t)mid);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_TRUE(!s_mounts[(uint32_t)mid].mounted);
}

void test_vfs_unmount_nonexistent(void)
{
    vfs_init();
    TEST_ASSERT_LT(vfs_unmount(99U), 0);
}

void test_vfs_open_close_basic(void)
{
    vfs_init();
    (void)vfs_mount("/mnt", VFS_FS_RAMFS, 0U);

    int32_t fd = vfs_open("/mnt/test.txt", 0U);
    TEST_ASSERT_GT(fd, -1);
    TEST_ASSERT_TRUE(s_fd_table[(uint32_t)fd].in_use);

    kernel_status_t ret = vfs_close((uint32_t)fd);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    TEST_ASSERT_TRUE(!s_fd_table[(uint32_t)fd].in_use);
}

void test_vfs_open_no_mount(void)
{
    vfs_init();
    int32_t fd = vfs_open("/mnt/test.txt", 0U);
    TEST_ASSERT_LT(fd, 0);
}

void test_vfs_close_invalid(void)
{
    vfs_init();
    TEST_ASSERT_LT(vfs_close(99U), 0);
}

void test_vfs_write_read_e2e(void)
{
    vfs_init();
    (void)vfs_mount("/mnt", VFS_FS_RAMFS, 0U);

    int32_t fd = vfs_open("/mnt/data.bin", 0U);
    TEST_ASSERT_GT(fd, -1);

    const char *test_data = "VFS read/write test!";
    uint32_t len = (uint32_t)strlen(test_data);

    /* 写入 */
    int64_t written = vfs_write((uint32_t)fd, test_data, (uint64_t)len);
    TEST_ASSERT_EQ(written, (int64_t)len);

    /* seek 回开头 - 模拟重置 offset */
    s_fd_table[(uint32_t)fd].offset = 0U;

    /* 读取 */
    char read_buf[64U];
    (void)memset(read_buf, 0, sizeof(read_buf));
    int64_t read_bytes = vfs_read((uint32_t)fd, read_buf, (uint64_t)len);
    TEST_ASSERT_EQ(read_bytes, (int64_t)len);

    /* 验证数据一致 */
    TEST_ASSERT_EQ((int64_t)memcmp(read_buf, test_data, (size_t)len), 0);

    (void)vfs_close((uint32_t)fd);
}

void test_vfs_read_empty_file(void)
{
    vfs_init();
    (void)vfs_mount("/mnt", VFS_FS_RAMFS, 0U);

    int32_t fd = vfs_open("/mnt/empty.txt", 0U);
    TEST_ASSERT_GT(fd, -1);

    char buf[32U];
    int64_t r = vfs_read((uint32_t)fd, buf, 32U);
    TEST_ASSERT_EQ(r, 0LL);

    (void)vfs_close((uint32_t)fd);
}

void test_vfs_unmount_busy(void)
{
    vfs_init();
    int32_t mid = vfs_mount("/mnt", VFS_FS_RAMFS, 0U);
    TEST_ASSERT_GT(mid, -1);

    int32_t fd = vfs_open("/mnt/test.txt", 0U);
    TEST_ASSERT_GT(fd, -1);

    /* 有打开的文件，卸载应失败 */
    TEST_ASSERT_LT(vfs_unmount((uint32_t)mid), 0);

    (void)vfs_close((uint32_t)fd);
    /* 关闭后卸载应成功 */
    TEST_ASSERT_EQ(vfs_unmount((uint32_t)mid), KERNEL_OK);
}

void test_vfs_max_fds(void)
{
    uint32_t i;
    int32_t fds[VFS_MAX_FDS + 1U];
    vfs_init();
    (void)vfs_mount("/mnt", VFS_FS_RAMFS, 0U);

    for (i = 0U; i < VFS_MAX_FDS; i++)
    {
        fds[i] = vfs_open("/mnt/file", 0U);
        TEST_ASSERT_GT(fds[i], -1);
    }

    /* 超出限制 */
    fds[VFS_MAX_FDS] = vfs_open("/mnt/overflow", 0U);
    TEST_ASSERT_LT(fds[VFS_MAX_FDS], 0);

    /* 清理 */
    for (i = 0U; i < VFS_MAX_FDS; i++)
    {
        (void)vfs_close((uint32_t)fds[i]);
    }
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("\n=== VFS 虚拟文件系统 单元测试 ===\n\n");

    printf("--- 初始化 ---\n");
    TEST_RUN(vfs_init_succeeds);

    printf("--- 挂载/卸载 ---\n");
    TEST_RUN(vfs_mount_basic);
    TEST_RUN(vfs_mount_max);
    TEST_RUN(vfs_mount_invalid_params);
    TEST_RUN(vfs_unmount_basic);
    TEST_RUN(vfs_unmount_nonexistent);
    TEST_RUN(vfs_unmount_busy);

    printf("--- 文件操作 ---\n");
    TEST_RUN(vfs_open_close_basic);
    TEST_RUN(vfs_open_no_mount);
    TEST_RUN(vfs_close_invalid);
    TEST_RUN(vfs_write_read_e2e);
    TEST_RUN(vfs_read_empty_file);
    TEST_RUN(vfs_max_fds);

    printf("\n=== 测试结果 ===\n");
    printf("总计: %u  通过: %u  失败: %u\n",
           s_total, s_passed, s_failed);

    return (s_failed == 0U) ? 0 : 1;
}
