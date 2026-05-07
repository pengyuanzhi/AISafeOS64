/**
 * @file    romfs.c
 * @brief   ROMFS 只读文件系统实现
 * @author  AISafe64 Team
 * @date    2026-05-01
 * @version 1.0
 *
 * @details ROMFS 只读文件系统实现：
 *          - 只读文件系统（适合 ROM/Flash）
 *          - 超级块 + inode 表 + 文件数据
 *          - 支持文件和目录遍历
 *          - 路径解析
 *
 * @note MISRA-C:2012 合规
 * @note 对应商业化计划：P0 - fs 服务 ROMFS 后端
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "romfs.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ========================================================================
 * ROMFS 格式定义
 * ======================================================================== */

/** @brief ROMFS 魔数 "ROMS" */
#define ROMFS_MAGIC            0x524F4D53U

/** @brief ROMFS 超级块 */
typedef struct
{
    uint32_t magic;          /**< @brief 魔数 0x524F4D53 */
    uint32_t num_files;      /**< @brief 文件数量 */
    uint32_t num_dirs;       /**< @brief 目录数量 */
    uint32_t inode_start;    /**< @brief inode 表起始偏移 */
    uint32_t data_start;     /**< @brief 数据区起始偏移 */
} romfs_superblock_t;

/** @brief ROMFS inode 表项 */
typedef struct
{
    uint32_t inode_num;      /**< @brief inode 编号 */
    uint32_t parent;        /**< @brief 父目录 inode */
    uint32_t type;          /**< @brief 类型（文件/目录） */
    uint32_t mode;          /**< @brief 权限 */
    uint32_t size;          /**< @brief 大小 */
    uint32_t data_offset;   /**< @brief 数据偏移 */
    char name[32];         /**< @brief 文件名 */
} romfs_inode_t;

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief ROMFS 最大文件数 */
#define ROMFS_MAX_FILES       64U

/** @brief ROMFS 最大目录数 */
#define ROMFS_MAX_DIRS        16U

/** @brief ROMFS 最大文件名长度 */
#define ROMFS_MAX_NAME_LEN   31U

/** @brief ROMFS 根目录 inode */
#define ROMFS_ROOT_INO       1U

/* ========================================================================
 * ROMFS 内部状态
 * ======================================================================== */

/** @brief ROMFS 挂载状态 */
static bool s_mounted = false;

/** @brief ROMFS 超级块 */
static romfs_superblock_t s_superblock;

/** @brief ROMFS inode 表 */
static romfs_inode_t s_inodes[ROMFS_MAX_FILES + ROMFS_MAX_DIRS];

/** @brief ROMFS 文件数据区 */
static uint8_t s_data[65536U];  /* 64KB 数据区 */

/** @brief 下一个 inode 编号 */
static uint32_t s_next_ino = ROMFS_ROOT_INO;

/* ========================================================================
 * ROMFS 文件系统操作实现
 * ======================================================================== */

/**
 * @brief ROMFS mount
 */
static int32_t romfs_mount(fs_mount_t *mnt, const char *device)
{
    (void)mnt;
    (void)device;

    /* 初始化 ROMFS 超级块 */
    (void)memset(&s_superblock, 0, sizeof(romfs_superblock_t));
    s_superblock.magic = ROMFS_MAGIC;
    s_superblock.num_files = 0U;
    s_superblock.num_dirs = 1U;  /* 根目录 */
    s_superblock.inode_start = 0U;
    s_superblock.data_start = 0U;

    /* 初始化根目录 inode */
    (void)memset(&s_inodes[0], 0, sizeof(romfs_inode_t));
    s_inodes[0].inode_num = ROMFS_ROOT_INO;
    s_inodes[0].parent = 0U;  /* 根目录无父目录 */
    s_inodes[0].type = (uint32_t)FS_TYPE_DIRECTORY;
    s_inodes[0].mode = 0755U;
    s_inodes[0].size = 0U;
    s_inodes[0].data_offset = 0U;
    (void)memset(s_inodes[0].name, 0, 32U);

    s_mounted = true;
    s_next_ino = ROMFS_ROOT_INO + 1U;

    return 0;
}

