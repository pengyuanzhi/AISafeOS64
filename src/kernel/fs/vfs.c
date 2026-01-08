/**
 * @file vfs.c
 * @brief AISafe64 RTOS - VFS Implementation
 *
 * @details Virtual File System implementation
 *
 * @note MISRA-C:2012 compliant
 * @note ISO 26262 ASIL-D compliant
 *
 * @version 1.0
 * @date 2025-01-08
 *
 * @author AISafe64 Team
 */

#include <fs/vfs.h>
#include <mm.h>
#include <printk.h>
#include <string.h>
#include <syscall.h>

/*
 * Global State
 */

static bool g_vfs_initialized = false;
static FDEntry_t g_fd_table[VFS_MAX_FD];
static VFSMount_t *g_mount_list = NULL;
static FileSystemType_t *g_fs_types[MAX_FILESYSTEMS];
static uint32_t g_fs_count = 0U;

static spinlock_t g_vfs_lock;

/*
 * Helper Functions
 */

/**
 * @brief Initialize file descriptor table
 */
static void init_fd_table(void)
{
    uint32_t i;

    for (i = 0U; i < VFS_MAX_FD; i++)
    {
        g_fd_table[i].file = NULL;
        g_fd_table[i].flags = 0U;
        g_fd_table[i].fd = (int)i;
        g_fd_table[i].open_count = 0ULL;
    }
}

/**
 * @brief Allocate file descriptor
 * @return File descriptor, -1 on failure
 */
static int alloc_fd(void)
{
    uint32_t i;

    for (i = 0U; i < VFS_MAX_FD; i++)
    {
        if (g_fd_table[i].file == NULL)
        {
            return (int)i;
        }
    }

    return -1; /* EMFILE */
}

/**
 * @brief Free file descriptor
 * @param fd File descriptor
 */
static void free_fd(int fd)
{
    if ((fd >= 0) && (fd < (int)VFS_MAX_FD))
    {
        g_fd_table[fd].file = NULL;
        g_fd_table[fd].flags = 0U;
        g_fd_table[fd].open_count = 0ULL;
    }
}

/**
 * @brief Find filesystem type
 * @param name Filesystem name
 * @return Filesystem type, NULL if not found
 */
static FileSystemType_t *find_filesystem(const char *name)
{
    uint32_t i;

    if (name == NULL)
    {
        return NULL;
    }

    for (i = 0U; i < g_fs_count; i++)
    {
        if (g_fs_types[i] != NULL)
        {
            if (strcmp(g_fs_types[i]->name, name) == 0)
            {
                return g_fs_types[i];
            }
        }
    }

    return NULL;
}

/**
 * @brief Find mount point by path
 * @param path Path
 * @return Mount point, NULL if not found
 */
static VFSMount_t *find_mount(const char *path)
{
    VFSMount_t *mount;
    VFSMount_t *best_match;
    size_t best_len;
    size_t len;

    if (path == NULL)
    {
        return NULL;
    }

    best_match = NULL;
    best_len = 0U;
    mount = g_mount_list;

    while (mount != NULL)
    {
        len = strlen(mount->mountpoint);

        /* Check if path starts with mountpoint */
        if (strncmp(path, mount->mountpoint, len) == 0)
        {
            /* Find longest matching mountpoint */
            if (len > best_len)
            {
                best_match = mount;
                best_len = len;
            }
        }

        mount = mount->next;
    }

    return best_match;
}

/**
 * @brief Allocate new file structure
 * @return File structure, NULL on failure
 */
static VFSFile_t *alloc_file(void)
{
    VFSFile_t *file;

    file = (VFSFile_t *)kmalloc((uint64_t)sizeof(VFSFile_t));
    if (file == NULL)
    {
        return NULL;
    }

    (void)memset(file, 0, sizeof(VFSFile_t));
    file->fd = -1;

    return file;
}

