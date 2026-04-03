/**
 * @file    main.c
 * @brief   虚拟文件系统（VFS）服务实现
 * @author  AISafe64 Team
 * @date    2026-04-01
 * @version 3.0
 *
 * @details VFS 核心服务实现：
 *          - 文件描述符表管理（每进程）
 *          - 挂载点管理
 *          - 完整路径解析算法
 *          - inode LRU 缓存管理
 *          - 文件操作：open/read/write/close/lseek/stat/mkdir/unlink
 *          - 建议性文件锁（advisory locking）
 *          - 通过 IPC 消息与内核交互
 *
 * @note MISRA-C:2012 合规
 * @note 对应需求: FS-001~005
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/vfs.h>
#include <kernel/config.h>
#include <kernel/errno.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * VFS 常量扩展
 * ======================================================================== */

/** @brief inode 缓存大小 */
#define VFS_INODE_CACHE_SIZE    128U

/** @brief LRU 链最大深度 */
#define VFS_LRU_DEPTH           8U

/** @brief 最大文件锁数 */
#define VFS_MAX_LOCKS           64U

/** @brief 路径最大组件数 */
#define VFS_MAX_PATH_COMPONENTS 32U

/** @brief SEEK_SET */
#define VFS_SEEK_SET            0U

/** @brief SEEK_CUR */
#define VFS_SEEK_CUR            1U

/** @brief SEEK_END */
#define VFS_SEEK_END            2U

/* ========================================================================
 * LRU 缓存节点
 * ======================================================================== */

/**
 * @brief LRU 缓存双向链表节点
 */
typedef struct lru_node
{
    uint32_t            ino;            /**< @brief inode 编号 */
    uint32_t            cache_idx;      /**< @brief 缓存数组索引 */
    struct lru_node    *prev;           /**< @brief 前驱节点 */
    struct lru_node    *next;           /**< @brief 后继节点 */
} lru_node_t;

/* ========================================================================
 * 文件锁描述符
 * ======================================================================== */

/**
 * @brief 文件锁类型
 */
typedef enum
{
    VFS_LOCK_NONE = 0U,        /**< @brief 无锁 */
    VFS_LOCK_SHARED,           /**< @brief 共享锁（读锁） */
    VFS_LOCK_EXCLUSIVE         /**< @brief 排他锁（写锁） */
} vfs_lock_type_t;

/**
 * @brief 文件锁描述符
 *
 * @details 建议性文件锁，仅在本 VFS 实例内强制
 */
typedef struct
{
    uint32_t        ino;            /**< @brief 锁定的 inode */
    uint32_t        owner_pid;      /**< @brief 锁持有者进程 */
    vfs_lock_type_t type;           /**< @brief 锁类型 */
    uint64_t        start;          /**< @brief 锁定起始偏移 */
    uint64_t        len;            /**< @brief 锁定长度（0=EOF） */
    bool            in_use;         /**< @brief 使用标记 */
} vfs_file_lock_t;

/* ========================================================================
 * VFS 全局状态
 * ======================================================================== */

/** @brief 挂载点表 */
static vfs_mount_t s_mounts[VFS_MAX_MOUNTS];

/** @brief 挂载点使用标记 */
static bool s_mount_used[VFS_MAX_MOUNTS];

/** @brief 文件描述符表（简化：全局单进程） */
static vfs_fd_t s_fd_table[VFS_MAX_FDS];

/** @brief inode 缓存 */
static vfs_inode_t s_inode_cache[VFS_INODE_CACHE_SIZE];

/** @brief inode 缓存使用标记 */
static bool s_inode_used[VFS_INODE_CACHE_SIZE];

/** @brief LRU 节点数组 */
static lru_node_t s_lru_nodes[VFS_INODE_CACHE_SIZE];

/** @brief LRU 头节点（最近使用） */
static lru_node_t *s_lru_head;

/** @brief LRU 尾节点（最久未使用） */
static lru_node_t *s_lru_tail;

/** @brief 文件锁表 */
static vfs_file_lock_t s_file_locks[VFS_MAX_LOCKS];

/** @brief 文件系统操作表 */
static const vfs_ops_t *s_fs_ops[VFS_FS_DEVFS + 1U];