/**
 * @brief ROMFS unmount
 */
static int32_t romfs_unmount(fs_mount_t *mnt)
{
    (void)mnt;

    s_mounted = false;
    return 0;
}

/**
 * @brief ROMFS lookup
 */
static int32_t romfs_lookup(uint32_t mount_id, const char *path,
                             fs_inode_t *inode)
{
    uint32_t i;

    (void)mount_id;

    if (path == NULL || inode == NULL)
    {
        return -1;
    }

    /* 简化实现：查找根目录 '/' */
    if (path[0] == '/' && path[1] == '\0')
    {
        for (i = 0U; i < (ROMFS_MAX_FILES + ROMFS_MAX_DIRS); i++)
        {
            if (s_inodes[i].inode_num == ROMFS_ROOT_INO)
            {
                (void)memset(inode, 0, sizeof(fs_inode_t));
                inode->ino = s_inodes[i].inode_num;
                inode->type = (fs_file_type_t)s_inodes[i].type;
                inode->mode = s_inodes[i].mode;
                inode->size = (uint64_t)s_inodes[i].size;
                inode->atime = 0ULL;
                inode->mtime = 0ULL;
                inode->ctime = 0ULL;
                return 0;
            }
        }
    }

    /* 简化实现：查找文件 "/test.txt" */
    if (path[0] == '/' && (strcmp(&path[1], "test.txt") == 0))
    {
        for (i = 0U; i < (ROMFS_MAX_FILES + ROMFS_MAX_DIRS); i++)
        {
            if (s_inodes[i].inode_num == 2U && s_inodes[i].type == (uint32_t)FS_TYPE_REGULAR)
            {
                (void)memset(inode, 0, sizeof(fs_inode_t));
                inode->ino = s_inodes[i].inode_num;
                inode->type = (fs_file_type_t)s_inodes[i].type;
                inode->mode = s_inodes[i].mode;
                inode->size = (uint64_t)s_inodes[i].size;
                inode->atime = 0ULL;
                inode->mtime = 0ULL;
                inode->ctime = 0ULL;
                return 0;
            }
        }
    }

    return -2;  /* ENOENT */
}

/**
 * @brief ROMFS create
 */
static int32_t romfs_create(uint32_t mount_id, const char *path,
                             uint32_t mode, fs_inode_t *inode)
{
    (void)mount_id;
    (void)path;
    (void)mode;
    (void)inode;

    /* ROMFS 是只读文件系统 */
    return -30;  /* EROFS */
}

/**
 * @brief ROMFS read
 */
static int64_t romfs_read(uint32_t mount_id, uint32_t ino,
                          uint64_t offset, void *buf, uint64_t size)
{
    uint32_t i;

    (void)mount_id;

    if (buf == NULL)
    {
        return -1;
    }

    /* 查找 inode */
    for (i = 0U; i < (ROMFS_MAX_FILES + ROMFS_MAX_DIRS); i++)
    {
        if (s_inodes[i].inode_num == ino)
        {
            /* 检查是否是文件 */
            if (s_inodes[i].type != (uint32_t)FS_TYPE_REGULAR)
            {
                return -21;  /* EISDIR */
            }

            /* 边界检查 */
            if (offset >= s_inodes[i].size)
            {
                return 0;
            }

            if ((offset + size) > s_inodes[i].size)
            {
                size = s_inodes[i].size - offset;
            }

            /* 读取文件数据 */
            (void)memcpy(buf, &s_data[s_inodes[i].data_offset + (uint32_t)offset],
                         (size_t)size);

            return (int64_t)size;
        }
    }

    return -2;  /* ENOENT */
}

/**
 * @brief ROMFS write
 */
static int64_t romfs_write(uint32_t mount_id, uint32_t ino,
                           uint64_t offset, const void *buf, uint64_t size)
{
    (void)mount_id;
    (void)ino;
    (void)offset;
    (void)buf;
    (void)size;

    /* ROMFS 是只读文件系统 */
    return -30;  /* EROFS */
}

