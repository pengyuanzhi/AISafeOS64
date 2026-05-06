/**
 * @file    ext4_file.c
 * @brief   Ext4 文件操作实现
 * @author  AISafe64 Team
 * @date    2026-05-06
 * @version 1.0
 *
 * @details Ext4 文件操作实现：
 *          - 打开/关闭文件
 *          - 读取/写入文件
 *          - 创建/删除文件
 *
 * @note MISRA-C:2012 合规
 * @note TDD: GREEN 阶段 - 最小实现
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "ext4_file.h"
#include "ext4_inode.h"
#include "ext4_dir.h"
#include "ext4_permission.h"
#include <string.h>
#include <stdlib.h>

/* ========================================================================
 * 内部变量
 * ======================================================================== */

/** @brief 文件描述符表 */
static ext4_fd_t s_fd_table[EXT4_MAX_FD_COUNT];
static uint32_t  s_next_fd = 0U;

/* ========================================================================
 * 内部辅助函数
 * ======================================================================== */

/**
 * @brief 查找文件描述符
 *
 * @param fd        文件描述符
 *
 * @return 文件描述符指针，NULL 表示无效
 */
static ext4_fd_t *find_fd(int32_t fd)
{
    if (fd < 0 || (uint32_t)fd >= EXT4_MAX_FD_COUNT)
    {
        return NULL;
    }

    return &s_fd_table[fd];
}

/**
 * @brief 分配文件描述符
 *
 * @return 文件描述符（>=0 成胜），<0 失败
 */
static int32_t alloc_fd(void)
{
    uint32_t i;

    for (i = 0U; i < EXT4_MAX_FD_COUNT; i++)
    {
        if (s_fd_table[i].state == EXT4_FD_UNUSED)
        {
            s_fd_table[i].state = EXT4_FD_OPEN;
            s_fd_table[i].inode = 0U;
            s_fd_table[i].offset = 0U;
            s_fd_table[i].flags = 0U;
            return (int32_t)i;
        }
    }

    return -9; /* EMFILE */
}

/**
 * @brief 释放文件描述符
 *
 * @param fd        文件描述符
 */
static void free_fd(int32_t fd)
{
    if (fd >= 0 && (uint32_t)fd < EXT4_MAX_FD_COUNT)
    {
        s_fd_table[fd].state = EXT4_FD_UNUSED;
        s_fd_table[fd].inode = 0U;
    }
}

/**
 * @brief 检查文件名
 *
 * @param path      文件路径
 *
 * @return 0 成功，<0 失败
 */
static int32_t check_filename(const char *path)
{
    if (path == NULL || path[0] == 0 || path[0] == '/')
    {
        return -22; /* EINVAL */
    }

    if (strlen(path) >= 255U)
    {
        return -22; /* EINVAL */
    }

    return 0;
}

/* ========================================================================
 * 文件操作接口实现
 * ======================================================================== */

/**
 * @brief 打开文件
 *
 * @param path      文件路径
 * @param flags     文件标志
 * @param mode      权限模式
 * @param uid       用户 ID
 * @param gid       组 ID
 *
 * @return 文件描述符（>=0 成胜），<0 失败
 */
