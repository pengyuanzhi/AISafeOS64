/**
 * @file vfs.h
 * @brief AISafe64 RTOS - Virtual File System
 *
 * @details Virtual File System layer for unified file access
 *          Supports multiple filesystem types
 *
 * @note MISRA-C:2012 compliant
 * @note ISO 26262 ASIL-D compliant
 *
 * @version 1.0
 * @date 2025-01-08
 *
 * @author AISafe64 Team
 */

#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * VFS Constants
 */

#define MAX_OPEN_FILES 256U       /**< Maximum open files per process */
#define MAX_FILESYSTEMS 8U         /**< Maximum mounted filesystems */
#define MAX_PATH_LEN 256U          /**< Maximum path length */
#define MAX_NAME_LEN 64U           /**< Maximum filename length */
#define VFS_MAX_FD 1024U           /**< Maximum file descriptors */

/*
 * File Types
 */

typedef enum
{
    FT_UNKNOWN = 0,  /**< Unknown file type */
    FT_REGULAR,      /**< Regular file */
    FT_DIRECTORY,    /**< Directory */
    FT_CHARDEV,      /**< Character device */
    FT_BLOCKDEV,     /**< Block device */
    FT_FIFO,         /**< Named pipe (FIFO) */
    FT_SYMLINK,      /**< Symbolic link */
    FT_SOCKET        /**< Socket */
} FileType_t;

/*
 * File Access Modes
 */

typedef enum
{
    O_RDONLY = 0,    /**< Read-only */
    O_WRONLY = 1,    /**< Write-only */
    O_RDWR = 2,      /**< Read-write */
    O_CREAT = 0x0100, /**< Create file if not exists */
    O_EXCL = 0x0200,  /**< Exclusive create */
    O_TRUNC = 0x0400, /**< Truncate file */
    O_APPEND = 0x0800 /**< Append mode */
} OpenFlags_t;

/*
 * Seek Origins
 */

typedef enum
{
    SEEK_SET = 0, /**< Seek from beginning */
    SEEK_CUR = 1, /**< Seek from current position */
    SEEK_END = 2  /**< Seek from end */
} SeekOrigin_t;

/*
 * Forward Declarations
 */

struct VFSInode;
struct VFSFile;
struct VFSMount;
struct FileSystemOps;

/*
 * File Statistics
 */

typedef struct
{
    uint32_t st_dev;     /**< Device ID */
    uint32_t st_ino;     /**< Inode number */
    uint32_t st_mode;    /**< File mode and permissions */
    uint32_t st_nlink;   /**< Number of hard links */
    uint32_t st_uid;     /**< User ID */
    uint32_t st_gid;     /**< Group ID */
    uint64_t st_rdev;    /**< Device ID (if special file) */
    uint64_t st_size;    /**< Total size in bytes */
    uint64_t st_blksize; /**< Block size */
    uint64_t st_blocks;  /**< Number of blocks allocated */
    uint64_t st_atime;   /**< Last access time */
    uint64_t st_mtime;   /**< Last modification time */
    uint64_t st_ctime;   /**< Last status change time */
} VFSStat_t;

/*
 * Inode Structure
 */

typedef struct VFSInode
{
    uint64_t ino;              /**< Inode number */
    uint32_t mode;             /**< File mode and type */
    uint64_t size;             /**< File size */
    uint64_t blocks;           /**< Number of blocks */
    uint64_t atime;            /**< Access time */
    uint64_t mtime;            /**< Modification time */
    uint64_t ctime;            /**< Change time */
    uint32_t nlink;            /**< Link count */
    uint32_t uid;              /**< Owner user ID */
    uint32_t gid;              /**< Owner group ID */
    uint64_t refcnt;           /**< Reference count */

    struct VFSInode *parent;   /**< Parent directory */
    struct VFSMount *mount;    /**< Mount point */

    void *private_data;        /**< Filesystem-specific data */

} VFSInode_t;

/*
 * File Structure
 */

typedef struct VFSFile
{
    int fd;                    /**< File descriptor */
    uint32_t flags;            /**< Open flags */
    uint64_t pos;              /**< Current position */
    uint64_t offset;           /**< Current offset */

    VFSInode_t *inode;         /**< Associated inode */
    struct VFSMount *mount;    /**< Mount point */

    void *private_data;        /**< Filesystem-specific data */

} VFSFile_t;

/*
 * File Operations Interface
 */

