/**
 * @file    fs_ipc.c
 * @brief   AISafeOS64 musl 适配层 — 文件系统 IPC 客户端
 * @version 2.0
 *
 * @details 实现 musl 文件系统系统调用的 IPC 客户端：
 *          - 文件描述符表管理（每进程）
 *          - 通过 IPC 与 FS 服务通信
 *          - 标准 POSIX API 支持
 *
 * @note MISRA-C:2012 合规
 * @note AISafeOS64 微内核架构 - 用户态服务通过 IPC 通信
 */

#include "syscall_arch.h"
#include "musl_safety.h"
#include "syscall_numbers.h"
#include "fs_ipc.h"
#include <kernel/syscall.h>
#include <kernel/service.h>
#include <kernel/vfs.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* 仅在 ARM64 交叉编译时使用真正的 syscall_entry.h */
#if defined(__aarch64__) && !defined(AISAFE_TEST_MODE)
#include "../arch/aarch64_aisafe/syscall_entry.h"
#else
/* 测试模式：使用桩版本 */
#include "../arch/aarch64_aisafe/syscall_entry_test.h"
#endif

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief 最大文件描述符数 */
#define FS_MAX_FDS     32U

/** @brief lseek 定位方式 */
#define SEEK_SET        0  /* 从文件开头定位 */
#define SEEK_CUR        1  /* 从当前位置定位 */
#define SEEK_END        2  /* 从文件末尾定位 */

/** @brief fcntl 目录文件描述符 */
#define AT_FDCWD       -100  /* 当前工作目录 */

/** @brief FS 服务端点（需要通过服务发现获取） */
static int s_fs_endpoint = -1;

/* ========================================================================
 * 文件描述符表项
 * ======================================================================== */

/**
 * @brief 文件描述符表项
 */
typedef struct fd_entry
{
    bool        in_use;         /**< @brief 是否在使用 */
    uint32_t    vfs_fd;         /**< @brief VFS 文件描述符（FS 服务内部） */
    uint64_t    offset;         /**< @brief 文件偏移量 */
    uint32_t    flags;          /**< @brief 打开标志 */
    uint32_t    mode;           /**< @brief 打开模式 */
} fd_entry_t;

/** @brief 文件描述符表（每进程） */
static fd_entry_t s_fd_table[FS_MAX_FDS];

/* ========================================================================
 * AISafeOS64 内核系统调用号（与 syscall_dispatch.c 保持一致）
 * ======================================================================== */
#define AISAFE_SYS_MSG_SEND             0x0104U
#define AISAFE_SYS_MSG_RECV             0x0105U

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 初始化文件描述符表
 */
static void fs_init_fd_table(void)
{
    uint32_t i;

    for (i = 0U; i < FS_MAX_FDS; i++)
    {
        s_fd_table[i].in_use = false;
        s_fd_table[i].vfs_fd = 0U;
        s_fd_table[i].offset = 0ULL;
        s_fd_table[i].flags = 0U;
        s_fd_table[i].mode = 0U;
    }
}

/**
 * @brief 分配文件描述符
 *
 * @return 文件描述符，-1 表示失败
 */
static int fs_alloc_fd(void)
{
    uint32_t i;

    for (i = 0U; i < FS_MAX_FDS; i++)
    {
        if (!s_fd_table[i].in_use)
        {
            s_fd_table[i].in_use = true;
            s_fd_table[i].vfs_fd = 0U;
            s_fd_table[i].offset = 0ULL;
            s_fd_table[i].flags = 0U;
            s_fd_table[i].mode = 0U;
            return (int)i;
        }
    }

    return -1;  /* EMFILE */
}

/**
 * @brief 释放文件描述符
 *
 * @param fd 文件描述符
 */
static void fs_free_fd(int fd)
{
    if ((fd >= 0) && (fd < (int)FS_MAX_FDS))
    {
        s_fd_table[fd].in_use = false;
    }
}

/**
 * @brief 验证文件描述符
 *
 * @param fd 文件描述符
 *
 * @return 0 表示有效，-1 表示无效
 */
static int fs_validate_fd(int fd)
{
    if ((fd >= 0) && (fd < (int)FS_MAX_FDS))
    {
        if (s_fd_table[fd].in_use)
        {
            return 0;
        }
    }

    return -1;
}

/* ========================================================================
 * IPC 消息定义
 * ======================================================================== */

/**
 * @brief FS 消息类型
 */