int32_t ext4_open(const char *path, uint32_t flags,
                   uint32_t mode, uint32_t uid, uint32_t gid)
{
    ext4_dir_entry_t entry;
    int32_t ret;

    /* 检查文件名 */
    ret = check_filename(path);
    if (ret != 0)
    {
        return ret;
    }

    /* 尝试查找文件 */
    ret = ext4_lookup(1U, path, &entry); /* 查找根目录 */

    if (ret == 0) /* 文件存在 */
    {
        /* 检查权限 */
        if (!ext4_check_permission(entry.inode, uid, gid, flags))
        {
            return -13; /* EACCES */
        }

        /* 检查是否为文件 */
        if (entry.file_type != 1U) /* NOT REG_FILE */
        {
            return -20; /* ENOTDIR */
        }

        /* 分配文件描述符 */
        int32_t fd = alloc_fd();
        if (fd < 0)
        {
            return -9; /* EMFILE */
        }

        s_fd_table[fd].inode = entry.inode;
        s_fd_table[fd].offset = 0U;
        s_fd_table[fd].flags = flags;

        return fd;
    }
    else if (ret == -2) /* ENOENT */
    {
        /* 检查 O_CREAT 标志 */
        if ((flags & O_CREAT) != 0U)
        {
            /* 创建文件 */
            ret = ext4_create(path, mode, uid, gid);
            if (ret != 0)
            {
                return ret;
            }

            /* 重新查找文件 */
            ret = ext4_lookup(1U, path, &entry);
            if (ret != 0)
            {
                return -2; /* ENOENT */
            }

            /* 分配文件描述符 */
            int32_t fd = alloc_fd();
            if (fd < 0)
            {
                return -9; /* EMFILE */
            }

            s_fd_table[fd].inode = entry.inode;
            s_fd_table[fd].offset = 0U;
            s_fd_table[fd].flags = flags;

            return fd;
        }
        else
        {
            return -2; /* ENOENT */
        }
    }
    else
    {
        return ret;
    }
}

/**
 * @brief 关闭文件
 *
 * @param fd        文件描述符
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_close(int32_t fd)
{
    ext4_fd_t *fd_entry = find_fd(fd);

    if (fd_entry == NULL)
    {
        return -9; /* EBADF */
    }

    if (fd_entry->state != EXT4_FD_OPEN)
    {
        return -9; /* EBADF */
    }

    /* 省略文件刷新 */
    free_fd(fd);

    return 0;
}

/**
 * @brief 读取文件
 *
 * @param fd        文件描述符
 * @param buf       输入缓冲区
 * @param count     读取字节数
 *
 * @return 实际读取字节数（>=0 成胜），<0 失败
 */
int32_t ext4_read(int32_t fd, void *buf, uint32_t count)
{
    ext4_fd_t *fd_entry = find_fd(fd);
    ext4_inode_t inode;
    uint32_t file_size;
    uint32_t to_read;
    uint32_t read_count;
    int32_t ret;

    if (fd_entry == NULL)
    {
        return -9; /* EBADF */
    }

    if (buf == NULL)
    {
        return -22; /* EINVAL */
    }

    if (count == 0U)
    {
        return 0;
    }

    /* 读取 Inode */
    ret = ext4_read_inode(fd_entry->inode, &inode);

    if (ret != 0)
    {
        return ret;
    }

    /* 获取文件大小 */
    file_size = inode.i_size;

    /* 计算要读取的字节数 */
    to_read = (fd_entry->offset + count > file_size) ?
              (file_size - fd_entry->offset) : count;

    if (to_read == 0U)
    {
        return 0;
    }

    /* 读取文件内容（模拟） */
    /* 省略具体实现 */

    read_count = to_read;

    /* 更新偏移 */
    fd_entry->offset += read_count;

    return (int32_t)read_count;
}

/**
 * @brief 写入文件
 *
 * @param fd        文件描述符
 * @param buf       输出缓冲区
 * @param count     写入字节数
 *
 * @return 实际写入字节数（>=0 成胜），<0 失败
 */
