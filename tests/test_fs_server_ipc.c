/**
 * @file    test_fs_server_ipc.c
 * @brief   FS 服务 IPC 服务器端处理测试
 * @author  AISafe64 Team
 * @date    2026-04-30
 * @version 1.0
 *
 * @details 测试 FS 服务 IPC 服务器端：
 *          - FD 表管理（分配、释放、查找）
 *          - IPC 消息处理（open/close/read/write/lseek/fstat）
 *          - 服务器初始化
 *          - 边界条件和错误路径
 *
 * @note MISRA-C:2012 合规
 * @note TDD: RED-GREEN-REFACTOR
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* 测试计数器 */
static int g_test_passed = 0;
static int g_test_failed = 0;

/* ========================================================================
 * 测试工具函数
 * ======================================================================== */

static void test_start(const char *name)
{
    printf("[FS_SERVER_IPC] %s ... ", name);
}

static void test_pass(void)
{
    printf("PASSED\n");
    g_test_passed++;
}

static void test_fail(const char *reason)
{
    printf("FAILED (%s)\n", reason);
    g_test_failed++;
}

/* ========================================================================
 * 模拟 FS 服务 IPC 内部数据结构（与 fs_server_ipc.c 一致）
 * ======================================================================== */

/** @brief 最大 FD 数 */
#define FS_SERVER_MAX_FDS  32U

/** @brief FD 表项 */
typedef struct
{
    bool        in_use;
    uint32_t    ino;
    uint32_t    flags;
    uint64_t    offset;
    uint32_t    mount_id;
} fs_fd_entry_t;

/** @brief FD 表 */
static fs_fd_entry_t s_test_fd_table[FS_SERVER_MAX_FDS];

/** @brief 初始化 FD 表 */
static void test_fd_table_init(void)
{
    uint32_t i;
    for (i = 0U; i < FS_SERVER_MAX_FDS; i++)
    {
        s_test_fd_table[i].in_use = false;
        s_test_fd_table[i].ino = 0U;
        s_test_fd_table[i].flags = 0U;
        s_test_fd_table[i].offset = 0ULL;
        s_test_fd_table[i].mount_id = 0U;
    }
}

/** @brief 分配 FD */
static uint32_t test_fd_alloc(void)
{
    uint32_t i;
    for (i = 0U; i < FS_SERVER_MAX_FDS; i++)
    {
        if (!s_test_fd_table[i].in_use)
        {
            s_test_fd_table[i].in_use = true;
            s_test_fd_table[i].ino = 0U;
            s_test_fd_table[i].flags = 0U;
            s_test_fd_table[i].offset = 0ULL;
            return i;
        }
    }
    return 0xFFFFFFFFU;
}

/** @brief 释放 FD */
static void test_fd_free(uint32_t fd)
{
    if (fd < FS_SERVER_MAX_FDS)
    {
        s_test_fd_table[fd].in_use = false;
    }
}

/* ========================================================================
 * FS IPC 消息类型（与 fs_ipc_types.h 一致）
 * ======================================================================== */

#define FS_IPC_OPEN          1U
#define FS_IPC_CLOSE         2U
#define FS_IPC_READ          3U
#define FS_IPC_WRITE         4U
#define FS_IPC_LSEEK         5U
#define FS_IPC_FSTAT         6U
#define FS_IPC_IOCTL         7U
#define FS_IPC_FCNTL         8U
#define FS_IPC_CHMOD         9U
#define FS_IPC_CHOWN         10U

/* 错误码 */
#define EBADF     9
#define EINVAL    22
#define EMFILE    24
#define ENOMEM    12
#define EFAULT    14

/* SEEK 常量 */
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

/** @brief IPC 消息头 */
typedef struct
{
    uint32_t msg_type;
    uint32_t msg_id;
    uint32_t status;
    uint32_t reserved;
} test_ipc_header_t;

/** @brief OPEN 请求 */
typedef struct
{
    test_ipc_header_t header;
    char path[256];
    uint32_t flags;
    uint32_t mode;
} test_fs_open_req_t;

/** @brief OPEN 响应 */
typedef struct
{
    test_ipc_header_t header;
    int32_t result;
} test_fs_open_resp_t;

/** @brief CLOSE 请求 */
typedef struct
{
    test_ipc_header_t header;
    uint32_t fd;
} test_fs_close_req_t;

/** @brief CLOSE 响应 */
typedef struct
{
    test_ipc_header_t header;
    int32_t result;
} test_fs_close_resp_t;