/**
 * @brief Free file structure
 * @param file File structure
 */
static void free_file(VFSFile_t *file)
{
    if (file != NULL)
    {
        if (file->private_data != NULL)
        {
            kfree(file->private_data);
        }
        kfree(file);
    }
}

/*
 * VFS Core API Implementation
 */

/**
 * @brief Initialize VFS layer
 */
int vfs_init(void)
{
    if (g_vfs_initialized)
    {
        return 0;
    }

    /* Initialize file descriptor table */
    init_fd_table();

    /* Initialize mount list */
    g_mount_list = NULL;

    /* Clear filesystem types */
    (void)memset(g_fs_types, 0, sizeof(g_fs_types));
    g_fs_count = 0U;

    /* Initialize lock */
    spinlock_init(&g_vfs_lock);

    g_vfs_initialized = true;

    printk(KERN_INFO "VFS initialized\n");

    return 0;
}

/**
 * @brief Register filesystem type
 */
int vfs_register_filesystem(const FileSystemType_t *fs)
{
    uint32_t i;

    if (fs == NULL)
    {
        return -EINVAL;
    }

    if (fs->name == NULL)
    {
        return -EINVAL;
    }

    spinlock_lock(&g_vfs_lock);

    /* Check if already registered */
    for (i = 0U; i < g_fs_count; i++)
    {
        if ((g_fs_types[i] != NULL) && (strcmp(g_fs_types[i]->name, fs->name) == 0))
        {
            spinlock_unlock(&g_vfs_lock);
            return -EEXIST;
        }
    }

    /* Check if space available */
    if (g_fs_count >= MAX_FILESYSTEMS)
    {
        spinlock_unlock(&g_vfs_lock);
        return -ENOSPC;
    }

    /* Register filesystem */
    g_fs_types[g_fs_count++] = (FileSystemType_t *)fs;

    spinlock_unlock(&g_vfs_lock);

    printk(KERN_INFO "VFS: Registered filesystem '%s'\n", fs->name);

    return 0;
}

/**
 * @brief Unregister filesystem type
 */
int vfs_unregister_filesystem(const char *name)
{
    uint32_t i;
    uint32_t j;

    if (name == NULL)
    {
        return -EINVAL;
    }

    spinlock_lock(&g_vfs_lock);

    /* Find filesystem */
    for (i = 0U; i < g_fs_count; i++)
    {
        if ((g_fs_types[i] != NULL) && (strcmp(g_fs_types[i]->name, name) == 0))
        {
            break;
        }
    }

    if (i >= g_fs_count)
    {
        spinlock_unlock(&g_vfs_lock);
        return -ENOENT;
    }

    /* Shift remaining filesystems */
    for (j = i; j < (g_fs_count - 1U); j++)
    {
        g_fs_types[j] = g_fs_types[j + 1U];
    }

    g_fs_count--;
    g_fs_types[g_fs_count] = NULL;

    spinlock_unlock(&g_vfs_lock);

    printk(KERN_INFO "VFS: Unregistered filesystem '%s'\n", name);

    return 0;
}

/**
 * @brief Mount filesystem
 */