int32_t ext4_write(int32_t fd, const void *buf, uint32_t count)
{
    ext4_fd_t *fd_entry = find_fd(fd);
    ext4_inode_t inode;
    int32_t ret;

    if (fd_entry == NULL)
    {
        return -9; /* EBADF */
    }

    if (buf == NULL)
    {
        return -22; /* EINVAL */
    }

    /* 读取 Inode */
    ret = ext4_read_inode(fd_entry->inode, &inode);

    if (ret != 0)
    {
        return ret;
    }

    /* 检查是否为写模式 */
    if ((fd_entry->flags & (O_WRONLY | O_RDWR)) == 0U)
    {
        return -13; /* EACCES */
    }

    /* 写入文件内容（模拟） */
    /* 省略具体实现 */

    /* 更新文件大小 */
    inode.i_size = fd_entry->offset + count;
    inode.i_mtime = 0U; /* 当前时间 */
    inode.i_blocks += (count + 4095U) / 4096U;

    /* 写回 Inode */
    ret = ext4_write_inode(fd_entry->inode, &inode);

    if (ret != 0)
    {
        return ret;
    }

    /* 更新偏移 */
    fd_entry->offset += count;

    return (int32_t)count;
}

/**
 * @brief 创建文件
 *
 * @param path      文件路径
 * @param mode      权限模式
 * @param uid       用户 ID
 * @param gid       组 ID
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_create(const char *path, uint32_t mode,
                     uint32_t uid, uint32_t gid)
{
    int32_t ret;

    /* 检查文件名 */
    ret = check_filename(path);
    if (ret != 0)
    {
        return ret;
    }

    /* 检查是否已存在 */
    ext4_dir_entry_t entry;
    ret = ext4_lookup(1U, path, &entry);

    if (ret == 0)
    {
        return -17; /* EEXIST */
    }

    /* 分配 Inode */
    uint32_t ino = ext4_alloc_inode(mode, uid, gid);
    if (ino == 0U)
    {
        return -12; /* ENOMEM */
    }

    /* 初始化 Inode */
    ext4_inode_t inode;
    inode.i_mode = mode;
    inode.i_uid = uid;
    inode.i_gid = gid;
    inode.i_size = 0U;
    inode.i_blocks = 0U;
    inode.i_links_count = 1U;
    inode.i_atime = 0U;
    inode.i_ctime = 0U;
    inode.i_mtime = 0U;

    ext4_write_inode(ino, &inode);

    /* 在根目录中添加条目 */
    ext4_dir_entry_t root_entry;
    root_entry.inode = ino;
    root_entry.rec_len = 16U;
    root_entry.name_len = (uint8_t)strlen(path);
    root_entry.file_type = 1U; /* REG_FILE */
    (void)strncpy(root_entry.name, path, EXT4_DIR_NAME_LEN);

    /* 添加到根目录（模拟） */
    /* 省略具体实现 */

    return 0;
}

/**
 * @brief 删除文件
 *
 * @param path      文件路径
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_unlink(const char *path)
{
    ext4_dir_entry_t entry;
    int32_t ret;

    /* 检查文件名 */
    if (path == NULL || path[0] == 0 || path[0] == '/')
    {
        return -22; /* EINVAL */
    }

    /* 查找文件 */
    ret = ext4_lookup(1U, path, &entry);

    if (ret != 0)
    {
        return -2; /* ENOENT */
    }

    /* 检查是否为文件 */
    if (entry.file_type != 1U) /* NOT REG_FILE */
    {
        return -20; /* ENOTDIR */
    }

    /* 释放 Inode */
    ext4_free_inode(entry.inode);

    /* 删除目录项（模拟） */
    /* 省略具体实现 */

    return 0;
}

/**
 * @brief 获取文件状态
 *
 * @param fd        文件描述符
 * @param size      输出文件大小
 *
 * @return 0 成功，<0 失败
 */
int32_t ext4_fstat(int32_t fd, uint32_t *size)
{
    ext4_fd_t *fd_entry = find_fd(fd);
    ext4_inode_t inode;
    int32_t ret;

    if (fd_entry == NULL)
    {
        return -9; /* EBADF */
    }

    if (size == NULL)
    {
        return -22; /* EINVAL */
    }

    /* 读取 Inode */
    ret = ext4_read_inode(fd_entry->inode, &inode);

    if (ret != 0)
    {
        return ret;
    }

    *size = inode.i_size;

    return 0;
}