typedef enum
{
    FS_MSG_OPEN     = 0x0050U,  /**< @brief 打开文件 */
    FS_MSG_CLOSE    = 0x0051U,  /**< @brief 关闭文件 */
    FS_MSG_READ     = 0x0052U,  /**< @brief 读取文件 */
    FS_MSG_WRITE    = 0x0053U,  /**< @brief 写入文件 */
    FS_MSG_LSEEK    = 0x0054U,  /**< @brief 定位文件 */
    FS_MSG_IOCTL    = 0x0055U,  /**< @brief 文件控制 */
    FS_MSG_FSTAT    = 0x0056U,  /**< @brief 获取文件状态 */
    FS_MSG_FCNTL    = 0x0057U,  /**< @brief 文件控制 */
    FS_MSG_MKDIR    = 0x0058U,  /**< @brief 创建目录 */
    FS_MSG_UNLINK   = 0x0059U,  /**< @brief 删除文件 */
    FS_MSG_RENAME   = 0x005AU,  /**< @brief 重命名 */
    FS_MSG_STATFS   = 0x005BU,  /**< @brief 获取文件系统状态 */
    FS_MSG_GETDENTS = 0x005CU   /**< @brief 读取目录项 */
} fs_msg_type_t;

/**
 * @brief FS 打开消息
 */
typedef struct fs_msg_open
{
    uint64_t    msg_type;       /**< @brief 消息类型：FS_MSG_OPEN */
    uint64_t    dirfd;          /**< @brief 目录文件描述符 */
    char        path[256];      /**< @brief 文件路径 */
    uint64_t    flags;          /**< @brief 打开标志 */
    uint64_t    mode;           /**< @brief 打开模式 */
} fs_msg_open_t;

/**
 * @brief FS 关闭消息
 */
typedef struct fs_msg_close
{
    uint64_t    msg_type;       /**< @brief 消息类型：FS_MSG_CLOSE */
    uint64_t    vfs_fd;         /**< @brief VFS 文件描述符 */
} fs_msg_close_t;

/**
 * @brief FS 读取消息
 */
typedef struct fs_msg_read
{
    uint64_t    msg_type;       /**< @brief 消息类型：FS_MSG_READ */
    uint64_t    vfs_fd;         /**< @brief VFS 文件描述符 */
    uint64_t    count;          /**< @brief 读取字节数 */
} fs_msg_read_t;

/**
 * @brief FS 写入消息
 */
typedef struct fs_msg_write
{
    uint64_t    msg_type;       /**< @brief 消息类型：FS_MSG_WRITE */
    uint64_t    vfs_fd;         /**< @brief VFS 文件描述符 */
    uint64_t    count;          /**< @brief 写入字节数 */
    char        data[4096];     /**< @brief 写入数据 */
} fs_msg_write_t;

/**
 * @brief FS 定位消息
 */
typedef struct fs_msg_lseek
{
    uint64_t    msg_type;       /**< @brief 消息类型：FS_MSG_LSEEK */
    uint64_t    vfs_fd;         /**< @brief VFS 文件描述符 */
    uint64_t    offset;         /**< @brief 偏移量 */
    uint64_t    whence;         /**< @brief 定位方式（SEEK_SET/SEEK_CUR/SEEK_END） */
} fs_msg_lseek_t;

/**
 * @brief FS 应答消息
 */
typedef struct fs_msg_reply
{
    int64_t     ret;            /**< @brief 返回值 */
    uint64_t    vfs_fd;         /**< @brief VFS 文件描述符（open 时返回） */
    uint64_t    offset;         /**< @brief 文件偏移量（lseek 时返回） */
    char        data[4096];     /**< @brief 读取数据（read 时返回） */
    uint64_t    stat_size;      /**< @brief 文件大小（fstat 时返回） */
    uint64_t    stat_mode;      /**< @brief 文件模式（fstat 时返回） */
} fs_msg_reply_t;

/* ========================================================================
 * FS 客户端实现
 * ======================================================================== */

/**
 * @brief FS 初始化
 *
 * @return 0 表示成功，负数表示错误
 */
int fs_client_init(void)
{
    fs_init_fd_table();

    /* TODO: 通过服务发现获取 FS 服务端点 */
    /* s_fs_endpoint = service_discover(SERVICE_FS_MANAGER); */

    /* 临时：使用固定端点（需要根据实际情况调整） */
    s_fs_endpoint = 1;

    return 0;
}

/**
 * @brief 打开文件
 *
 * @param path 文件路径
 * @param flags 打开标志
 * @param mode 打开模式
 *
 * @return 文件描述符，-1 表示失败
 */
