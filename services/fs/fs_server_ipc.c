/**
 * @file    fs_server_ipc.c
 * @brief   FS 服务 IPC 服务器端实现
 * @author  AISafe64 Team
 * @date    2026-04-30
 * @version 3.0
 *
 * @details FS 服务 IPC 服务器端实现：
 *          - IPC 通道创建和管理
 *          - IPC 消息接收和分发
 *          - 文件系统操作处理
 *          - 路径解析和权限检查
 *
 * @note MISRA-C:2012 合规
 * @note 使用内核 IPC 系统调用
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "fs_ops.h"
#include "fs_ipc_types.h"
#include <kernel/fs_ipc.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ========================================================================
 * 前向声明
 * ======================================================================== */

/**
 * @brief SVC 调用辅助函数（3 参数版本）
 */
static inline int64_t svc_call_3(uint64_t nr, int64_t a0, int64_t a1, int64_t a2);

/* ========================================================================
 * 内核 IPC 系统调用接口
 * ======================================================================== */

/** @brief IPC 通道 ID */
static uint64_t s_fs_channel_id = 0U;

/** @brief IPC 连接 ID */
static uint64_t s_fs_conn_id = 0U;

/* 内核 IPC 系统调用号（与 include/kernel/syscall.h 一致） */
#define AISAFE_SYS_CHANNEL_CREATE       0x0100U
#define AISAFE_SYS_CHANNEL_DESTROY      0x0101U
#define AISAFE_SYS_CONNECT_ATTACH       0x0102U
#define AISAFE_SYS_MSG_SEND             0x0104U
#define AISAFE_SYS_MSG_RECV             0x0105U
#define AISAFE_SYS_MSG_REPLY            0x0106U

/**
 * @brief 通过 SVC 调用内核 IPC 接收
 *
 * @param buf     接收缓冲区
 * @param size    缓冲区大小
 * @param msg_id_out 输出消息 ID
 *
 * @return 接收到的字节数，负数表示错误
 */
static int32_t fs_ipc_recv(void *buf, uint64_t size, uint64_t *msg_id_out)
{
    int64_t ret;

    if (buf == NULL || msg_id_out == NULL)
    {
        return -14;  /* -EFAULT */
    }

    /* 调用内核 SYS_MSG_RECV 系统调用 */
    ret = svc_call_3(AISAFE_SYS_MSG_RECV,
                     (int64_t)s_fs_channel_id,
                     (int64_t)buf,
                     (int64_t)size);

    if (ret < 0)
    {
        return (int32_t)ret;
    }

    /* 从消息头提取消息 ID */
    *msg_id_out = (uint64_t)ret;

    return (int32_t)ret;
}

/**
 * @brief 通过 SVC 调用内核 IPC 回复
 *
 * @param msg_id  消息 ID
 * @param buf     回复缓冲区
 * @param size    回复大小
 *
 * @return 0 成功，负数表示错误
 */
static int32_t fs_ipc_reply(uint64_t msg_id, const void *buf, uint64_t size)
{
    int64_t ret;

    if (buf == NULL)
    {
        return -14;  /* -EFAULT */
    }

    /* 调用内核 SYS_MSG_REPLY 系统调用 */
    ret = svc_call_3(AISAFE_SYS_MSG_REPLY,
                     (int64_t)msg_id,
                     (int64_t)buf,
                     (int64_t)size);

    return (int32_t)ret;
}

/**
 * @brief SVC 调用辅助宏（3 参数版本）
 *
 * @param nr   系统调用号
 * @param a0   参数 0
 * @param a1   参数 1
 * @param a2   参数 2
 *
 * @return 系统调用返回值
 */
static inline int64_t svc_call_3(uint64_t nr, int64_t a0, int64_t a1, int64_t a2)
{
    register uint64_t x8 __asm__("x8") = nr;
    register uint64_t x0 __asm__("x0") = (uint64_t)a0;
    register uint64_t x1 __asm__("x1") = (uint64_t)a1;
    register uint64_t x2 __asm__("x2") = (uint64_t)a2;

    __asm__ volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2)
        : "memory"
    );

    return (int64_t)x0;
}

/* ========================================================================
 * 文件描述符表（服务器端）
 * ======================================================================== */