typedef struct FileOps
{
    /**
     * @brief Open file
     */
    int (*open)(VFSInode_t *inode, VFSFile_t *file);

    /**
     * @brief Close file
     */
    int (*close)(VFSFile_t *file);

    /**
     * @brief Read from file
     */
    ssize_t (*read)(VFSFile_t *file, void *buf, uint64_t count);

    /**
     * @brief Write to file
     */
    ssize_t (*write)(VFSFile_t *file, const void *buf, uint64_t count);

    /**
     * @brief Seek in file
     */
    uint64_t (*lseek)(VFSFile_t *file, int64_t offset, int whence);

    /**
     * @brief IO control
     */
    int (*ioctl)(VFSFile_t *file, uint32_t cmd, void *arg);

    /**
     * @brief Get file statistics
     */
    int (*fstat)(VFSFile_t *file, VFSStat_t *stat);

    /**
     * @brief Sync file to disk
     */
    int (*fsync)(VFSFile_t *file);

} FileOps_t;

/*
 * Inode Operations Interface
 */

typedef struct InodeOps
{
    /**
     * @brief Lookup inode in directory
     */
    int (*lookup)(VFSInode_t *dir, const char *name, VFSInode_t **result);

    /**
     * @brief Create new inode
     */
    int (*create)(VFSInode_t *dir, const char *name, uint32_t mode, VFSInode_t **result);

    /**
     * @brief Create directory
     */
    int (*mkdir)(VFSInode_t *dir, const char *name, uint32_t mode);

    /**
     * @brief Remove file
     */
    int (*unlink)(VFSInode_t *dir, const char *name);

    /**
     * @brief Remove directory
     */
    int (*rmdir)(VFSInode_t *dir, const char *name);

    /**
     * @brief Rename file
     */
    int (*rename)(VFSInode_t *old_dir, const char *old_name,
                  VFSInode_t *new_dir, const char *new_name);

    /**
     * @brief Get file statistics
     */
    int (*stat)(VFSInode_t *inode, VFSStat_t *stat);

} InodeOps_t;

/*
 * Superblock Operations
 */

typedef struct SuperBlockOps
{
    /**
     * @brief Allocate new inode
     */
    int (*alloc_inode)(struct VFSMount *mount, VFSInode_t **inode);

    /**
     * @brief Free inode
     */
    int (*free_inode)(VFSInode_t *inode);

    /**
     * @brief Write inode to disk
     */
    int (*write_inode)(VFSInode_t *inode);

    /**
     * @brief Read inode from disk
     */
    int (*read_inode)(VFSInode_t *inode);

    /**
     * @brief Sync filesystem
     */
    int (*sync)(struct VFSMount *mount);

    /**
     * @brief Unmount filesystem
     */
    int (*umount)(struct VFSMount *mount);

} SuperBlockOps_t;

/*
 * Mount Point Structure
 */

typedef struct VFSMount
{
    char mountpoint[MAX_PATH_LEN]; /**< Mount point path */
    char device[MAX_PATH_LEN];     /**< Device path */
    uint32_t flags;                /**< Mount flags */

    VFSInode_t *root;              /**< Root inode */
    VFSInode_t *covered;           /**< Covered directory */

    const FileOps_t *file_ops;     /**< File operations */
    const InodeOps_t *inode_ops;   /**< Inode operations */
    const SuperBlockOps_t *sb_ops; /**< Superblock operations */

    void *private_data;            /**< Filesystem-specific data */

    struct VFSMount *next;         /**< Next mount in list */

} VFSMount_t;

/*
 * Filesystem Type
 */

typedef struct FileSystemType
{
    const char *name;              /**< Filesystem name */
    uint32_t flags;                /**< Filesystem flags */

    /**
     * @brief Mount filesystem
     */
    int (*mount)(VFSMount_t *mount, const char *device, const char *opts);

    /**
     * @brief Kill filesystem (forced unmount)
     */
    int (*kill_sb)(VFSMount_t *mount);

} FileSystemType_t;

/*
 * Directory Entry Structure
 */

typedef struct DirEntry
{
    uint64_t ino;                  /**< Inode number */
    uint32_t type;                 /**< File type */
    uint8_t name_len;              /**< Name length */
    char name[MAX_NAME_LEN];       /**< Filename */

} DirEntry_t;

/*
 * Global File Descriptor Table Entry
 */

typedef struct
{
    VFSFile_t *file;               /**< File structure */
    uint32_t flags;                /**< Descriptor flags */
    int fd;                        /**< File descriptor number */
    uint64_t open_count;           /**< Number of times opened */

} FDEntry_t;

/*
 * VFS Core API
 */

/**
 * @brief Initialize VFS layer
 * @return 0 on success, negative error code on failure
 */
int vfs_init(void);

/**
 * @brief Register filesystem type
 * @param fs Filesystem type structure
 * @return 0 on success, negative error code on failure
 */
int vfs_register_filesystem(const FileSystemType_t *fs);

/**
 * @brief Unregister filesystem type
 * @param name Filesystem name
 * @return 0 on success, negative error code on failure
 */
int vfs_unregister_filesystem(const char *name);

