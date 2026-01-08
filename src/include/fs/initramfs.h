/**
 * @file initramfs.h
 * @brief AISafe64 RTOS - InitRAMFS Filesystem
 *
 * @details Initial RAM filesystem for boot-time file storage
 *          Simple filesystem stored in memory
 *
 * @note MISRA-C:2012 compliant
 * @note ISO 26262 ASIL-D compliant
 *
 * @version 1.0
 * @date 2025-01-08
 *
 * @author AISafe64 Team
 */

#ifndef INITRAMFS_H
#define INITRAMFS_H

#include <fs/vfs.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * InitRAMFS Constants
     */

#define INITRAMFS_MAX_FILES 128U           /**< Maximum files in initramfs */
#define INITRAMFS_MAX_SIZE (1024U * 1024U) /**< Maximum initramfs size (1MB) */
#define INITRAMFS_BLOCK_SIZE 4096U         /**< Block size */

    /*
     * InitRAMFS File Node
     */

    typedef struct InitRAMFSNode
    {
        char name[MAX_NAME_LEN]; /**< Filename */
        uint64_t ino;            /**< Inode number */
        uint32_t mode;           /**< File mode */
        uint64_t size;           /**< File size */
        uint64_t data_offset;    /**< Offset in data area */

        uint32_t uid;   /**< Owner user ID */
        uint32_t gid;   /**< Owner group ID */
        uint64_t atime; /**< Access time */
        uint64_t mtime; /**< Modification time */
        uint64_t ctime; /**< Change time */

        struct InitRAMFSNode *parent;   /**< Parent directory */
        struct InitRAMFSNode *children; /**< Child files */
        struct InitRAMFSNode *next;     /**< Next sibling */

        bool is_directory; /**< Is directory */
        uint32_t refcount; /**< Reference count */

        VFSInode_t *vfs_inode; /**< Associated VFS inode */

    } InitRAMFSNode_t;

    /*
     * InitRAMFS Superblock
     */

    typedef struct
    {
        uint8_t *data;      /**< Data area */
        uint64_t data_size; /**< Data area size */
        uint64_t data_used; /**< Used data */

        InitRAMFSNode_t *root;  /**< Root directory */
        InitRAMFSNode_t *files; /**< All files */

        uint32_t file_count; /**< Number of files */
        uint64_t next_ino;   /**< Next inode number */

        spinlock_t lock; /**< Protects filesystem */

    } InitRAMFSSuper_t;

    /*
     * InitRAMFS API
     */

    /**
     * @brief Initialize initramfs
     * @param size Size of data area
     * @return 0 on success, negative error code on failure
     */
    int initramfs_init(uint64_t size);

    /**
     * @brief Create initramfs from memory image
     * @param addr Memory address
     * @param size Image size
     * @return 0 on success, negative error code on failure
     */
    int initramfs_load_from_memory(uint8_t *addr, uint64_t size);

    /**
     * @brief Create file in initramfs
     * @param path File path
     * @param mode File mode
     * @return Node on success, NULL on failure
     */
    InitRAMFSNode_t *initramfs_create_file(const char *path, uint32_t mode);

    /**
     * @brief Create directory in initramfs
     * @param path Directory path
     * @param mode Directory mode
     * @return Node on success, NULL on failure
     */
    InitRAMFSNode_t *initramfs_create_dir(const char *path, uint32_t mode);

    /**
     * @brief Write data to file
     * @param node File node
     * @param data Data to write
     * @param size Size of data
     * @return Number of bytes written, negative error code on failure
     */
    int initramfs_write_file(InitRAMFSNode_t *node, const void *data, uint64_t size);

    /**
     * @brief Read data from file
     * @param node File node
     * @param data Buffer to read into
     * @param size Size to read
     * @param offset Offset in file
     * @return Number of bytes read, negative error code on failure
     */
    int initramfs_read_file(InitRAMFSNode_t *node, void *data, uint64_t size, uint64_t offset);

    /**
     * @brief Lookup file by path
     * @param path File path
     * @return Node on success, NULL on failure
     */
    InitRAMFSNode_t *initramfs_lookup(const char *path);

    /**
     * @brief Delete file
     * @param path File path
     * @return 0 on success, negative error code on failure
     */
    int initramfs_unlink(const char *path);

    /**
     * @brief Get initramfs superblock
     * @return Superblock
     */
    InitRAMFSSuper_t *initramfs_get_super(void);

    /**
     * @brief Register initramfs with VFS
     * @return 0 on success, negative error code on failure
     */
    int initramfs_register(void);

    /**
     * @brief Unregister initramfs from VFS
     * @return 0 on success, negative error code on failure
     */
    int initramfs_unregister(void);

#ifdef __cplusplus
}
#endif

#endif /* INITRAMFS_H */