/** @brief 服务器端文件描述符表 */
static struct
{
    bool        in_use;         /**< @brief 是否在使用 */
    uint32_t    ino;            /**< @brief inode 编号 */
    uint32_t    flags;          /**< @brief 打开标志 */
    uint64_t    offset;         /**< @brief 当前偏移 */
    uint32_t    mount_id;       /**< @brief 挂载点 ID */
} s_fd_table[32U];

/** @brief 初始化标志 */
static bool s_initialized = false;

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 初始化文件描述符表
 */
static void fs_server_init_fd_table(void)
{
    uint32_t i;

    for (i = 0U; i < 32U; i++)
    {
        s_fd_table[i].in_use = false;
        s_fd_table[i].ino = 0U;
        s_fd_table[i].flags = 0U;
        s_fd_table[i].offset = 0ULL;
        s_fd_table[i].mount_id = 0U;
    }
}

/**
 * @brief 分配文件描述符
 */
static uint32_t fs_server_alloc_fd(void)
{
    uint32_t i;

    for (i = 0U; i < 32U; i++)
    {
        if (!s_fd_table[i].in_use)
        {
            s_fd_table[i].in_use = true;
            s_fd_table[i].ino = 0U;
            s_fd_table[i].flags = 0U;
            s_fd_table[i].offset = 0ULL;
            return i;
        }
    }

    return 0xFFFFFFFFU;
}

/**
 * @brief 释放文件描述符
 */
static void fs_server_free_fd(uint32_t fd)
{
    if (fd < 32U)
    {
        s_fd_table[fd].in_use = false;
    }
}

/* ========================================================================
 * 文件系统操作实现（集成 RAMFS）
 * ======================================================================== */

/**
 * @brief 打开文件
 */
static int32_t fs_server_open(const char *path, uint32_t flags, uint32_t mode)
{
    uint32_t fd;
    int32_t ret;

    (void)mode;  /* RAMFS 暂不支持创建模式 */

    /* 调用 RAMFS 打开文件 */
    ret = fs_open(path, flags, mode);
    if (ret < 0)
    {
        return ret;
    }

    fd = fs_server_alloc_fd();
    if (fd == 0xFFFFFFFFU)
    {
        return -EMFILE;
    }

    /* 保存文件描述符信息 */
    s_fd_table[fd].ino = (uint32_t)ret;
    s_fd_table[fd].flags = flags;
    s_fd_table[fd].offset = 0ULL;

    return (int32_t)fd;
}

/**
 * @brief 关闭文件
 */
static int32_t fs_server_close(uint32_t fd)
{
    int32_t ret;

    if (fd >= 32U)
    {
        return -EBADF;
    }

    if (!s_fd_table[fd].in_use)
    {
        return -EBADF;
    }

    /* 调用 RAMFS 关闭文件 */
    ret = fs_close(fd);
    if (ret < 0)
    {
        return ret;
    }

    fs_server_free_fd(fd);

    return 0;
}

/**
 * @brief 读取文件
 */
static int64_t fs_server_read(uint32_t fd, uint64_t offset, void *buf, uint64_t size)
{
    if (fd >= 32U)
    {
        return -EBADF;
    }

    if (!s_fd_table[fd].in_use)
    {
        return -EBADF;
    }

    if ((s_fd_table[fd].flags & 0x02U) == 0x02U)  /* O_RDONLY */
    {
        return -EBADF;
    }

    if (buf == NULL)
    {
        return -EINVAL;
    }

    /* 更新偏移 */
    s_fd_table[fd].offset = offset;

    /* 调用 RAMFS 读取文件 */
    return fs_read(fd, buf, size);
}

/**
 * @brief 写入文件
 */
static int64_t fs_server_write(uint32_t fd, uint64_t offset, const void *buf, uint64_t size)
{
    if (fd >= 32U)
    {
        return -EBADF;
    }

    if (!s_fd_table[fd].in_use)
    {
        return -EBADF;
    }

    if ((s_fd_table[fd].flags & 0x01U) == 0x01U)  /* O_WRONLY */
    {
        return -EBADF;
    }

    if (buf == NULL)
    {
        return -EINVAL;
    }

    /* 更新偏移 */
    s_fd_table[fd].offset = offset;

    /* 调用 RAMFS 写入文件 */
    return fs_write(fd, buf, size);
}

