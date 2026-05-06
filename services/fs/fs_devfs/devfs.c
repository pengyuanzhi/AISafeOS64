/**
 * @file    devfs.c
 * @brief   DEVFS 设备文件系统实现
 * @author  AISafe64 Team
 * @date    2026-05-07
 * @version 1.0
 *
 * @details DEVFS 设备文件系统实现：
 *          - 设备注册/注销/查找
 *          - fs_ops 接口适配
 *          - 通过 /dev 路径访问设备
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include "devfs.h"
#include "fs_ops.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ========================================================================
 * 常量定义
 * ======================================================================== */

/** @brief DEVFS 根路径前缀 */
#define DEVFS_ROOT_PREFIX       "/dev/"

/** @brief DEVFS 根路径长度 */
#define DEVFS_ROOT_PREFIX_LEN   5U

/* ========================================================================
 * DEVFS 内部状态
 * ======================================================================== */

/** @brief DEVFS 全局实例（静态分配，避免动态内存） */
static devfs_instance_t s_devfs_instance;

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 查找空闲设备槽位
 *
 * @param inst DEVFS 实例
 *
 * @return 设备槽位指针，NULL 表示已满
 */
static devfs_device_t *find_free_device(devfs_instance_t *inst)
{
    uint32_t i;

    if (inst == NULL)
    {
        return NULL;
    }

    for (i = 0U; i < DEVFS_MAX_DEVICES; i++)
    {
        if (!inst->devices[i].in_use)
        {
            return &inst->devices[i];
        }
    }

    return NULL;
}

/**
 * @brief 从路径中提取设备名
 *
 * @param path     完整路径（如 /dev/null）
 * @param name     输出设备名
 * @param name_len 缓冲区大小
 *
 * @return 0 成功，<0 失败
 */
static int32_t extract_device_name(const char *path, char *name,
                                    uint32_t name_len)
{
    uint32_t i;
    uint32_t j;

    if ((path == NULL) || (name == NULL) || (name_len == 0U))
    {
        return -1;
    }

    /* 跳过 "/dev/" 前缀 */
    i = 0U;
    while ((i < DEVFS_ROOT_PREFIX_LEN) && (path[i] != '\0'))
    {
        if (path[i] != DEVFS_ROOT_PREFIX[i])
        {
            break;
        }
        i++;
    }

    if (i == DEVFS_ROOT_PREFIX_LEN)
    {
        /* 成功匹配 /dev/ 前缀 */
    }
    else if (path[0] == '/')
    {
        /* 跳过前导 '/' */
        i = 1U;
    }
    else
    {
        i = 0U;
    }

    /* 复制设备名 */
    j = 0U;
    while ((path[i] != '\0') && (j < (name_len - 1U)))
    {
        name[j] = path[i];
        i++;
        j++;
    }
    name[j] = '\0';

    if (j == 0U)
    {
        return -1;
    }

    return 0;
}

/* ========================================================================
 * DEVFS 公共接口实现
 * ======================================================================== */

/**
 * @brief 初始化 DEVFS 实例
 */
int32_t devfs_instance_init(devfs_instance_t *inst)
{
    uint32_t i;

    if (inst == NULL)
    {
        return -1;
    }

    for (i = 0U; i < DEVFS_MAX_DEVICES; i++)
    {
        (void)memset(&inst->devices[i], 0, sizeof(devfs_device_t));
    }

    inst->initialized = true;

    return 0;
}

/**
 * @brief 清理 DEVFS 实例
 */
int32_t devfs_instance_cleanup(devfs_instance_t *inst)
{
    uint32_t i;

    if (inst == NULL)
    {
        return -1;
    }

    /* 关闭所有设备 */
    for (i = 0U; i < DEVFS_MAX_DEVICES; i++)
    {
        if (inst->devices[i].in_use && (inst->devices[i].ops != NULL))
        {
            if (inst->devices[i].ops->close != NULL)
            {
                (void)inst->devices[i].ops->close(inst->devices[i].private_data);
            }
        }
    }

    (void)memset(inst, 0, sizeof(devfs_instance_t));

    return 0;
}

/**
 * @brief 注册设备
 */