int vfs_mount(const char *device, const char *mountpoint, const char *fstype, uint32_t flags)
{
    VFSMount_t *mount;
    FileSystemType_t *fs;
    int ret;

    if ((mountpoint == NULL) || (fstype == NULL))
    {
        return -EINVAL;
    }

    /* Find filesystem type */
    fs = find_filesystem(fstype);
    if (fs == NULL)
    {
        printk(KERN_ERR "VFS: Filesystem type '%s' not found\n", fstype);
        return -ENODEV;
    }

    /* Check if mountpoint already exists */
    mount = find_mount(mountpoint);
    if (mount != NULL)
    {
        printk(KERN_ERR "VFS: Mountpoint '%s' already mounted\n", mountpoint);
        return -EBUSY;
    }

    /* Allocate mount structure */
    mount = (VFSMount_t *)kmalloc((uint64_t)sizeof(VFSMount_t));
    if (mount == NULL)
    {
        return -ENOMEM;
    }

    /* Initialize mount */
    (void)memset(mount, 0, sizeof(VFSMount_t));

    if (device != NULL)
    {
        (void)strncpy(mount->device, device, sizeof(mount->device) - 1U);
    }

    (void)strncpy(mount->mountpoint, mountpoint, sizeof(mount->mountpoint) - 1U);
    mount->flags = flags;
    mount->root = NULL;
    mount->covered = NULL;
    mount->file_ops = NULL;
    mount->inode_ops = NULL;
    mount->sb_ops = NULL;
    mount->private_data = NULL;
    mount->next = NULL;

    /* Call filesystem mount function */
    if (fs->mount != NULL)
    {
        ret = fs->mount(mount, device, NULL);
        if (ret != 0)
        {
            printk(KERN_ERR "VFS: Failed to mount '%s': %d\n", mountpoint, ret);
            kfree(mount);
            return ret;
        }
    }

    /* Add to mount list */
    spinlock_lock(&g_vfs_lock);

    mount->next = g_mount_list;
    g_mount_list = mount;

    spinlock_unlock(&g_vfs_lock);

    printk(KERN_INFO "VFS: Mounted '%s' on '%s'\n", fstype, mountpoint);

    return 0;
}

/**
 * @brief Unmount filesystem
 */
int vfs_umount(const char *mountpoint)
{
    VFSMount_t *mount;
    VFSMount_t *prev;

    if (mountpoint == NULL)
    {
        return -EINVAL;
    }

    spinlock_lock(&g_vfs_lock);

    /* Find mount point */
    prev = NULL;
    mount = g_mount_list;

    while (mount != NULL)
    {
        if (strcmp(mount->mountpoint, mountpoint) == 0)
        {
            break;
        }
        prev = mount;
        mount = mount->next;
    }

    if (mount == NULL)
    {
        spinlock_unlock(&g_vfs_lock);
        return -ENOENT;
    }

    /* Remove from list */
    if (prev == NULL)
    {
        g_mount_list = mount->next;
    }
    else
    {
        prev->next = mount->next;
    }

    spinlock_unlock(&g_vfs_lock);

    /* Call filesystem unmount */
    if ((mount->sb_ops != NULL) && (mount->sb_ops->umount != NULL))
    {
        (void)mount->sb_ops->umount(mount);
    }

    printk(KERN_INFO "VFS: Unmounted '%s'\n", mountpoint);

    /* Free mount structure */
    if (mount->private_data != NULL)
    {
        kfree(mount->private_data);
    }
    kfree(mount);

    return 0;
}

/*
 * File Operations API Implementation
 */

/**
 * @brief Open file
 */
int vfs_open(const char *path, uint32_t flags)
{
    VFSFile_t *file;
    VFSMount_t *mount;
    VFSInode_t *inode;
    int fd;
    int ret;

    if (path == NULL)
    {
        return -EINVAL;
    }

    /* Find mount point */
    mount = find_mount(path);
    if (mount == NULL)
    {
        return -ENOENT;
    }

    /* Get inode */
    ret = vfs_get_inode(path, &inode);
    if (ret != 0)
    {
        /* File doesn't exist */
        if ((flags & O_CREAT) != 0U)
        {
            /* TODO: Create file */
            return -ENOSYS;
        }
        return ret;
    }

    /* Allocate file structure */
    file = alloc_file();
    if (file == NULL)
    {
        vfs_put_inode(inode);
        return -ENOMEM;
    }

    /* Initialize file */
    file->flags = flags;
    file->pos = 0ULL;
    file->offset = 0ULL;
    file->inode = inode;
    file->mount = mount;
    file->private_data = NULL;

    /* Call filesystem open function */
    if ((mount->file_ops != NULL) && (mount->file_ops->open != NULL))
    {
        ret = mount->file_ops->open(inode, file);
        if (ret != 0)
        {
            vfs_put_inode(inode);
            free_file(file);
            return ret;
        }
    }

    /* Allocate file descriptor */
    fd = alloc_fd();
    if (fd < 0)
    {
        if ((mount->file_ops != NULL) && (mount->file_ops->close != NULL))
        {
            (void)mount->file_ops->close(file);
        }
        vfs_put_inode(inode);
        free_file(file);
        return -EMFILE;
    }

    /* Link file to descriptor */
    file->fd = fd;
    g_fd_table[fd].file = file;
    g_fd_table[fd].flags = flags;
    g_fd_table[fd].open_count = 1ULL;

    return fd;
}