/**
 * @brief Mount filesystem
 * @param device Device path
 * @param mountpoint Mount point path
 * @param fstype Filesystem type name
 * @param flags Mount flags
 * @return 0 on success, negative error code on failure
 */
int vfs_mount(const char *device, const char *mountpoint, const char *fstype, uint32_t flags);

/**
 * @brief Unmount filesystem
 * @param mountpoint Mount point path
 * @return 0 on success, negative error code on failure
 */
int vfs_umount(const char *mountpoint);

/*
 * File Operations API
 */

/**
 * @brief Open file
 * @param path File path
 * @param flags Open flags
 * @return File descriptor on success, negative error code on failure
 */
int vfs_open(const char *path, uint32_t flags);

/**
 * @brief Close file
 * @param fd File descriptor
 * @return 0 on success, negative error code on failure
 */
int vfs_close(int fd);

/**
 * @brief Read from file
 * @param fd File descriptor
 * @param buf Buffer to read into
 * @param count Number of bytes to read
 * @return Number of bytes read, negative error code on failure
 */
ssize_t vfs_read(int fd, void *buf, uint64_t count);

/**
 * @brief Write to file
 * @param fd File descriptor
 * @param buf Buffer to write from
 * @param count Number of bytes to write
 * @return Number of bytes written, negative error code on failure
 */
ssize_t vfs_write(int fd, const void *buf, uint64_t count);

/**
 * @brief Seek in file
 * @param fd File descriptor
 * @param offset Offset
 * @param whence Seek origin
 * @return New position, negative error code on failure
 */
uint64_t vfs_lseek(int fd, int64_t offset, int whence);

/**
 * @brief IO control
 * @param fd File descriptor
 * @param cmd Command
 * @param arg Argument
 * @return 0 on success, negative error code on failure
 */
int vfs_ioctl(int fd, uint32_t cmd, void *arg);

/**
 * @brief Get file statistics
 * @param path File path
 * @param stat Statistics structure
 * @return 0 on success, negative error code on failure
 */
int vfs_stat(const char *path, VFSStat_t *stat);

/**
 * @brief Get file statistics (by descriptor)
 * @param fd File descriptor
 * @param stat Statistics structure
 * @return 0 on success, negative error code on failure
 */
int vfs_fstat(int fd, VFSStat_t *stat);

/**
 * @brief Sync file to disk
 * @param fd File descriptor
 * @return 0 on success, negative error code on failure
 */
int vfs_fsync(int fd);

/*
 * Directory Operations API
 */

/**
 * @brief Open directory
 * @param path Directory path
 * @return File descriptor on success, negative error code on failure
 */
int vfs_opendir(const char *path);

/**
 * @brief Read directory entry
 * @param fd File descriptor
 * @param entry Directory entry structure
 * @return 0 on success, negative error code on failure or end of directory
 */
int vfs_readdir(int fd, DirEntry_t *entry);

/**
 * @brief Close directory
 * @param fd File descriptor
 * @return 0 on success, negative error code on failure
 */
int vfs_closedir(int fd);

/**
 * @brief Create directory
 * @param path Directory path
 * @param mode Directory mode
 * @return 0 on success, negative error code on failure
 */
int vfs_mkdir(const char *path, uint32_t mode);

/**
 * @brief Remove directory
 * @param path Directory path
 * @return 0 on success, negative error code on failure
 */
int vfs_rmdir(const char *path);

/*
 * File Management API
 */

/**
 * @brief Create file
 * @param path File path
 * @param mode File mode
 * @return 0 on success, negative error code on failure
 */
int vfs_create(const char *path, uint32_t mode);

/**
 * @brief Remove file
 * @param path File path
 * @return 0 on success, negative error code on failure
 */
int vfs_unlink(const char *path);

/**
 * @brief Rename file
 * @param oldpath Old path
 * @param newpath New path
 * @return 0 on success, negative error code on failure
 */
int vfs_rename(const char *oldpath, const char *newpath);

/*
 * Utility Functions
 */

/**
 * @brief Get file from file descriptor
 * @param fd File descriptor
 * @return File structure, NULL on failure
 */
VFSFile_t *vfs_get_file(int fd);

/**
 * @brief Put file (decrement reference)
 * @param file File structure
 */
void vfs_put_file(VFSFile_t *file);

/**
 * @brief Get inode from path
 * @param path File path
 * @param result Output: inode
 * @return 0 on success, negative error code on failure
 */
int vfs_get_inode(const char *path, VFSInode_t **result);

/**
 * @brief Put inode (decrement reference)
 * @param inode Inode structure
 */
void vfs_put_inode(VFSInode_t *inode);

/**
 * @brief Resolve path to canonical form
 * @param path Input path
 * @param resolved Output: resolved path
 * @param size Size of resolved buffer
 * @return 0 on success, negative error code on failure
 */
int vfs_resolve_path(const char *path, char *resolved, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* VFS_H */