int fs_open(const char *path, int flags, mode_t mode)
{
    int fd;
    fs_msg_open_t req;
    fs_msg_reply_t reply;
    long ret;

    /* 参数验证 */
    if (path == NULL)
    {
        return -1;
    }

    /* 分配文件描述符 */
    fd = fs_alloc_fd();
    if (fd < 0)
    {
        return -1;  /* EMFILE */
    }

    /* 构造请求消息 */
    (void)memset(&req, 0, sizeof(req));
    req.msg_type = FS_MSG_OPEN;
    req.dirfd = AT_FDCWD;  /* 当前工作目录 */
    (void)strncpy(req.path, path, sizeof(req.path) - 1U);
    req.flags = (uint64_t)flags;
    req.mode = (uint64_t)mode;

    /* 发送消息到 FS 服务 */
    ret = aisafe_svc_call(
        AISAFE_SYS_MSG_SEND,
        s_fs_endpoint,
        (long)&req,
        sizeof(req),
        0, 0, 0
    );

    if (ret < 0)
    {
        fs_free_fd(fd);
        return -1;
    }

    /* 接收回复 */
    ret = aisafe_svc_call(
        AISAFE_SYS_MSG_RECV,
        s_fs_endpoint,
        (long)&reply,
        sizeof(reply),
        0, 0, 0
    );

    if (ret < 0)
    {
        fs_free_fd(fd);
        return -1;
    }

    /* 检查返回值 */
    if (reply.ret < 0)
    {
        fs_free_fd(fd);
        return (int)reply.ret;
    }

    /* 保存 VFS 文件描述符 */
    s_fd_table[fd].vfs_fd = reply.vfs_fd;
    s_fd_table[fd].flags = (uint32_t)flags;
    s_fd_table[fd].mode = (uint32_t)mode;

    return fd;
}

/**
 * @brief 关闭文件
 *
 * @param fd 文件描述符
 *
 * @return 0 表示成功，-1 表示失败
 */
int fs_close(int fd)
{
    fs_msg_close_t req;
    fs_msg_reply_t reply;
    long ret;

    /* 验证文件描述符 */
    if (fs_validate_fd(fd) != 0)
    {
        return -1;  /* EBADF */
    }

    /* 构造请求消息 */
    (void)memset(&req, 0, sizeof(req));
    req.msg_type = FS_MSG_CLOSE;
    req.vfs_fd = s_fd_table[fd].vfs_fd;

    /* 发送消息到 FS 服务 */
    ret = aisafe_svc_call(
        AISAFE_SYS_MSG_SEND,
        s_fs_endpoint,
        (long)&req,
        sizeof(req),
        0, 0, 0
    );

    if (ret < 0)
    {
        return -1;
    }

    /* 接收回复 */
    ret = aisafe_svc_call(
        AISAFE_SYS_MSG_RECV,
        s_fs_endpoint,
        (long)&reply,
        sizeof(reply),
        0, 0, 0
    );

    if (ret < 0)
    {
        return -1;
    }

    /* 检查返回值 */
    if (reply.ret < 0)
    {
        return (int)reply.ret;
    }

    /* 释放文件描述符 */
    fs_free_fd(fd);

    return 0;
}

/**
 * @brief 读取文件
 *
 * @param fd 文件描述符
 * @param buf 缓冲区
 * @param count 读取字节数
 *
 * @return 实际读取字节数，-1 表示失败
 */
long fs_read(int fd, void *buf, size_t count)
{
    fs_msg_read_t req;
    fs_msg_reply_t reply;
    long ret;

    /* 参数验证 */
    if (fs_validate_fd(fd) != 0)
    {
        return -1;  /* EBADF */
    }

    if (buf == NULL)
    {
        return -1;  /* EFAULT */
    }

    /* 构造请求消息 */
    (void)memset(&req, 0, sizeof(req));
    req.msg_type = FS_MSG_READ;
    req.vfs_fd = s_fd_table[fd].vfs_fd;
    req.count = count;

    /* 发送消息到 FS 服务 */
    ret = aisafe_svc_call(
        AISAFE_SYS_MSG_SEND,
        s_fs_endpoint,
        (long)&req,
        sizeof(req),
        0, 0, 0
    );

    if (ret < 0)
    {
        return -1;
    }

    /* 接收回复 */
    ret = aisafe_svc_call(
        AISAFE_SYS_MSG_RECV,
        s_fs_endpoint,
        (long)&reply,
        sizeof(reply),
        0, 0, 0
    );

    if (ret < 0)
    {
        return -1;
    }

    /* 检查返回值 */
    if (reply.ret < 0)
    {
        return (long)reply.ret;
    }

    /* 复制数据 */
    (void)memcpy(buf, reply.data, (size_t)reply.ret);

    /* 更新偏移量 */
    s_fd_table[fd].offset += (uint64_t)reply.ret;

    return reply.ret;
}

/**
 * @brief 写入文件
 *
 * @param fd 文件描述符
 * @param buf 缓冲区
 * @param count 写入字节数
 *
 * @return 实际写入字节数，-1 表示失败
 */