/**
 * @brief 定位文件偏移
 */
static int64_t fs_server_lseek(uint32_t fd, int64_t offset, uint32_t whence)
{
    int64_t new_offset;

    if (fd >= 32U)
    {
        return -EBADF;
    }

    if (!s_fd_table[fd].in_use)
    {
        return -EBADF;
    }

    switch (whence)
    {
        case FS_SEEK_SET:  /* SEEK_SET */
            new_offset = offset;
            break;

        case FS_SEEK_CUR:  /* SEEK_CUR */
            new_offset = (int64_t)s_fd_table[fd].offset + offset;
            break;

        case FS_SEEK_END:  /* SEEK_END */
        {
            fs_stat_t stat;
            int32_t ret = fs_fstat(fd, &stat);
            if (ret < 0)
            {
                return ret;
            }
            new_offset = (int64_t)stat.st_size + offset;
            break;
        }

        default:
            return -EINVAL;
    }

    if (new_offset < 0)
    {
        return -EINVAL;
    }

    s_fd_table[fd].offset = (uint64_t)new_offset;

    return new_offset;
}

/**
 * @brief 获取文件状态
 */
static int32_t fs_server_fstat(uint32_t fd, fs_stat_t *stat)
{
    if (fd >= 32U)
    {
        return -EBADF;
    }

    if (!s_fd_table[fd].in_use)
    {
        return -EBADF;
    }

    if (stat == NULL)
    {
        return -EINVAL;
    }

    /* 调用 RAMFS 获取文件状态 */
    return fs_fstat(fd, stat);
}

/* ========================================================================
 * IPC 消息处理
 * ======================================================================== */

/**
 * @brief 处理 FS IPC 消息
 */
static void fs_server_handle_ipc_msg(const fs_ipc_msg_header_t *header,
                                     const void *req_data,
                                     uint32_t req_size,
                                     void *resp_data,
                                     uint32_t *resp_size)
{
    fs_ipc_msg_header_t resp_header;
    uint32_t resp_header_size;

    /* 初始化响应头 */
    resp_header.msg_type = header->msg_type;
    resp_header.msg_id = header->msg_id;
    resp_header.status = 0;
    resp_header.reserved = 0;

    /* 复制响应头到响应缓冲区 */
    (void)memcpy(resp_data, &resp_header, sizeof(fs_ipc_msg_header_t));
    resp_header_size = sizeof(fs_ipc_msg_header_t);

    /* 根据消息类型分发处理 */
    switch (header->msg_type)
    {
        case FS_IPC_OPEN:
        {
            const fs_ipc_open_req_t *req = (const fs_ipc_open_req_t *)req_data;
            fs_ipc_open_resp_t *resp = (fs_ipc_open_resp_t *)resp_data;

            int32_t fd = fs_server_open(req->path, req->flags, req->mode);
            resp->header.msg_type = header->msg_type;
            resp->header.msg_id = header->msg_id;
            resp->header.status = 0;
            resp->header.reserved = 0;
            resp->result = fd;

            *resp_size = sizeof(fs_ipc_open_resp_t);
            break;
        }

        case FS_IPC_CLOSE:
        {
            const fs_ipc_close_req_t *req = (const fs_ipc_close_req_t *)req_data;
            fs_ipc_close_resp_t *resp = (fs_ipc_close_resp_t *)resp_data;

            int32_t result = fs_server_close(req->fd);
            resp->header.msg_type = header->msg_type;
            resp->header.msg_id = header->msg_id;
            resp->header.status = 0;
            resp->header.reserved = 0;
            resp->result = result;

            *resp_size = sizeof(fs_ipc_close_resp_t);
            break;
        }

        case FS_IPC_READ:
        {
            const fs_ipc_read_req_t *req = (const fs_ipc_read_req_t *)req_data;
            fs_ipc_read_resp_t *resp = (fs_ipc_read_resp_t *)resp_data;

            int64_t bytes = fs_server_read(req->fd, req->offset,
                                            resp->data, req->size);
            resp->header.msg_type = header->msg_type;
            resp->header.msg_id = header->msg_id;
            resp->header.status = 0;
            resp->header.reserved = 0;
            resp->result = bytes;

            *resp_size = sizeof(fs_ipc_read_resp_t);
            if (bytes > 0)
            {
                *resp_size += (uint32_t)bytes - 4096U;  /* 调整大小 */
            }
            break;
        }

        case FS_IPC_WRITE:
        {
            const fs_ipc_write_req_t *req = (const fs_ipc_write_req_t *)req_data;
            fs_ipc_write_resp_t *resp = (fs_ipc_write_resp_t *)resp_data;

            int64_t bytes = fs_server_write(req->fd, req->offset,
                                             req->data, req->size);
            resp->header.msg_type = header->msg_type;
            resp->header.msg_id = header->msg_id;
            resp->header.status = 0;
            resp->header.reserved = 0;
            resp->result = bytes;

            *resp_size = sizeof(fs_ipc_write_resp_t);
            break;
        }

        case FS_IPC_LSEEK:
        {
            const fs_ipc_lseek_req_t *req = (const fs_ipc_lseek_req_t *)req_data;
            fs_ipc_lseek_resp_t *resp = (fs_ipc_lseek_resp_t *)resp_data;

            int64_t offset = fs_server_lseek(req->fd, req->offset, req->whence);
            resp->header.msg_type = header->msg_type;
            resp->header.msg_id = header->msg_id;
            resp->header.status = 0;
            resp->header.reserved = 0;
            resp->result = offset;

            *resp_size = sizeof(fs_ipc_lseek_resp_t);
            break;
        }

        case FS_IPC_FSTAT:
        {
            const fs_ipc_fstat_req_t *req = (const fs_ipc_fstat_req_t *)req_data;
            fs_ipc_fstat_resp_t *resp = (fs_ipc_fstat_resp_t *)resp_data;
            fs_stat_t stat;

            int32_t result = fs_server_fstat(req->fd, &stat);
            resp->header.msg_type = header->msg_type;
            resp->header.msg_id = header->msg_id;
            resp->header.status = 0;
            resp->header.reserved = 0;
            resp->result = result;

            if (result == 0)
            {
                resp->st_dev = stat.st_dev;
                resp->st_ino = stat.st_ino;
                resp->st_mode = stat.st_mode;
                resp->st_nlink = stat.st_nlink;
                resp->st_uid = stat.st_uid;
                resp->st_gid = stat.st_gid;
                resp->st_size = stat.st_size;
                resp->st_atime = stat.st_atime;
                resp->st_mtime = stat.st_mtime;
                resp->st_ctime = stat.st_ctime;
            }

            *resp_size = sizeof(fs_ipc_fstat_resp_t);
            break;
        }

        default:
            /* 未知消息类型 */
            break;
    }

    (void)req_size;  /* 未使用 */
}