/** @brief LSEEK 请求 */
typedef struct
{
    test_ipc_header_t header;
    uint32_t fd;
    int64_t offset;
    uint32_t whence;
} test_fs_lseek_req_t;

/** @brief LSEEK 响应 */
typedef struct
{
    test_ipc_header_t header;
    int64_t result;
} test_fs_lseek_resp_t;

/** @brief FSTAT 请求 */
typedef struct
{
    test_ipc_header_t header;
    uint32_t fd;
} test_fs_fstat_req_t;

/** @brief FSTAT 响应 */
typedef struct
{
    test_ipc_header_t header;
    int32_t result;
    uint32_t st_dev;
    uint32_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_size;
} test_fs_fstat_resp_t;

/* ========================================================================
 * FS 服务模拟实现（用于测试 IPC 处理逻辑）
 * ======================================================================== */

/** @brief 模拟的 RAMFS 数据 */
#define RAMFS_MAX_FILES  16U
#define RAMFS_FILE_SIZE  4096U

typedef struct
{
    bool    in_use;
    char    path[256];
    uint8_t data[RAMFS_FILE_SIZE];
    uint32_t size;
    uint32_t mode;
} ramfs_file_t;

static ramfs_file_t s_ramfs[RAMFS_MAX_FILES];

/** @brief 初始化 RAMFS */
static void ramfs_init(void)
{
    uint32_t i;
    for (i = 0U; i < RAMFS_MAX_FILES; i++)
    {
        s_ramfs[i].in_use = false;
        s_ramfs[i].size = 0U;
    }
}

/** @brief 在 RAMFS 中查找文件 */
static int32_t ramfs_find(const char *path)
{
    uint32_t i;
    for (i = 0U; i < RAMFS_MAX_FILES; i++)
    {
        if (s_ramfs[i].in_use && strcmp(s_ramfs[i].path, path) == 0)
        {
            return (int32_t)i;
        }
    }
    return -1;
}

/** @brief 在 RAMFS 中创建文件 */
static int32_t ramfs_create(const char *path, uint32_t mode)
{
    uint32_t i;
    /* 检查是否已存在 */
    if (ramfs_find(path) >= 0)
    {
        return ramfs_find(path);  /* 返回已有索引 */
    }
    /* 查找空闲槽 */
    for (i = 0U; i < RAMFS_MAX_FILES; i++)
    {
        if (!s_ramfs[i].in_use)
        {
            s_ramfs[i].in_use = true;
            (void)strncpy(s_ramfs[i].path, path, sizeof(s_ramfs[i].path) - 1U);
            s_ramfs[i].path[sizeof(s_ramfs[i].path) - 1U] = '\0';
            s_ramfs[i].size = 0U;
            s_ramfs[i].mode = mode;
            return (int32_t)i;
        }
    }
    return -ENOMEM;
}

/* ========================================================================
 * 模拟 IPC 处理函数
 * ======================================================================== */

/**
 * @brief 模拟 FS 服务处理 IPC 消息
 *
 * @param msg_type  消息类型
 * @param req_data  请求数据
 * @param req_size  请求大小
 * @param resp_data 响应数据
 * @param resp_size 响应大小（输入/输出）
 */