/**
 * @brief Close file
 */
int vfs_close(int fd)
{
    VFSFile_t *file;
    VFSMount_t *mount;
    int ret = 0;

    if ((fd < 0) || (fd >= (int)VFS_MAX_FD))
    {
        return -EBADF;
    }

    file = g_fd_table[fd].file;
    if (file == NULL)
    {
        return -EBADF;
    }

    mount = file->mount;

    /* Decrement reference count */
    g_fd_table[fd].open_count--;
    if (g_fd_table[fd].open_count > 0ULL)
    {
        return 0;
    }

    /* Call filesystem close function */
    if ((mount != NULL) && (mount->file_ops != NULL) && (mount->file_ops->close != NULL))
    {
        ret = mount->file_ops->close(file);
    }

    /* Put inode */
    if (file->inode != NULL)
    {
        vfs_put_inode(file->inode);
    }

    /* Free file */
    free_file(file);

    /* Free descriptor */
    free_fd(fd);

    return ret;
}

/**
 * @brief Read from file
 */
ssize_t vfs_read(int fd, void *buf, uint64_t count)
{
    VFSFile_t *file;
    VFSMount_t *mount;

    if ((fd < 0) || (fd >= (int)VFS_MAX_FD))
    {
        return -EBADF;
    }

    if (buf == NULL)
    {
        return -EINVAL;
    }

    file = g_fd_table[fd].file;
    if (file == NULL)
    {
        return -EBADF;
    }

    mount = file->mount;

    /* Check if readable */
    if ((file->flags & O_ACCMODE) == O_WRONLY)
    {
        return -EBADF;
    }

    /* Call filesystem read function */
    if ((mount == NULL) || (mount->file_ops == NULL) || (mount->file_ops->read == NULL))
    {
        return -ENOSYS;
    }

    return mount->file_ops->read(file, buf, count);
}

/**
 * @brief Write to file
 */
ssize_t vfs_write(int fd, const void *buf, uint64_t count)
{
    VFSFile_t *file;
    VFSMount_t *mount;

    if ((fd < 0) || (fd >= (int)VFS_MAX_FD))
    {
        return -EBADF;
    }

    if (buf == NULL)
    {
        return -EINVAL;
    }

    file = g_fd_table[fd].file;
    if (file == NULL)
    {
        return -EBADF;
    }

    mount = file->mount;

    /* Check if writable */
    if ((file->flags & O_ACCMODE) == O_RDONLY)
    {
        return -EBADF;
    }

    /* Call filesystem write function */
    if ((mount == NULL) || (mount->file_ops == NULL) || (mount->file_ops->write == NULL))
    {
        return -ENOSYS;
    }

    return mount->file_ops->write(file, buf, count);
}

/**
 * @brief Seek in file
 */