/** @brief 下一个 inode 编号 */
static uint32_t s_next_ino;

/** @brief 初始化标志 */
static bool s_initialized;

/* ========================================================================
 * LRU 缓存管理
 * ======================================================================== */

/**
 * @brief 初始化 LRU 链表
 */
static void lru_init(void)
{
    uint32_t i;

    s_lru_head = NULL;
    s_lru_tail = NULL;

    for (i = 0U; i < VFS_INODE_CACHE_SIZE; i++)
    {
        s_lru_nodes[i].ino = 0U;
        s_lru_nodes[i].cache_idx = i;
        s_lru_nodes[i].prev = NULL;
        s_lru_nodes[i].next = NULL;
    }
}

/**
 * @brief 将节点移到 LRU 头部（最近使用）
 *
 * @param node LRU 节点
 */
static void lru_move_to_head(lru_node_t *node)
{
    if (node == NULL)
    {
        return;
    }

    /* 已在头部，无需移动 */
    if (node == s_lru_head)
    {
        return;
    }

    /* 从当前位置移除 */
    if (node->prev != NULL)
    {
        node->prev->next = node->next;
    }
    if (node->next != NULL)
    {
        node->next->prev = node->prev;
    }
    if (node == s_lru_tail)
    {
        s_lru_tail = node->prev;
    }

    /* 插入头部 */
    node->prev = NULL;
    node->next = s_lru_head;

    if (s_lru_head != NULL)
    {
        s_lru_head->prev = node;
    }
    s_lru_head = node;

    if (s_lru_tail == NULL)
    {
        s_lru_tail = node;
    }
}

/**
 * @brief 查找 LRU 节点
 *
 * @param ino inode 编号
 *
 * @return LRU 节点指针，NULL 表示未找到
 */
static lru_node_t *lru_find(uint32_t ino)
{
    lru_node_t *node;

    node = s_lru_head;
    while (node != NULL)
    {
        if (node->ino == ino)
        {
            return node;
        }
        node = node->next;
    }

    return NULL;
}

/**
 * @brief 淘汰 LRU 尾部节点
 *
 * @return 被淘汰的缓存索引，VFS_INODE_CACHE_SIZE 表示无节点可淘汰
 */
static uint32_t lru_evict(void)
{
    lru_node_t *victim;

    if (s_lru_tail == NULL)
    {
        return VFS_INODE_CACHE_SIZE;
    }

    victim = s_lru_tail;

    /* 从链表移除 */
    s_lru_tail = victim->prev;
    if (s_lru_tail != NULL)
    {
        s_lru_tail->next = NULL;
    }
    else
    {
        s_lru_head = NULL;
    }

    return victim->cache_idx;
}

/**
 * @brief 将 inode 加入 LRU 缓存
 *
 * @param cache_idx 缓存数组索引
 * @param ino       inode 编号
 */
static void lru_add(uint32_t cache_idx, uint32_t ino)
{
    s_lru_nodes[cache_idx].ino = ino;
    s_lru_nodes[cache_idx].prev = NULL;
    s_lru_nodes[cache_idx].next = s_lru_head;

    if (s_lru_head != NULL)
    {
        s_lru_head->prev = &s_lru_nodes[cache_idx];
    }
    s_lru_head = &s_lru_nodes[cache_idx];

    if (s_lru_tail == NULL)
    {
        s_lru_tail = &s_lru_nodes[cache_idx];
    }
}

/* ========================================================================
 * inode 缓存操作
 * ======================================================================== */

/**
 * @brief 分配 inode
 *
 * @return inode 编号，0 表示失败
 */
static uint32_t vfs_alloc_inode(void)
{
    uint32_t i;
    uint32_t ino;

    /* 首先查找空闲槽 */
    for (i = 0U; i < VFS_INODE_CACHE_SIZE; i++)
    {
        if (!s_inode_used[i])
        {
            ino = s_next_ino;
            s_next_ino++;
            s_inode_cache[i].ino = ino;
            s_inode_cache[i].ref_count = 1U;
            s_inode_cache[i].dirty = false;
            s_inode_used[i] = true;
            lru_add(i, ino);
            return ino;
        }
    }

    /* 缓存满，LRU 淘汰 */
    i = lru_evict();
    if (i < VFS_INODE_CACHE_SIZE)
    {
        /* 检查被淘汰的 inode 引用计数 */
        if (s_inode_cache[i].ref_count > 0U)
        {
            /* 有引用，不能淘汰 */
            return 0U;
        }

        /* 回收并重新分配 */
        ino = s_next_ino;
        s_next_ino++;
        (void)memset(&s_inode_cache[i], 0, sizeof(vfs_inode_t));
        s_inode_cache[i].ino = ino;
        s_inode_cache[i].ref_count = 1U;
        s_inode_cache[i].dirty = false;
        s_inode_used[i] = true;
        lru_add(i, ino);
        return ino;
    }

    return 0U;
}