long fs_write(int fd, const void *buf, size_t count)
{
    fs_msg_write_t req;
    fs_msg_reply_t reply;
    long ret;

    /* 参数验证 */
    if (fs_validate_fd(fd) != 0)
    {
        return -1;  /* EBADF */
    }

    if (buf == NULL)
    {
        return -1;  /* EFAULT */
    }

    /* 构造请求消息 */
    (void)memset(&req, 0, sizeof(req));
    req.msg_type = FS_MSG_WRITE;
    req.vfs_fd = s_fd_table[fd].vfs_fd;
    req.count = count;

    /* 复制数据 */
    if (count > sizeof(req.data))
    {
        count = sizeof(req.data);
    }
    (void)memcpy(req.data, buf, count);

    /* 发送消息到 FS 服务 */
    ret = aisafe_svc_call(
        AISAFE_SYS_MSG_SEND,
        s_fs_endpoint,
        (long)&req,
        sizeof(req),
        0, 0, 0
    );

    if (ret < 0)
    {
        return -1;
    }

    /* 接收回复 */
    ret = aisafe_svc_call(
        AISAFE_SYS_MSG_RECV,
        s_fs_endpoint,
        (long)&reply,
        sizeof(reply),
        0, 0, 0
    );

    if (ret < 0)
    {
        return -1;
    }

    /* 检查返回值 */
    if (reply.ret < 0)
    {
        return (long)reply.ret;
    }

    /* 更新偏移量 */
    s_fd_table[fd].offset += (uint64_t)reply.ret;

    return reply.ret;
}

/* ========================================================================
 * lseek / fstat / ioctl / fcntl 实现
 * ======================================================================== */

/**
 * @brief 文件定位
 *
 * @param fd 文件描述符
 * @param offset 偏移量
 * @param whence 定位方式（SEEK_SET/SEEK_CUR/SEEK_END）
 *
 * @return 新的文件偏移量，-1 表示失败
 */
long fs_lseek(int fd, long offset, int whence)
{
    fs_msg_lseek_t req;
    fs_msg_reply_t reply;
    long ret;

    /* 参数验证 */
    if (fs_validate_fd(fd) != 0)
    {
        return -1;  /* EBADF */
    }

    /* 构造请求消息 */
    (void)memset(&req, 0, sizeof(req));
    req.msg_type = FS_MSG_LSEEK;
    req.vfs_fd = s_fd_table[fd].vfs_fd;
    req.offset = (uint64_t)offset;
    req.whence = (uint64_t)whence;

    /* 发送消息到 FS 服务 */
    ret = aisafe_svc_call(
        AISAFE_SYS_MSG_SEND,
        s_fs_endpoint,
        (long)&req,
        sizeof(req),
        0, 0, 0
    );

    if (ret < 0)
    {
        return -1;
    }

    /* 接收回复 */
    ret = aisafe_svc_call(
        AISAFE_SYS_MSG_RECV,
        s_fs_endpoint,
        (long)&reply,
        sizeof(reply),
        0, 0, 0
    );

    if (ret < 0)
    {
        return -1;
    }

    /* 检查返回值 */
    if (reply.ret < 0)
    {
        return (long)reply.ret;
    }

    /* 更新偏移量 */
    s_fd_table[fd].offset = reply.offset;

    return (long)reply.offset;
}

/**
 * @brief 获取文件状态
 *
 * @param fd 文件描述符
 * @param statbuf stat 结构体指针
 *
 * @return 0 表示成功，-1 表示失败
 */
int fs_fstat(int fd, void *statbuf)
{
    /* TODO: 通过 FS 服务获取文件状态 */
    /* 当前实现：返回 -ENOSYS */
    (void)fd;
    (void)statbuf;
    return -ENOSYS;
}

/**
 * @brief 文件控制
 *
 * @param fd 文件描述符
 * @param request 控制请求
 * @param arg 参数
 *
 * @return 0 表示成功，-1 表示失败
 */
int fs_ioctl(int fd, unsigned long request, void *arg)
{
    /* TODO: 通过 FS 服务执行 ioctl */
    /* 当前实现：返回 -ENOSYS */
    (void)fd;
    (void)request;
    (void)arg;
    return -ENOSYS;
}

/**
 * @brief 文件描述符控制
 *
 * @param fd 文件描述符
 * @param cmd 控制命令
 * @param arg 参数
 *
 * @return 0 表示成功，-1 表示失败
 */
int fs_fcntl(int fd, int cmd, int arg)
{
    /* TODO: 通过 FS 服务执行 fcntl */
    /* 当前实现：返回 -ENOSYS */
    (void)fd;
    (void)cmd;
    (void)arg;
    return -ENOSYS;
}
