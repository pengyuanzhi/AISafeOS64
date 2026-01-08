/**
 * @file procfs.h
 * @brief AISafe64 RTOS - ProcFS Filesystem
 *
 * @details Process filesystem for runtime system information
 *          Provides access to process and system information
 *
 * @note MISRA-C:2012 compliant
 * @note ISO 26262 ASIL-D compliant
 *
 * @version 1.0
 * @date 2025-01-08
 *
 * @author AISafe64 Team
 */

#ifndef PROCFS_H
#define PROCFS_H

#include <fs/vfs.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * ProcFS Constants
     */

#define PROCFS_MAX_ENTRIES 64U     /**< Maximum procfs entries */
#define PROCFS_MAX_DATA_SIZE 4096U /**< Maximum data size per entry */
#define PROCFS_PATH_MAX 128U       /**< Maximum path length */

    /*
     * ProcFS Entry Types
     */

    typedef enum
    {
        PROC_TYPE_DIR = 0, /**< Directory */
        PROC_TYPE_FILE,    /**< Regular file */
        PROC_TYPE_SYMLINK, /**< Symbolic link */
        PROC_TYPE_DYNAMIC  /**< Dynamic content (callback) */
    } ProcFSType_t;

    /*
     * ProcFS Entry
     */

    typedef struct ProcFSEntry
    {
        char name[MAX_NAME_LEN]; /**< Entry name */
        ProcFSType_t type;       /**< Entry type */
        uint32_t mode;           /**< File mode */
        uint32_t uid;            /**< Owner user ID */
        uint32_t gid;            /**< Owner group ID */

        struct ProcFSEntry *parent;   /**< Parent directory */
        struct ProcFSEntry *children; /**< Child entries */
        struct ProcFSEntry *next;     /**< Next sibling */

        uint64_t ino;      /**< Inode number */
        uint32_t refcount; /**< Reference count */

        VFSInode_t *vfs_inode; /**< Associated VFS inode */

        /**
         * @brief Read callback for dynamic entries
         * @param entry Entry
         * @param buf Buffer to write to
         * @param size Buffer size
         * @return Number of bytes written
         */
        ssize_t (*read_cb)(struct ProcFSEntry *entry, char *buf, size_t size);

        /**
         * @brief Write callback for dynamic entries
         * @param entry Entry
         * @param buf Buffer to read from
         * @param size Buffer size
         * @return Number of bytes read
         */
        ssize_t (*write_cb)(struct ProcFSEntry *entry, const char *buf, size_t size);

        void *private_data; /**< Entry-specific data */

    } ProcFSEntry_t;

    /*
     * ProcFS Superblock
     */

    typedef struct
    {
        ProcFSEntry_t *root;    /**< Root directory */
        ProcFSEntry_t *entries; /**< All entries */
        uint32_t entry_count;   /**< Number of entries */
        uint64_t next_ino;      /**< Next inode number */

        spinlock_t lock; /**< Protects filesystem */

    } ProcFSSuper_t;

    /*
     * ProcFS API
     */

    /**
     * @brief Initialize procfs
     * @return 0 on success, negative error code on failure
     */
    int procfs_init(void);

    /**
     * @brief Create procfs entry
     * @param parent Parent entry
     * @param name Entry name
     * @param type Entry type
     * @param mode File mode
     * @return New entry, NULL on failure
     */
    ProcFSEntry_t *procfs_create_entry(ProcFSEntry_t *parent, const char *name, ProcFSType_t type,
                                       uint32_t mode);

    /**
     * @brief Remove procfs entry
     * @param entry Entry to remove
     * @return 0 on success, negative error code on failure
     */
    int procfs_remove_entry(ProcFSEntry_t *entry);

    /**
     * @brief Find entry by path
     * @param path Entry path
     * @return Entry, NULL if not found
     */
    ProcFSEntry_t *procfs_find_entry(const char *path);

    /**
     * @brief Register standard procfs entries
     * @return 0 on success, negative error code on failure
     */
    int procfs_register_standard_entries(void);

    /**
     * @brief Register procfs with VFS
     * @return 0 on success, negative error code on failure
     */
    int procfs_register(void);

    /**
     * @brief Unregister procfs from VFS
     * @return 0 on success, negative error code on failure
     */
    int procfs_unregister(void);

    /**
     * @brief Get procfs superblock
     * @return Superblock
     */
    ProcFSSuper_t *procfs_get_super(void);

#ifdef __cplusplus
}
#endif

#endif /* PROCFS_H */