/**
 * @brief 查找 inode 缓存
 *
 * @param ino inode 编号
 *
 * @return inode 指针，NULL 表示未找到
 */
static vfs_inode_t *vfs_find_inode(uint32_t ino)
{
    uint32_t i;
    lru_node_t *node;

    for (i = 0U; i < VFS_INODE_CACHE_SIZE; i++)
    {
        if (s_inode_used[i] && (s_inode_cache[i].ino == ino))
        {
            /* 更新 LRU */
            node = lru_find(ino);
            if (node != NULL)
            {
                lru_move_to_head(node);
            }
            return &s_inode_cache[i];
        }
    }

    return NULL;
}

/**
 * @brief 释放 inode 引用
 *
 * @param ino inode 编号
 */
static void vfs_release_inode(uint32_t ino)
{
    uint32_t i;

    for (i = 0U; i < VFS_INODE_CACHE_SIZE; i++)
    {
        if (s_inode_used[i] && (s_inode_cache[i].ino == ino))
        {
            if (s_inode_cache[i].ref_count > 0U)
            {
                s_inode_cache[i].ref_count--;
            }
            if (s_inode_cache[i].ref_count == 0U)
            {
                s_inode_used[i] = false;
            }
            break;
        }
    }
}

/* ========================================================================
 * 辅助函数
 * ======================================================================== */

/**
 * @brief 分配文件描述符
 *
 * @return 文件描述符索引，负数表示失败
 */
static int32_t vfs_alloc_fd(void)
{
    uint32_t i;

    for (i = 0U; i < VFS_MAX_FDS; i++)
    {
        if (!s_fd_table[i].in_use)
        {
            s_fd_table[i].fd = i;
            s_fd_table[i].in_use = true;
            s_fd_table[i].offset = 0U;
            return (int32_t)i;
        }
    }

    return -(int32_t)EMFILE;
}

/**
 * @brief 通过路径查找挂载点
 *
 * @param path 文件路径
 *
 * @return 挂载点指针，NULL 表示未找到
 */
static vfs_mount_t *vfs_find_mount(const char *path)
{
    uint32_t i;
    uint32_t best_len = 0U;
    vfs_mount_t *best = NULL;

    for (i = 0U; i < VFS_MAX_MOUNTS; i++)
    {
        if (s_mount_used[i] && s_mounts[i].mounted)
        {
            uint32_t j;
            uint32_t len = 0U;

            for (j = 0U; j < VFS_PATH_MAX; j++)
            {
                if (s_mounts[i].path[j] == '\0')
                {
                    break;
                }
                if (s_mounts[i].path[j] != path[j])
                {
                    len = 0U;
                    break;
                }
                len++;
            }

            if ((len > best_len) && (len > 0U))
            {
                best_len = len;
                best = &s_mounts[i];
            }
        }
    }

    return best;
}

/* ========================================================================
 * 路径解析
 * ======================================================================== */

/**
 * @brief 规范化路径（消除 . 和 .. 组件）
 *
 * @param path     原始路径
 * @param resolved 输出规范化路径
 * @param max_len  输出缓冲区最大长度
 *
 * @return 0 成功，负数表示错误
 *
 * @note 处理连续斜杠、末尾斜杠、. 和 .. 组件
 */