/* ========================================================================
 * FS 服务器初始化
 * ======================================================================== */

/**
 * @brief 初始化 FS 服务器
 */
void fs_server_init(void)
{
    int64_t ret;

    if (!s_initialized)
    {
        fs_server_init_fd_table();

        /* 创建 IPC 通道 */
        ret = svc_call_3(AISAFE_SYS_CHANNEL_CREATE, 0, 0, 0);
        if (ret >= 0)
        {
            s_fs_channel_id = (uint64_t)ret;
        }

        s_initialized = true;
    }
}

/* ========================================================================
 * FS 服务器主循环
 * ======================================================================== */

/**
 * @brief FS 服务器主函数
 */
void fs_server_main(void)
{
    uint8_t recv_buf[4096];
    uint8_t send_buf[4096];
    int32_t ret;
    uint64_t msg_id;

    fs_server_init();

    printf("[FS Server] Starting FS service...\n");

    for (;;)
    {
        /* 接收 IPC 消息 */
        ret = fs_ipc_recv(recv_buf, sizeof(recv_buf), &msg_id);

        if (ret < 0)
        {
            /* 没有消息，短暂延迟后继续 */
            continue;
        }

        /* 处理消息 */
        fs_ipc_msg_header_t *header = (fs_ipc_msg_header_t *)recv_buf;
        uint32_t resp_size = sizeof(send_buf);

        fs_server_handle_ipc_msg(header, recv_buf + sizeof(fs_ipc_msg_header_t),
                                   (uint32_t)ret - sizeof(fs_ipc_msg_header_t),
                                   send_buf, &resp_size);

        /* 发送回复 */
        fs_ipc_reply(msg_id, send_buf, resp_size);
    }
}