uint64_t vfs_lseek(int fd, int64_t offset, int whence)
{
    VFSFile_t *file;
    VFSMount_t *mount;
    uint64_t new_pos;

    if ((fd < 0) || (fd >= (int)VFS_MAX_FD))
    {
        return (uint64_t)-EBADF;
    }

    file = g_fd_table[fd].file;
    if (file == NULL)
    {
        return (uint64_t)-EBADF;
    }

    mount = file->mount;

    /* Call filesystem lseek function if available */
    if ((mount != NULL) && (mount->file_ops != NULL) && (mount->file_ops->lseek != NULL))
    {
        new_pos = mount->file_ops->lseek(file, offset, whence);
    }
    else
    {
        /* Default implementation */
        switch (whence)
        {
            case SEEK_SET:
                new_pos = (uint64_t)offset;
                break;

            case SEEK_CUR:
                new_pos = file->pos + (uint64_t)offset;
                break;

            case SEEK_END:
                if (file->inode != NULL)
                {
                    new_pos = file->inode->size + (uint64_t)offset;
                }
                else
                {
                    return (uint64_t)-EBADF;
                }
                break;

            default:
                return (uint64_t)-EINVAL;
        }

        file->pos = new_pos;
    }

    return new_pos;
}

/**
 * @brief IO control
 */
int vfs_ioctl(int fd, uint32_t cmd, void *arg)
{
    VFSFile_t *file;
    VFSMount_t *mount;

    if ((fd < 0) || (fd >= (int)VFS_MAX_FD))
    {
        return -EBADF;
    }

    file = g_fd_table[fd].file;
    if (file == NULL)
    {
        return -EBADF;
    }

    mount = file->mount;

    /* Call filesystem ioctl function */
    if ((mount == NULL) || (mount->file_ops == NULL) || (mount->file_ops->ioctl == NULL))
    {
        return -ENOSYS;
    }

    return mount->file_ops->ioctl(file, cmd, arg);
}

/**
 * @brief Get file statistics
 */
int vfs_stat(const char *path, VFSStat_t *stat)
{
    VFSInode_t *inode;
    int ret;

    if ((path == NULL) || (stat == NULL))
    {
        return -EINVAL;
    }

    /* Get inode */
    ret = vfs_get_inode(path, &inode);
    if (ret != 0)
    {
        return ret;
    }

    /* Initialize stat */
    (void)memset(stat, 0, sizeof(VFSStat_t));

    /* Fill stat from inode */
    stat->st_ino = (uint32_t)inode->ino;
    stat->st_mode = inode->mode;
    stat->st_size = inode->size;
    stat->st_blocks = inode->blocks;
    stat->st_atime = inode->atime;
    stat->st_mtime = inode->mtime;
    stat->st_ctime = inode->ctime;
    stat->st_nlink = inode->nlink;
    stat->st_uid = inode->uid;
    stat->st_gid = inode->gid;

    /* Put inode */
    vfs_put_inode(inode);

    return 0;
}

/**
 * @brief Get file statistics (by descriptor)
 */
int vfs_fstat(int fd, VFSStat_t *stat)
{
    VFSFile_t *file;
    VFSInode_t *inode;
    VFSMount_t *mount;

    if ((fd < 0) || (fd >= (int)VFS_MAX_FD))
    {
        return -EBADF;
    }

    if (stat == NULL)
    {
        return -EINVAL;
    }

    file = g_fd_table[fd].file;
    if (file == NULL)
    {
        return -EBADF;
    }

    inode = file->inode;
    mount = file->mount;

    /* Call filesystem fstat function if available */
    if ((mount != NULL) && (mount->file_ops != NULL) && (mount->file_ops->fstat != NULL))
    {
        return mount->file_ops->fstat(file, stat);
    }

    /* Default implementation */
    (void)memset(stat, 0, sizeof(VFSStat_t));

    if (inode != NULL)
    {
        stat->st_ino = (uint32_t)inode->ino;
        stat->st_mode = inode->mode;
        stat->st_size = inode->size;
        stat->st_blocks = inode->blocks;
        stat->st_atime = inode->atime;
        stat->st_mtime = inode->mtime;
        stat->st_ctime = inode->ctime;
        stat->st_nlink = inode->nlink;
        stat->st_uid = inode->uid;
        stat->st_gid = inode->gid;
    }

    return 0;
}