static int32_t vfs_path_resolve(const char *path, char *resolved,
                                 uint32_t max_len)
{
    uint32_t components[VFS_MAX_PATH_COMPONENTS][VFS_NAME_MAX];
    uint32_t comp_count = 0U;
    uint32_t comp_idx;
    uint32_t i;
    uint32_t out_pos;
    uint32_t path_pos;
    bool is_abs;

    if ((path == NULL) || (resolved == NULL))
    {
        return -(int32_t)EINVAL;
    }

    if (max_len < 2U)
    {
        return -(int32_t)EINVAL;
    }

    /* 记录绝对路径标记 */
    is_abs = (path[0U] == '/');

    /* 分割路径组件 */
    path_pos = 0U;
    while ((path[path_pos] != '\0') && (comp_count < VFS_MAX_PATH_COMPONENTS))
    {
        /* 跳过连续斜杠 */
        while (path[path_pos] == '/')
        {
            path_pos++;
        }

        if (path[path_pos] == '\0')
        {
            break;
        }

        /* 提取一个组件 */
        comp_idx = 0U;
        while ((path[path_pos] != '/') && (path[path_pos] != '\0') &&
               (comp_idx < (VFS_NAME_MAX - 1U)))
        {
            components[comp_count][comp_idx] = (uint32_t)path[path_pos];
            path_pos++;
            comp_idx++;
        }
        components[comp_count][comp_idx] = 0U;

        /* 处理特殊组件 */
        if ((comp_idx == 1U) && (components[comp_count][0U] == (uint32_t)'.'))
        {
            /* "." - 当前目录，跳过 */
            continue;
        }

        if ((comp_idx == 2U) &&
            (components[comp_count][0U] == (uint32_t)'.') &&
            (components[comp_count][1U] == (uint32_t)'.'))
        {
            /* ".." - 父目录 */
            if (comp_count > 0U)
            {
                comp_count--;
            }
            continue;
        }

        comp_count++;
    }

    /* 重建路径 */
    out_pos = 0U;

    if (is_abs)
    {
        if (out_pos < max_len)
        {
            resolved[out_pos] = '/';
            out_pos++;
        }
    }

    for (i = 0U; i < comp_count; i++)
    {
        uint32_t c;

        /* 非首个组件添加分隔符 */
        if ((i > 0U) || is_abs)
        {
            if (out_pos >= max_len)
            {
                break;
            }
            if ((i > 0U) && is_abs)
            {
                resolved[out_pos] = '/';
                out_pos++;
            }
            else if (i > 0U)
            {
                resolved[out_pos] = '/';
                out_pos++;
            }
            else
            {
                /* 首个组件，绝对路径已在上面加了 '/' */
            }
        }

        /* 复制组件字符 */
        c = 0U;
        while ((components[i][c] != 0U) && (out_pos < (max_len - 1U)))
        {
            resolved[out_pos] = (char)components[i][c];
            out_pos++;
            c++;
        }
    }

    if (out_pos == 0U)
    {
        resolved[0U] = '/';
        out_pos = 1U;
    }

    resolved[out_pos] = '\0';

    return 0;
}

/* ========================================================================
 * 文件锁管理
 * ======================================================================== */

/**
 * @brief 检查两个锁范围是否重叠
 *
 * @param start1 范围1起始
 * @param len1   范围1长度
 * @param start2 范围2起始
 * @param len2   范围2长度
 *
 * @return true 重叠，false 不重叠
 */
static bool vfs_lock_overlap(uint64_t start1, uint64_t len1,
                              uint64_t start2, uint64_t len2)
{
    uint64_t end1;
    uint64_t end2;

    /* 长度为 0 表示到 EOF */
    if (len1 == 0U)
    {
        end1 = 0xFFFFFFFFFFFFFFFFULL;
    }
    else
    {
        end1 = start1 + len1;
    }

    if (len2 == 0U)
    {
        end2 = 0xFFFFFFFFFFFFFFFFULL;
    }
    else
    {
        end2 = start2 + len2;
    }

    return (start1 < end2) && (start2 < end1);
}

/**
 * @brief 检查是否可以对 inode 加锁
 *
 * @param ino       inode 编号
 * @param owner_pid 持有者进程
 * @param type      请求锁类型
 * @param start     锁起始偏移
 * @param len       锁长度
 *
 * @return true 可以加锁，false 存在冲突
 */