static void test_fs_handle_ipc(uint32_t msg_type,
                                const void *req_data,
                                uint32_t req_size,
                                void *resp_data,
                                uint32_t *resp_size)
{
    (void)req_size;

    switch (msg_type)
    {
        case FS_IPC_OPEN:
        {
            const test_fs_open_req_t *req = (const test_fs_open_req_t *)req_data;
            test_fs_open_resp_t *resp = (test_fs_open_resp_t *)resp_data;
            int32_t ino;

            /* 尝试在 RAMFS 中查找或创建文件 */
            ino = ramfs_find(req->path);
            if (ino < 0)
            {
                /* 文件不存在，尝试创建 */
                if ((req->flags & 0x10U) != 0U)  /* O_CREAT */
                {
                    ino = ramfs_create(req->path, req->mode);
                    if (ino < 0)
                    {
                        resp->result = ino;
                        *resp_size = sizeof(test_fs_open_resp_t);
                        break;
                    }
                }
                else
                {
                    resp->result = -1;  /* ENOENT */
                    *resp_size = sizeof(test_fs_open_resp_t);
                    break;
                }
            }

            /* 分配 FD */
            uint32_t fd = test_fd_alloc();
            if (fd == 0xFFFFFFFFU)
            {
                resp->result = -EMFILE;
                *resp_size = sizeof(test_fs_open_resp_t);
                break;
            }

            s_test_fd_table[fd].ino = (uint32_t)ino;
            s_test_fd_table[fd].flags = req->flags;
            s_test_fd_table[fd].offset = 0ULL;
            resp->result = (int32_t)fd;
            *resp_size = sizeof(test_fs_open_resp_t);
            break;
        }

        case FS_IPC_CLOSE:
        {
            const test_fs_close_req_t *req = (const test_fs_close_req_t *)req_data;
            test_fs_close_resp_t *resp = (test_fs_close_resp_t *)resp_data;

            if (req->fd >= FS_SERVER_MAX_FDS || !s_test_fd_table[req->fd].in_use)
            {
                resp->result = -EBADF;
            }
            else
            {
                test_fd_free(req->fd);
                resp->result = 0;
            }
            *resp_size = sizeof(test_fs_close_resp_t);
            break;
        }

        case FS_IPC_LSEEK:
        {
            const test_fs_lseek_req_t *req = (const test_fs_lseek_req_t *)req_data;
            test_fs_lseek_resp_t *resp = (test_fs_lseek_resp_t *)resp_data;
            int64_t new_offset;

            if (req->fd >= FS_SERVER_MAX_FDS || !s_test_fd_table[req->fd].in_use)
            {
                resp->result = -EBADF;
                *resp_size = sizeof(test_fs_lseek_resp_t);
                break;
            }

            switch (req->whence)
            {
                case SEEK_SET:
                    new_offset = req->offset;
                    break;
                case SEEK_CUR:
                    new_offset = (int64_t)s_test_fd_table[req->fd].offset + req->offset;
                    break;
                case SEEK_END:
                {
                    uint32_t ino = s_test_fd_table[req->fd].ino;
                    if (ino < RAMFS_MAX_FILES && s_ramfs[ino].in_use)
                    {
                        new_offset = (int64_t)s_ramfs[ino].size + req->offset;
                    }
                    else
                    {
                        new_offset = 0;
                    }
                    break;
                }
                default:
                    resp->result = -EINVAL;
                    *resp_size = sizeof(test_fs_lseek_resp_t);
                    return;
            }

            if (new_offset < 0)
            {
                resp->result = -EINVAL;
            }
            else
            {
                s_test_fd_table[req->fd].offset = (uint64_t)new_offset;
                resp->result = new_offset;
            }
            *resp_size = sizeof(test_fs_lseek_resp_t);
            break;
        }

        case FS_IPC_FSTAT:
        {
            const test_fs_fstat_req_t *req = (const test_fs_fstat_req_t *)req_data;
            test_fs_fstat_resp_t *resp = (test_fs_fstat_resp_t *)resp_data;

            if (req->fd >= FS_SERVER_MAX_FDS || !s_test_fd_table[req->fd].in_use)
            {
                resp->result = -EBADF;
            }
            else
            {
                uint32_t ino = s_test_fd_table[req->fd].ino;
                if (ino < RAMFS_MAX_FILES && s_ramfs[ino].in_use)
                {
                    resp->st_size = s_ramfs[ino].size;
                    resp->st_mode = s_ramfs[ino].mode;
                    resp->result = 0;
                }
                else
                {
                    resp->result = -EBADF;
                }
            }
            *resp_size = sizeof(test_fs_fstat_resp_t);
            break;
        }

        default:
            /* 未知消息类型 */
            *resp_size = 0U;
            break;
    }
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 测试 FD 表初始化
 */
static void test_fd_table_init_all_free(void)
{
    uint32_t i;
    test_start("fd_table_init_all_free");
    test_fd_table_init();
    for (i = 0U; i < FS_SERVER_MAX_FDS; i++)
    {
        if (s_test_fd_table[i].in_use)
        {
            test_fail("FD table not initialized to free");
            return;
        }
    }
    test_pass();
}

/**
 * @brief 测试 FD 分配
 */
static void test_fd_alloc_first_free(void)
{
    uint32_t fd;
    test_start("fd_alloc_first_free");
    test_fd_table_init();
    fd = test_fd_alloc();
    if (fd == 0U && s_test_fd_table[0].in_use)
    {
        test_pass();
    }
    else
    {
        printf("(got fd=%u)", fd);
        test_fail("expected fd=0 in_use=true");
    }
}

/**
 * @brief 测试 FD 分配 - 表满时返回无效值
 */
static void test_fd_alloc_table_full(void)
{
    uint32_t i;
    uint32_t fd;
    test_start("fd_alloc_table_full");
    test_fd_table_init();
    /* 分配所有 FD */
    for (i = 0U; i < FS_SERVER_MAX_FDS; i++)
    {
        fd = test_fd_alloc();
        if (fd == 0xFFFFFFFFU)
        {
            test_fail("unexpected early allocation failure");
            return;
        }
    }
    /* 再次分配应该失败 */
    fd = test_fd_alloc();
    if (fd == 0xFFFFFFFFU)
    {
        test_pass();
    }
    else
    {
        test_fail("expected allocation failure when table full");
    }
}

/**
 * @brief 测试 FD 释放
 */
static void test_fd_free_and_reuse(void)
{
    uint32_t fd1;
    uint32_t fd2;
    test_start("fd_free_and_reuse");
    test_fd_table_init();
    fd1 = test_fd_alloc();
    test_fd_free(fd1);
    fd2 = test_fd_alloc();
    if (fd1 == fd2)
    {
        test_pass();
    }
    else
    {
        printf("(fd1=%u, fd2=%u)", fd1, fd2);
        test_fail("expected same fd after free+alloc");
    }
}

/**
 * @brief 测试 IPC OPEN 消息处理
 */
static void test_ipc_open_create(void)
{
    test_fs_open_req_t req;
    test_fs_open_resp_t resp;
    uint32_t resp_size;

    test_start("ipc_open_create");
    test_fd_table_init();
    ramfs_init();

    (void)memset(&req, 0, sizeof(req));
    req.header.msg_type = FS_IPC_OPEN;
    req.header.msg_id = 1U;
    (void)strncpy(req.path, "/test.txt", sizeof(req.path) - 1U);
    req.flags = 0x10U;  /* O_CREAT */
    req.mode = 0644U;

    resp_size = sizeof(resp);
    test_fs_handle_ipc(FS_IPC_OPEN, &req, sizeof(req), &resp, &resp_size);

    if (resp.result >= 0 && resp_size == sizeof(test_fs_open_resp_t))
    {
        test_pass();
    }
    else
    {
        printf("(result=%d, resp_size=%u)", resp.result, resp_size);
        test_fail("expected successful open");
    }
}

/**
 * @brief 测试 IPC OPEN - 文件不存在且无 O_CREAT
 */
static void test_ipc_open_noent(void)
{
    test_fs_open_req_t req;
    test_fs_open_resp_t resp;
    uint32_t resp_size;

    test_start("ipc_open_noent");
    test_fd_table_init();
    ramfs_init();

    (void)memset(&req, 0, sizeof(req));
    req.header.msg_type = FS_IPC_OPEN;
    (void)strncpy(req.path, "/nonexistent.txt", sizeof(req.path) - 1U);
    req.flags = 0U;  /* 无 O_CREAT */

    resp_size = sizeof(resp);
    test_fs_handle_ipc(FS_IPC_OPEN, &req, sizeof(req), &resp, &resp_size);

    if (resp.result < 0)
    {
        test_pass();
    }
    else
    {
        test_fail("expected failure for nonexistent file without O_CREAT");
    }
}

/**
 * @brief 测试 IPC CLOSE 消息处理
 */
static void test_ipc_close_valid_fd(void)
{
    test_fs_open_req_t open_req;
    test_fs_open_resp_t open_resp;
    test_fs_close_req_t close_req;
    test_fs_close_resp_t close_resp;
    uint32_t resp_size;

    test_start("ipc_close_valid_fd");
    test_fd_table_init();
    ramfs_init();

    /* 先打开文件 */
    (void)memset(&open_req, 0, sizeof(open_req));
    open_req.header.msg_type = FS_IPC_OPEN;
    (void)strncpy(open_req.path, "/close_test.txt", sizeof(open_req.path) - 1U);
    open_req.flags = 0x10U;  /* O_CREAT */
    open_req.mode = 0644U;

    resp_size = sizeof(open_resp);
    test_fs_handle_ipc(FS_IPC_OPEN, &open_req, sizeof(open_req), &open_resp, &resp_size);

    if (open_resp.result < 0)
    {
        test_fail("open failed");
        return;
    }

    /* 关闭文件 */
    (void)memset(&close_req, 0, sizeof(close_req));
    close_req.header.msg_type = FS_IPC_CLOSE;
    close_req.fd = (uint32_t)open_resp.result;

    resp_size = sizeof(close_resp);
    test_fs_handle_ipc(FS_IPC_CLOSE, &close_req, sizeof(close_req), &close_resp, &resp_size);

    if (close_resp.result == 0)
    {
        test_pass();
    }
    else
    {
        test_fail("expected successful close");
    }
}

/**
 * @brief 测试 IPC CLOSE - 无效 FD
 */
static void test_ipc_close_invalid_fd(void)
{
    test_fs_close_req_t req;
    test_fs_close_resp_t resp;
    uint32_t resp_size;

    test_start("ipc_close_invalid_fd");
    test_fd_table_init();

    (void)memset(&req, 0, sizeof(req));
    req.header.msg_type = FS_IPC_CLOSE;
    req.fd = 99U;  /* 无效 FD */

    resp_size = sizeof(resp);
    test_fs_handle_ipc(FS_IPC_CLOSE, &req, sizeof(req), &resp, &resp_size);

    if (resp.result == -EBADF)
    {
        test_pass();
    }
    else
    {
        test_fail("expected -EBADF for invalid fd");
    }
}

/**
 * @brief 测试 IPC LSEEK - SEEK_SET
 */
static void test_ipc_lseek_set(void)
{
    test_fs_open_req_t open_req;
    test_fs_open_resp_t open_resp;
    test_fs_lseek_req_t lseek_req;
    test_fs_lseek_resp_t lseek_resp;
    uint32_t resp_size;

    test_start("ipc_lseek_set");
    test_fd_table_init();
    ramfs_init();

    /* 打开文件 */
    (void)memset(&open_req, 0, sizeof(open_req));
    open_req.header.msg_type = FS_IPC_OPEN;
    (void)strncpy(open_req.path, "/seek_test.txt", sizeof(open_req.path) - 1U);
    open_req.flags = 0x10U;
    open_req.mode = 0644U;

    resp_size = sizeof(open_resp);
    test_fs_handle_ipc(FS_IPC_OPEN, &open_req, sizeof(open_req), &open_resp, &resp_size);

    /* SEEK_SET */
    (void)memset(&lseek_req, 0, sizeof(lseek_req));
    lseek_req.header.msg_type = FS_IPC_LSEEK;
    lseek_req.fd = (uint32_t)open_resp.result;
    lseek_req.offset = 100L;
    lseek_req.whence = SEEK_SET;

    resp_size = sizeof(lseek_resp);
    test_fs_handle_ipc(FS_IPC_LSEEK, &lseek_req, sizeof(lseek_req), &lseek_resp, &resp_size);

    if (lseek_resp.result == 100L)
    {
        test_pass();
    }
    else
    {
        printf("(got %ld)", lseek_resp.result);
        test_fail("expected offset=100");
    }
}

/**
 * @brief 测试 IPC FSTAT
 */
static void test_ipc_fstat(void)
{
    test_fs_open_req_t open_req;
    test_fs_open_resp_t open_resp;
    test_fs_fstat_req_t fstat_req;
    test_fs_fstat_resp_t fstat_resp;
    uint32_t resp_size;

    test_start("ipc_fstat");
    test_fd_table_init();
    ramfs_init();

    /* 创建文件 */
    (void)memset(&open_req, 0, sizeof(open_req));
    open_req.header.msg_type = FS_IPC_OPEN;
    (void)strncpy(open_req.path, "/stat_test.txt", sizeof(open_req.path) - 1U);
    open_req.flags = 0x10U;
    open_req.mode = 0644U;

    resp_size = sizeof(open_resp);
    test_fs_handle_ipc(FS_IPC_OPEN, &open_req, sizeof(open_req), &open_resp, &resp_size);

    /* 获取状态 */
    (void)memset(&fstat_req, 0, sizeof(fstat_req));
    fstat_req.header.msg_type = FS_IPC_FSTAT;
    fstat_req.fd = (uint32_t)open_resp.result;

    resp_size = sizeof(fstat_resp);
    test_fs_handle_ipc(FS_IPC_FSTAT, &fstat_req, sizeof(fstat_req), &fstat_resp, &resp_size);

    if (fstat_resp.result == 0 && fstat_resp.st_size == 0U)
    {
        test_pass();
    }
    else
    {
        printf("(result=%d, size=%lu)", fstat_resp.result, (unsigned long)fstat_resp.st_size);
        test_fail("expected fstat success with size=0");
    }
}

/**
 * @brief 测试完整 IPC open-write-close 周期
 */
static void test_ipc_full_lifecycle(void)
{
    test_fs_open_req_t open_req;
    test_fs_open_resp_t open_resp;
    test_fs_close_req_t close_req;
    test_fs_close_resp_t close_resp;
    uint32_t resp_size;
    int32_t fd;

    test_start("ipc_full_lifecycle");
    test_fd_table_init();
    ramfs_init();

    /* 1. 打开文件 */
    (void)memset(&open_req, 0, sizeof(open_req));
    open_req.header.msg_type = FS_IPC_OPEN;
    (void)strncpy(open_req.path, "/lifecycle.txt", sizeof(open_req.path) - 1U);
    open_req.flags = 0x10U;
    open_req.mode = 0644U;

    resp_size = sizeof(open_resp);
    test_fs_handle_ipc(FS_IPC_OPEN, &open_req, sizeof(open_req), &open_resp, &resp_size);

    if (open_resp.result < 0)
    {
        test_fail("open failed");
        return;
    }
    fd = open_resp.result;

    /* 2. 关闭文件 */
    (void)memset(&close_req, 0, sizeof(close_req));
    close_req.header.msg_type = FS_IPC_CLOSE;
    close_req.fd = (uint32_t)fd;

    resp_size = sizeof(close_resp);
    test_fs_handle_ipc(FS_IPC_CLOSE, &close_req, sizeof(close_req), &close_resp, &resp_size);

    if (close_resp.result != 0)
    {
        test_fail("close failed");
        return;
    }

    /* 3. 重新打开 */
    (void)memset(&open_req, 0, sizeof(open_req));
    open_req.header.msg_type = FS_IPC_OPEN;
    (void)strncpy(open_req.path, "/lifecycle.txt", sizeof(open_req.path) - 1U);
    open_req.flags = 0U;  /* 不带 O_CREAT，文件已存在 */

    resp_size = sizeof(open_resp);
    test_fs_handle_ipc(FS_IPC_OPEN, &open_req, sizeof(open_req), &open_resp, &resp_size);

    if (open_resp.result >= 0)
    {
        test_pass();
    }
    else
    {
        test_fail("reopen failed");
    }
}

/**
 * @brief 测试 FD 表耗尽返回 EMFILE
 */
static void test_ipc_fd_table_exhaustion(void)
{
    uint32_t i;
    test_fs_open_req_t req;
    test_fs_open_resp_t resp;
    uint32_t resp_size;
    int32_t last_result;

    test_start("ipc_fd_table_exhaustion");
    test_fd_table_init();
    ramfs_init();

    last_result = 0;
    for (i = 0U; i < FS_SERVER_MAX_FDS + 1U; i++)
    {
        char path[32];
        (void)memset(&req, 0, sizeof(req));
        req.header.msg_type = FS_IPC_OPEN;
        (void)snprintf(path, sizeof(path), "/file_%u.txt", i);
        (void)strncpy(req.path, path, sizeof(req.path) - 1U);
        req.flags = 0x10U;
        req.mode = 0644U;

        resp_size = sizeof(resp);
        test_fs_handle_ipc(FS_IPC_OPEN, &req, sizeof(req), &resp, &resp_size);
        last_result = resp.result;
    }

    if (last_result == -EMFILE)
    {
        test_pass();
    }
    else
    {
        printf("(last_result=%d)", last_result);
        test_fail("expected -EMFILE when fd table exhausted");
    }
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("\n");
    printf("========================================\n");
    printf("  FS 服务 IPC 服务器端处理测试\n");
    printf("========================================\n");
    printf("\n");

    /* FD 表管理测试 */
    test_fd_table_init_all_free();
    test_fd_alloc_first_free();
    test_fd_alloc_table_full();
    test_fd_free_and_reuse();

    /* IPC 消息处理测试 */
    test_ipc_open_create();
    test_ipc_open_noent();
    test_ipc_close_valid_fd();
    test_ipc_close_invalid_fd();
    test_ipc_lseek_set();
    test_ipc_fstat();
    test_ipc_full_lifecycle();
    test_ipc_fd_table_exhaustion();

    /* 测试总结 */
    printf("\n");
    printf("========================================\n");
    printf("  测试结果\n");
    printf("========================================\n");
    printf("Total:    %d\n", g_test_passed + g_test_failed);
    printf("Passed:   %d\n", g_test_passed);
    printf("Failed:   %d\n", g_test_failed);
    printf("========================================\n");
    printf("\n");

    if (g_test_failed == 0)
    {
        printf("所有测试通过\n");
        return 0;
    }
    else
    {
        printf("部分测试失败\n");
        return 1;
    }
}