int32_t devfs_register_device(devfs_instance_t *inst,
                               const char *name,
                               devfs_device_type_t type,
                               uint32_t major,
                               uint32_t minor,
                               const devfs_ops_t *ops,
                               void *private_data)
{
    devfs_device_t *dev;
    uint32_t name_len;
    uint32_t k;

    if ((inst == NULL) || (name == NULL) || (ops == NULL))
    {
        return -1;
    }

    /* 检查设备名长度 */
    name_len = 0U;
    while (name[name_len] != '\0')
    {
        name_len++;
        if (name_len >= DEVFS_MAX_NAME_LEN)
        {
            return -1;
        }
    }

    if (name_len == 0U)
    {
        return -1;
    }

    /* 检查是否重名 */
    for (k = 0U; k < DEVFS_MAX_DEVICES; k++)
    {
        if (inst->devices[k].in_use)
        {
            if (strcmp(inst->devices[k].name, name) == 0)
            {
                return -1;
            }
        }
    }

    /* 查找空闲槽位 */
    dev = find_free_device(inst);
    if (dev == NULL)
    {
        return -1;
    }

    /* 复制设备名称 */
    for (k = 0U; k < name_len; k++)
    {
        dev->name[k] = name[k];
    }
    dev->name[name_len] = '\0';

    dev->type = type;
    dev->major = major;
    dev->minor = minor;
    dev->ops = ops;
    dev->private_data = private_data;
    dev->in_use = true;

    return 0;
}

/**
 * @brief 注销设备
 */
int32_t devfs_unregister_device(devfs_instance_t *inst, const char *name)
{
    uint32_t i;

    if ((inst == NULL) || (name == NULL))
    {
        return -1;
    }

    for (i = 0U; i < DEVFS_MAX_DEVICES; i++)
    {
        if (inst->devices[i].in_use &&
            (strcmp(inst->devices[i].name, name) == 0))
        {
            /* 关闭设备 */
            if ((inst->devices[i].ops != NULL) &&
                (inst->devices[i].ops->close != NULL))
            {
                (void)inst->devices[i].ops->close(inst->devices[i].private_data);
            }

            (void)memset(&inst->devices[i], 0, sizeof(devfs_device_t));

            return 0;
        }
    }

    return -1;
}

/**
 * @brief 查找设备
 */
devfs_device_t *devfs_find_device(devfs_instance_t *inst, const char *name)
{
    uint32_t i;

    if ((inst == NULL) || (name == NULL))
    {
        return NULL;
    }

    for (i = 0U; i < DEVFS_MAX_DEVICES; i++)
    {
        if (inst->devices[i].in_use &&
            (strcmp(inst->devices[i].name, name) == 0))
        {
            return &inst->devices[i];
        }
    }

    return NULL;
}

/* ========================================================================
 * fs_ops 接口实现
 * ======================================================================== */

/**
 * @brief DEVFS mount
 */
static int32_t devfs_mount(fs_mount_t *mnt, const char *device)
{
    int32_t ret;

    (void)device;

    if (mnt == NULL)
    {
        return -1;
    }

    ret = devfs_instance_init(&s_devfs_instance);
    if (ret != 0)
    {
        return -1;
    }

    mnt->private_data = (void *)&s_devfs_instance;

    return 0;
}

/**
 * @brief DEVFS unmount
 */
static int32_t devfs_unmount(fs_mount_t *mnt)
{
    if ((mnt == NULL) || (mnt->private_data == NULL))
    {
        return -1;
    }

    (void)devfs_instance_cleanup((devfs_instance_t *)mnt->private_data);
    mnt->private_data = NULL;

    return 0;
}

/**
 * @brief DEVFS lookup
 */