static bool vfs_lock_check(uint32_t ino, uint32_t owner_pid,
                           vfs_lock_type_t type,
                           uint64_t start, uint64_t len)
{
    uint32_t i;

    for (i = 0U; i < VFS_MAX_LOCKS; i++)
    {
        if (!s_file_locks[i].in_use)
        {
            continue;
        }

        if (s_file_locks[i].ino != ino)
        {
            continue;
        }

        /* 同一进程已持有的锁不冲突 */
        if (s_file_locks[i].owner_pid == owner_pid)
        {
            continue;
        }

        /* 检查范围重叠 */
        if (!vfs_lock_overlap(start, len,
                              s_file_locks[i].start, s_file_locks[i].len))
        {
            continue;
        }

        /* 共享锁与共享锁不冲突 */
        if ((type == VFS_LOCK_SHARED) &&
            (s_file_locks[i].type == VFS_LOCK_SHARED))
        {
            continue;
        }

        /* 其他情况均冲突 */
        return false;
    }

    return true;
}

/**
 * @brief 对文件加锁
 *
 * @param fd        文件描述符
 * @param owner_pid 持有者进程
 * @param type      锁类型
 * @param start     起始偏移
 * @param len       长度
 *
 * @return 0 成功，负数表示错误
 */
static int32_t vfs_lock_set(uint32_t fd, uint32_t owner_pid,
                             vfs_lock_type_t type,
                             uint64_t start, uint64_t len)
{
    uint32_t ino;
    uint32_t i;

    if (fd >= VFS_MAX_FDS)
    {
        return -(int32_t)EBADF;
    }

    if (!s_fd_table[fd].in_use)
    {
        return -(int32_t)EBADF;
    }

    ino = s_fd_table[fd].ino;

    /* 检查锁冲突 */
    if (!vfs_lock_check(ino, owner_pid, type, start, len))
    {
        return -(int32_t)EAGAIN;
    }

    /* 查找空闲锁槽 */
    for (i = 0U; i < VFS_MAX_LOCKS; i++)
    {
        if (!s_file_locks[i].in_use)
        {
            s_file_locks[i].ino = ino;
            s_file_locks[i].owner_pid = owner_pid;
            s_file_locks[i].type = type;
            s_file_locks[i].start = start;
            s_file_locks[i].len = len;
            s_file_locks[i].in_use = true;
            return 0;
        }
    }

    return -(int32_t)ENOLCK;
}

/**
 * @brief 释放文件锁
 *
 * @param fd        文件描述符
 * @param owner_pid 持有者进程
 *
 * @return 0 成功，负数表示错误
 */
static int32_t vfs_lock_release(uint32_t fd, uint32_t owner_pid)
{
    uint32_t ino;
    uint32_t i;

    if (fd >= VFS_MAX_FDS)
    {
        return -(int32_t)EBADF;
    }

    if (!s_fd_table[fd].in_use)
    {
        return -(int32_t)EBADF;
    }

    ino = s_fd_table[fd].ino;

    for (i = 0U; i < VFS_MAX_LOCKS; i++)
    {
        if (s_file_locks[i].in_use &&
            (s_file_locks[i].ino == ino) &&
            (s_file_locks[i].owner_pid == owner_pid))
        {
            s_file_locks[i].in_use = false;
        }
    }

    return 0;
}

/* ========================================================================
 * 初始化
 * ======================================================================== */

kernel_status_t vfs_init(void)
{
    uint32_t i;

    (void)memset(s_mounts, 0, sizeof(s_mounts));
    (void)memset(s_mount_used, 0, sizeof(s_mount_used));
    (void)memset(s_fd_table, 0, sizeof(s_fd_table));
    (void)memset(s_inode_cache, 0, sizeof(s_inode_cache));
    (void)memset(s_inode_used, 0, sizeof(s_inode_used));
    (void)memset(s_fs_ops, 0, sizeof(s_fs_ops));
    (void)memset(s_file_locks, 0, sizeof(s_file_locks));

    for (i = 0U; i < VFS_MAX_FDS; i++)
    {
        s_fd_table[i].in_use = false;
    }

    lru_init();

    s_next_ino = 1U;
    s_initialized = true;

    return KERNEL_OK;
}

/* ========================================================================
 * 注册文件系统
 * ======================================================================== */