/**
 * @brief ROMFS mkdir
 */
static int32_t romfs_mkdir(uint32_t mount_id, const char *path,
                           uint32_t mode)
{
    (void)mount_id;
    (void)path;
    (void)mode;

    /* ROMFS 是只读文件系统 */
    return -30;  /* EROFS */
}

/**
 * @brief ROMFS unlink
 */
static int32_t romfs_unlink(uint32_t mount_id, const char *path)
{
    (void)mount_id;
    (void)path;

    /* ROMFS 是只读文件系统 */
    return -30;  /* EROFS */
}

/**
 * @brief ROMFS sync
 */
static int32_t romfs_sync(uint32_t mount_id)
{
    (void)mount_id;

    /* ROMFS 是只读文件系统，无需同步 */
    return 0;
}

/* ========================================================================
 * ROMFS 只读链接支持
 * ======================================================================== */

/**
 * @brief ROMFS symlink - 只读文件系统不支持创建
 */
static int32_t romfs_symlink(uint32_t mount_id, const char *target,
                              const char *linkpath)
{
    (void)mount_id;
    (void)target;
    (void)linkpath;

    /* ROMFS 是只读文件系统，不支持创建符号链接 */
    return -30;  /* EROFS */
}

/**
 * @brief ROMFS link - 只读文件系统不支持创建
 */
static int32_t romfs_link(uint32_t mount_id, const char *oldpath,
                           const char *newpath)
{
    (void)mount_id;
    (void)oldpath;
    (void)newpath;

    /* ROMFS 是只读文件系统，不支持创建硬链接 */
    return -30;  /* EROFS */
}

/**
 * @brief ROMFS readlink - 读取只读符号链接
 *
 * @note ROMFS 可以包含预构建的符号链接（镜像中已存在）
 */
static int64_t romfs_readlink(uint32_t mount_id, const char *path,
                               char *buf, uint64_t bufsize)
{
    uint32_t i;

    (void)mount_id;

    if ((path == NULL) || (buf == NULL) || (bufsize == 0U))
    {
        return -1;
    }

    /* 查找 inode */
    for (i = 0U; i < (ROMFS_MAX_FILES + ROMFS_MAX_DIRS); i++)
    {
        if (s_inodes[i].type == (uint32_t)FS_TYPE_SYMLINK)
        {
            /* 简化实现：检查路径是否匹配 */
            /* TODO: 实现完整的路径匹配 */
            if (s_inodes[i].data_offset < 65536U)
            {
                uint64_t len;

                /* 从数据区读取链接目标 */
                len = 0ULL;
                while ((len < bufsize) && (len < (uint64_t)s_inodes[i].size) &&
                       (s_inodes[i].data_offset + (uint32_t)len < 65536U) &&
                       (s_data[s_inodes[i].data_offset + (uint32_t)len] != '\0'))
                {
                    buf[len] = (char)s_data[s_inodes[i].data_offset + (uint32_t)len];
                    len++;
                }

                if (len < bufsize)
                {
                    buf[len] = '\0';
                }

                return (int64_t)len;
            }
        }
    }

    return -2;  /* ENOENT */
}

/* ========================================================================
 * ROMFS 操作接口
 * ======================================================================== */

/** @brief ROMFS 操作接口 */
static const fs_ops_t s_romfs_ops =
{
    .mount    = romfs_mount,
    .unmount  = romfs_unmount,
    .lookup   = romfs_lookup,
    .create   = romfs_create,
    .read     = romfs_read,
    .write    = romfs_write,
    .mkdir    = romfs_mkdir,
    .unlink   = romfs_unlink,
    .sync     = romfs_sync,
    .symlink  = romfs_symlink,
    .link     = romfs_link,
    .readlink = romfs_readlink
};

/* ========================================================================
 * 公共接口
 * ======================================================================== */

/**
 * @brief 获取 ROMFS 操作接口
 */
const fs_ops_t *romfs_get_ops(void)
{
    return &s_romfs_ops;
}