static int32_t devfs_lookup(uint32_t mount_id, const char *path,
                             fs_inode_t *inode)
{
    devfs_instance_t *inst;
    devfs_device_t *dev;
    char device_name[DEVFS_MAX_NAME_LEN];
    int32_t ret;

    (void)mount_id;

    if ((path == NULL) || (inode == NULL))
    {
        return -1;
    }

    inst = &s_devfs_instance;
    if (!inst->initialized)
    {
        return -1;
    }

    /* 根目录 */
    if ((path[0] == '/') && (path[1] == '\0'))
    {
        (void)memset(inode, 0, sizeof(fs_inode_t));
        inode->ino = 0U;
        inode->type = FS_TYPE_DIRECTORY;
        inode->mode = 0755U;
        return 0;
    }

    /* 提取设备名 */
    ret = extract_device_name(path, device_name, DEVFS_MAX_NAME_LEN);
    if (ret != 0)
    {
        return -2;  /* ENOENT */
    }

    /* 查找设备 */
    dev = devfs_find_device(inst, device_name);
    if (dev == NULL)
    {
        return -2;  /* ENOENT */
    }

    /* 填充 inode */
    (void)memset(inode, 0, sizeof(fs_inode_t));
    inode->ino = (uint32_t)(((uintptr_t)dev - (uintptr_t)inst->devices) /
                             sizeof(devfs_device_t));
    inode->type = FS_TYPE_DEVICE;
    inode->mode = 0666U;
    inode->size = 0ULL;
    inode->nlinks = 1U;

    return 0;
}

/**
 * @brief DEVFS create（不支持）
 */
static int32_t devfs_create(uint32_t mount_id, const char *path,
                             uint32_t mode, fs_inode_t *inode)
{
    (void)mount_id;
    (void)path;
    (void)mode;
    (void)inode;

    /* DEVFS 不支持动态创建设备文件 */
    return -1;
}

/**
 * @brief DEVFS read
 */
static int64_t devfs_read(uint32_t mount_id, uint32_t ino,
                           uint64_t offset, void *buf, uint64_t size)
{
    devfs_instance_t *inst;
    devfs_device_t *dev;

    (void)mount_id;
    (void)offset;

    if (buf == NULL)
    {
        return -1;
    }

    inst = &s_devfs_instance;
    if (!inst->initialized)
    {
        return -1;
    }

    if (ino >= DEVFS_MAX_DEVICES)
    {
        return -1;
    }

    dev = &inst->devices[ino];
    if (!dev->in_use || (dev->ops == NULL) || (dev->ops->read == NULL))
    {
        return -1;
    }

    return dev->ops->read(dev->private_data, buf, size);
}

/**
 * @brief DEVFS write
 */
static int64_t devfs_write(uint32_t mount_id, uint32_t ino,
                            uint64_t offset, const void *buf, uint64_t size)
{
    devfs_instance_t *inst;
    devfs_device_t *dev;

    (void)mount_id;
    (void)offset;

    if (buf == NULL)
    {
        return -1;
    }

    inst = &s_devfs_instance;
    if (!inst->initialized)
    {
        return -1;
    }

    if (ino >= DEVFS_MAX_DEVICES)
    {
        return -1;
    }

    dev = &inst->devices[ino];
    if (!dev->in_use || (dev->ops == NULL) || (dev->ops->write == NULL))
    {
        return -1;
    }

    return dev->ops->write(dev->private_data, buf, size);
}

/**
 * @brief DEVFS mkdir（不支持）
 */
static int32_t devfs_mkdir(uint32_t mount_id, const char *path,
                            uint32_t mode)
{
    (void)mount_id;
    (void)path;
    (void)mode;

    /* DEVFS 不支持创建目录 */
    return -1;
}

/**
 * @brief DEVFS unlink（不支持）
 */
static int32_t devfs_unlink(uint32_t mount_id, const char *path)
{
    (void)mount_id;
    (void)path;

    /* DEVFS 不支持手动删除设备文件 */
    return -1;
}

/**
 * @brief DEVFS sync
 */
static int32_t devfs_sync(uint32_t mount_id)
{
    (void)mount_id;

    return 0;
}

/* ========================================================================
 * DEVFS 操作接口
 * ======================================================================== */

/** @brief DEVFS 操作接口 */
static const fs_ops_t s_devfs_ops =
{
    .mount   = devfs_mount,
    .unmount = devfs_unmount,
    .lookup  = devfs_lookup,
    .create  = devfs_create,
    .read    = devfs_read,
    .write   = devfs_write,
    .mkdir   = devfs_mkdir,
    .unlink  = devfs_unlink,
    .sync    = devfs_sync
};

/**
 * @brief 获取 DEVFS 操作接口
 *
 * @return DEVFS 操作接口指针
 */
const fs_ops_t *devfs_get_ops(void)
{
    return &s_devfs_ops;
}