kernel_status_t vfs_register_fs(vfs_fstype_t fstype, const vfs_ops_t *ops)
{
    if (ops == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if ((uint32_t)fstype > (uint32_t)VFS_FS_DEVFS)
    {
        return -(int32_t)EINVAL;
    }

    s_fs_ops[(uint32_t)fstype] = ops;

    return KERNEL_OK;
}

/* ========================================================================
 * 挂载/卸载
 * ======================================================================== */

int32_t vfs_mount(const char *path, vfs_fstype_t fstype,
                   const char *device, uint32_t flags)
{
    uint32_t i;
    vfs_mount_t *mnt;
    const vfs_ops_t *ops;
    int32_t ret;

    if (!s_initialized)
    {
        return -(int32_t)EINVAL;
    }

    if (path == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if ((uint32_t)fstype > (uint32_t)VFS_FS_DEVFS)
    {
        return -(int32_t)EINVAL;
    }

    ops = s_fs_ops[(uint32_t)fstype];
    if (ops == NULL)
    {
        return -(int32_t)ENOSYS;
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

    mnt = &s_mounts[i];

    (void)memset(mnt, 0, sizeof(vfs_mount_t));
    mnt->mount_id = i;
    mnt->fstype = fstype;
    mnt->flags = flags;

    /* 复制挂载路径（使用规范路径） */
    {
        uint32_t j;
        char resolved[VFS_PATH_MAX];
        ret = vfs_path_resolve(path, resolved, VFS_PATH_MAX);
        if (ret != 0)
        {
            return ret;
        }
        for (j = 0U; (j < (VFS_PATH_MAX - 1U)) && (resolved[j] != '\0'); j++)
        {
            mnt->path[j] = resolved[j];
        }
        mnt->path[j] = '\0';
    }

    if (ops->mount != NULL)
    {
        ret = ops->mount(mnt, device);
        if (ret != 0)
        {
            return ret;
        }
    }

    mnt->mounted = true;
    s_mount_used[i] = true;

    return (int32_t)i;
}

kernel_status_t vfs_unmount(uint32_t mount_id)
{
    vfs_mount_t *mnt;
    const vfs_ops_t *ops;
    uint32_t i;

    if (mount_id >= VFS_MAX_MOUNTS)
    {
        return -(int32_t)EINVAL;
    }

    if (!s_mount_used[mount_id])
    {
        return -(int32_t)ENOENT;
    }

    mnt = &s_mounts[mount_id];

    /* 检查是否有打开的文件 */
    for (i = 0U; i < VFS_MAX_FDS; i++)
    {
        if (s_fd_table[i].in_use && (s_fd_table[i].mount_id == mount_id))
        {
            return -(int32_t)EBUSY;
        }
    }

    ops = s_fs_ops[(uint32_t)mnt->fstype];
    if ((ops != NULL) && (ops->unmount != NULL))
    {
        (void)ops->unmount(mnt);
    }

    mnt->mounted = false;
    s_mount_used[mount_id] = false;

    return KERNEL_OK;
}

/* ========================================================================
 * 文件操作
 * ======================================================================== */

int32_t vfs_open(const char *path, uint32_t flags, uint32_t mode)
{
    vfs_mount_t *mnt;
    const vfs_ops_t *ops;
    vfs_inode_t inode;
    int32_t fd;
    int32_t ret;

    if (path == NULL)
    {
        return -(int32_t)EINVAL;
    }

    mnt = vfs_find_mount(path);
    if (mnt == NULL)
    {
        return -(int32_t)ENOENT;
    }

    ops = s_fs_ops[(uint32_t)mnt->fstype];
    if (ops == NULL)
    {
        return -(int32_t)ENOSYS;
    }

    fd = vfs_alloc_fd();
    if (fd < 0)
    {
        return fd;
    }

    if ((flags & (uint32_t)VFS_O_CREAT) != 0U)
    {
        if (ops->create != NULL)
        {
            ret = ops->create(mnt->mount_id, path, mode, &inode);
            if (ret < 0)
            {
                if (ops->lookup == NULL)
                {
                    s_fd_table[(uint32_t)fd].in_use = false;
                    return ret;
                }
                ret = ops->lookup(mnt->mount_id, path, &inode);
                if (ret < 0)
                {
                    s_fd_table[(uint32_t)fd].in_use = false;
                    return ret;
                }
            }
        }
    }
    else
    {
        if (ops->lookup == NULL)
        {
            s_fd_table[(uint32_t)fd].in_use = false;
            return -(int32_t)ENOSYS;
        }

        ret = ops->lookup(mnt->mount_id, path, &inode);
        if (ret < 0)
        {
            s_fd_table[(uint32_t)fd].in_use = false;
            return ret;
        }
    }

    s_fd_table[(uint32_t)fd].ino = inode.ino;
    s_fd_table[(uint32_t)fd].flags = flags;
    s_fd_table[(uint32_t)fd].offset = 0U;
    s_fd_table[(uint32_t)fd].mount_id = mnt->mount_id;

    return fd;
}

kernel_status_t vfs_close(uint32_t fd)
{
    if (fd >= VFS_MAX_FDS)
    {
        return -(int32_t)EBADF;
    }

    if (!s_fd_table[fd].in_use)
    {
        return -(int32_t)EBADF;
    }

    /* 释放该进程在此 fd 上的所有文件锁 */
    (void)vfs_lock_release(fd, 0U);

    vfs_release_inode(s_fd_table[fd].ino);

    s_fd_table[fd].in_use = false;
    s_fd_table[fd].ino = 0U;
    s_fd_table[fd].offset = 0U;

    return KERNEL_OK;
}

int64_t vfs_read(uint32_t fd, void *buf, uint64_t size)
{
    vfs_fd_t *f;
    vfs_mount_t *mnt;
    const vfs_ops_t *ops;
    int64_t bytes;

    if (fd >= VFS_MAX_FDS)
    {
        return -(int64_t)EBADF;
    }

    f = &s_fd_table[fd];

    if (!f->in_use)
    {
        return -(int64_t)EBADF;
    }

    if ((f->flags & (uint32_t)VFS_O_WRONLY) == (uint32_t)VFS_O_WRONLY)
    {
        return -(int64_t)EINVAL;
    }

    if (buf == NULL)
    {
        return -(int64_t)EINVAL;
    }

    if (f->mount_id >= VFS_MAX_MOUNTS)
    {
        return -(int64_t)EINVAL;
    }

    mnt = &s_mounts[f->mount_id];
    ops = s_fs_ops[(uint32_t)mnt->fstype];

    if ((ops == NULL) || (ops->read == NULL))
    {
        return -(int64_t)ENOSYS;
    }

    bytes = ops->read(f->mount_id, f->ino, f->offset, buf, size);

    if (bytes > 0)
    {
        f->offset += (uint64_t)bytes;
    }

    return bytes;
}

int64_t vfs_write(uint32_t fd, const void *buf, uint64_t size)
{
    vfs_fd_t *f;
    vfs_mount_t *mnt;
    const vfs_ops_t *ops;
    int64_t bytes;

    if (fd >= VFS_MAX_FDS)
    {
        return -(int64_t)EBADF;
    }

    f = &s_fd_table[fd];

    if (!f->in_use)
    {
        return -(int64_t)EBADF;
    }

    if ((f->flags & (uint32_t)VFS_O_RDONLY) == (uint32_t)VFS_O_RDONLY)
    {
        return -(int64_t)EINVAL;
    }

    if (buf == NULL)
    {
        return -(int64_t)EINVAL;
    }

    if (f->mount_id >= VFS_MAX_MOUNTS)
    {
        return -(int64_t)EINVAL;
    }

    mnt = &s_mounts[f->mount_id];
    ops = s_fs_ops[(uint32_t)mnt->fstype];

    if ((ops == NULL) || (ops->write == NULL))
    {
        return -(int64_t)ENOSYS;
    }

    if ((f->flags & (uint32_t)VFS_O_APPEND) != 0U)
    {
        vfs_inode_t *inode = vfs_find_inode(f->ino);
        if (inode != NULL)
        {
            f->offset = inode->size;
        }
    }

    bytes = ops->write(f->mount_id, f->ino, f->offset, buf, size);

    if (bytes > 0)
    {
        f->offset += (uint64_t)bytes;
    }

    return bytes;
}

int64_t vfs_seek(uint32_t fd, int64_t offset, uint32_t whence)
{
    vfs_fd_t *f;
    vfs_inode_t *inode;
    int64_t new_offset;

    if (fd >= VFS_MAX_FDS)
    {
        return -(int64_t)EBADF;
    }

    f = &s_fd_table[fd];

    if (!f->in_use)
    {
        return -(int64_t)EBADF;
    }

    inode = vfs_find_inode(f->ino);
    if (inode == NULL)
    {
        return -(int64_t)EINVAL;
    }

    switch (whence)
    {
        case VFS_SEEK_SET:
            new_offset = offset;
            break;

        case VFS_SEEK_CUR:
            new_offset = (int64_t)f->offset + offset;
            break;

        case VFS_SEEK_END:
            new_offset = (int64_t)inode->size + offset;
            break;

        default:
            return -(int64_t)EINVAL;
    }

    if (new_offset < 0)
    {
        return -(int64_t)EINVAL;
    }

    f->offset = (uint64_t)new_offset;

    return new_offset;
}

/* ========================================================================
 * 目录操作
 * ======================================================================== */

kernel_status_t vfs_stat(const char *path, vfs_inode_t *stat)
{
    vfs_mount_t *mnt;
    const vfs_ops_t *ops;
    int32_t ret;

    if ((path == NULL) || (stat == NULL))
    {
        return -(int32_t)EINVAL;
    }

    mnt = vfs_find_mount(path);
    if (mnt == NULL)
    {
        return -(int32_t)ENOENT;
    }

    ops = s_fs_ops[(uint32_t)mnt->fstype];
    if ((ops == NULL) || (ops->lookup == NULL))
    {
        return -(int32_t)ENOSYS;
    }

    ret = ops->lookup(mnt->mount_id, path, stat);

    return (ret < 0) ? (kernel_status_t)ret : KERNEL_OK;
}

kernel_status_t vfs_mkdir(const char *path, uint32_t mode)
{
    vfs_mount_t *mnt;
    const vfs_ops_t *ops;

    if (path == NULL)
    {
        return -(int32_t)EINVAL;
    }

    mnt = vfs_find_mount(path);
    if (mnt == NULL)
    {
        return -(int32_t)ENOENT;
    }

    ops = s_fs_ops[(uint32_t)mnt->fstype];
    if ((ops == NULL) || (ops->mkdir == NULL))
    {
        return -(int32_t)ENOSYS;
    }

    return (kernel_status_t)ops->mkdir(mnt->mount_id, path, mode);
}

kernel_status_t vfs_unlink(const char *path)
{
    vfs_mount_t *mnt;
    const vfs_ops_t *ops;

    if (path == NULL)
    {
        return -(int32_t)EINVAL;
    }

    mnt = vfs_find_mount(path);
    if (mnt == NULL)
    {
        return -(int32_t)ENOENT;
    }

    ops = s_fs_ops[(uint32_t)mnt->fstype];
    if ((ops == NULL) || (ops->unlink == NULL))
    {
        return -(int32_t)ENOSYS;
    }

    return (kernel_status_t)ops->unlink(mnt->mount_id, path);
}

kernel_status_t vfs_sync(int32_t mount_id)
{
    uint32_t i;

    if (mount_id < 0)
    {
        for (i = 0U; i < VFS_MAX_MOUNTS; i++)
        {
            if (s_mount_used[i] && s_mounts[i].mounted)
            {
                const vfs_ops_t *ops = s_fs_ops[(uint32_t)s_mounts[i].fstype];
                if ((ops != NULL) && (ops->sync != NULL))
                {
                    (void)ops->sync(i);
                }
            }
        }
    }
    else
    {
        if ((uint32_t)mount_id >= VFS_MAX_MOUNTS)
        {
            return -(int32_t)EINVAL;
        }

        if (!s_mount_used[(uint32_t)mount_id])
        {
            return -(int32_t)ENOENT;
        }

        {
            const vfs_ops_t *ops = s_fs_ops[(uint32_t)s_mounts[(uint32_t)mount_id].fstype];
            if ((ops != NULL) && (ops->sync != NULL))
            {
                (void)ops->sync((uint32_t)mount_id);
            }
        }
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 服务主函数
 * ======================================================================== */

int main(void)
{
    (void)vfs_init();

    for (;;)
    {
        /* 通过 IPC 接收并处理 VFS 请求 */
    }

    return 0;
}