/**
 * @brief Sync file to disk
 */
int vfs_fsync(int fd)
{
    VFSFile_t *file;
    VFSMount_t *mount;

    if ((fd < 0) || (fd >= (int)VFS_MAX_FD))
    {
        return -EBADF;
    }

    file = g_fd_table[fd].file;
    if (file == NULL)
    {
        return -EBADF;
    }

    mount = file->mount;

    /* Call filesystem fsync function */
    if ((mount == NULL) || (mount->file_ops == NULL) || (mount->file_ops->fsync == NULL))
    {
        return -ENOSYS;
    }

    return mount->file_ops->fsync(file);
}

/*
 * Directory Operations API Implementation
 */

int vfs_opendir(const char *path)
{
    /* TODO: Implement opendir */
    (void)path;
    return -ENOSYS;
}

int vfs_readdir(int fd, DirEntry_t *entry)
{
    /* TODO: Implement readdir */
    (void)fd;
    (void)entry;
    return -ENOSYS;
}

int vfs_closedir(int fd)
{
    /* TODO: Implement closedir */
    (void)fd;
    return -ENOSYS;
}

int vfs_mkdir(const char *path, uint32_t mode)
{
    /* TODO: Implement mkdir */
    (void)path;
    (void)mode;
    return -ENOSYS;
}

int vfs_rmdir(const char *path)
{
    /* TODO: Implement rmdir */
    (void)path;
    return -ENOSYS;
}

/*
 * File Management API Implementation
 */

int vfs_create(const char *path, uint32_t mode)
{
    /* TODO: Implement create */
    (void)path;
    (void)mode;
    return -ENOSYS;
}

int vfs_unlink(const char *path)
{
    /* TODO: Implement unlink */
    (void)path;
    return -ENOSYS;
}

int vfs_rename(const char *oldpath, const char *newpath)
{
    /* TODO: Implement rename */
    (void)oldpath;
    (void)newpath;
    return -ENOSYS;
}

/*
 * Utility Functions Implementation
 */

/**
 * @brief Get file from file descriptor
 */
VFSFile_t *vfs_get_file(int fd)
{
    if ((fd < 0) || (fd >= (int)VFS_MAX_FD))
    {
        return NULL;
    }

    return g_fd_table[fd].file;
}

/**
 * @brief Put file (decrement reference)
 */
void vfs_put_file(VFSFile_t *file)
{
    /* TODO: Implement reference counting */
    (void)file;
}

/**
 * @brief Get inode from path
 */
int vfs_get_inode(const char *path, VFSInode_t **result)
{
    VFSMount_t *mount;

    if ((path == NULL) || (result == NULL))
    {
        return -EINVAL;
    }

    /* Find mount point */
    mount = find_mount(path);
    if (mount == NULL)
    {
        return -ENOENT;
    }

    /* TODO: Implement path traversal */
    /* For now, just return root inode if available */

    if (mount->root == NULL)
    {
        return -ENOENT;
    }

    *result = mount->root;

    return 0;
}

/**
 * @brief Put inode (decrement reference)
 */
void vfs_put_inode(VFSInode_t *inode)
{
    if (inode != NULL)
    {
        /* TODO: Implement reference counting */
        (void)inode;
    }
}

/**
 * @brief Resolve path to canonical form
 */
int vfs_resolve_path(const char *path, char *resolved, size_t size)
{
    /* TODO: Implement path resolution */
    if ((path == NULL) || (resolved == NULL))
    {
        return -EINVAL;
    }

    if (size == 0U)
    {
        return -EINVAL;
    }

    /* For now, just copy the path */
    (void)strncpy(resolved, path, size - 1U);
    resolved[size - 1U] = '\0';

    return 0;
}
