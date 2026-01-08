/**
 * @file procfs.c
 * @brief AISafe64 RTOS - ProcFS Implementation
 *
 * @details Process filesystem implementation
 *
 * @note MISRA-C:2012 compliant
 * @note ISO 26262 ASIL-D compliant
 *
 * @version 1.0
 * @date 2025-01-08
 *
 * @author AISafe64 Team
 */

#include <fs/procfs.h>
#include <mm.h>
#include <printk.h>
#include <sched.h>
#include <string.h>
#include <time.h>

/*
 * Global State
 */

static ProcFSSuper_t g_procfs_super;
static bool g_procfs_initialized = false;

/*
 * Helper Functions
 */

/**
 * @brief Allocate new procfs entry
 */
static ProcFSEntry_t *alloc_entry(const char *name, ProcFSType_t type)
{
    ProcFSEntry_t *entry;

    entry = (ProcFSEntry_t *)kmalloc(sizeof(ProcFSEntry_t));
    if (entry == NULL)
    {
        return NULL;
    }

    (void)memset(entry, 0, sizeof(ProcFSEntry_t));

    if (name != NULL)
    {
        (void)strncpy(entry->name, name, sizeof(entry->name) - 1U);
        entry->name[sizeof(entry->name) - 1U] = '\0';
    }

    entry->type = type;
    entry->mode = 0444U;
    entry->uid = 0U;
    entry->gid = 0U;
    entry->refcount = 1U;
    entry->parent = NULL;
    entry->children = NULL;
    entry->next = NULL;
    entry->vfs_inode = NULL;
    entry->read_cb = NULL;
    entry->write_cb = NULL;
    entry->private_data = NULL;

    return entry;
}

/**
 * @brief Free procfs entry
 */
static void free_entry(ProcFSEntry_t *entry)
{
    if (entry == NULL)
    {
        return;
    }

    /* Free VFS inode if exists */
    if (entry->vfs_inode != NULL)
    {
        kfree(entry->vfs_inode);
    }

    kfree(entry);
}

/**
 * @brief Add child to parent
 */
static void add_child(ProcFSEntry_t *parent, ProcFSEntry_t *child)
{
    ProcFSEntry_t *prev;

    if ((parent == NULL) || (child == NULL))
    {
        return;
    }

    child->parent = parent;

    /* Add to front of children list */
    if (parent->children == NULL)
    {
        parent->children = child;
    }
    else
    {
        /* Find last child */
        prev = parent->children;
        while (prev->next != NULL)
        {
            prev = prev->next;
        }
        prev->next = child;
    }
}

/**
 * @brief Find child by name
 */
static ProcFSEntry_t *find_child(ProcFSEntry_t *parent, const char *name)
{
    ProcFSEntry_t *child;

    if ((parent == NULL) || (name == NULL))
    {
        return NULL;
    }

    child = parent->children;
    while (child != NULL)
    {
        if (strcmp(child->name, name) == 0)
        {
            return child;
        }
        child = child->next;
    }

    return NULL;
}

/*
 * Standard ProcFS Entries Callbacks
 */

/**
 * @brief /proc/version callback
 */
static ssize_t proc_version_read(ProcFSEntry_t *entry, char *buf, size_t size)
{
    int len;

    (void)entry;

    len = snprintf(buf, size,
                   "AISafe64 RTOS version 1.0.0\n"
                   "Built: %s %s\n"
                   "Compiler: GCC\n"
                   "Architecture: ARMv8-A\n"
                   "Platform: QEMU Virt Machine\n",
                   __DATE__, __TIME__);

    return (ssize_t)len;
}

/**
 * @brief /proc/cpuinfo callback
 */
static ssize_t proc_cpuinfo_read(ProcFSEntry_t *entry, char *buf, size_t size)
{
    int len;

    (void)entry;

    len = snprintf(buf, size,
                   "Processor\t: ARMv8-A Cortex-A53\n"
                   "CPU implementer\t: 0x41\n"
                   "CPU architecture\t: 8\n"
                   "CPU variant\t: 0x0\n"
                   "CPU part\t: 0xd03\n"
                   "CPU revision\t: 4\n\n"
                   "Hardware\t: QEMU Virt Machine\n"
                   "Revision\t: 0000\n");

    return (ssize_t)len;
}

/**
 * @brief /proc/meminfo callback
 */
static ssize_t proc_meminfo_read(ProcFSEntry_t *entry, char *buf, size_t size)
{
    int len;
    uint64_t total;
    uint64_t free;
    uint64_t used;

    (void)entry;

    /* Get memory info */
    total = get_total_memory();
    free = get_free_memory();
    used = total - free;

    len = snprintf(buf, size,
                   "MemTotal: %8llu kB\n"
                   "MemFree:  %8llu kB\n"
                   "MemUsed:  %8llu kB\n",
                   total / 1024ULL, free / 1024ULL, used / 1024ULL);

    return (ssize_t)len;
}

/**
 * @brief /proc/uptime callback
 */
static ssize_t proc_uptime_read(ProcFSEntry_t *entry, char *buf, size_t size)
{
    int len;
    uint64_t uptime_ns;
    uint64_t uptime_sec;
    uint64_t uptime_rem;

    (void)entry;

    uptime_ns = get_time_ns();
    uptime_sec = uptime_ns / 1000000000ULL;
    uptime_rem = (uptime_ns % 1000000000ULL) / 1000000ULL;

    len = snprintf(buf, size, "%llu.%03llu\n", uptime_sec, uptime_rem);

    return (ssize_t)len;
}

/**
 * @brief /proc/sched callback (scheduler statistics)
 */
static ssize_t proc_sched_read(ProcFSEntry_t *entry, char *buf, size_t size)
{
    int len;
    SchedStats_t stats;
    int ret;

    (void)entry;

    /* Get scheduler statistics */
    (void)memset(&stats, 0, sizeof(stats));
    ret = sched_get_stats(0U, &stats);

    if (ret == 0)
    {
        len = snprintf(buf, size,
                       "Scheduler Statistics:\n"
                       "  Running tasks:     %u\n"
                       "  Total switches:    %llu\n"
                       "  Total runtime:     %llu ns\n"
                       "  Average load:      %llu\n",
                       stats.nr_running, stats.nr_switches, stats.total_time, stats.avg_load);
    }
    else
    {
        len = snprintf(buf, size, "Scheduler statistics unavailable\n");
    }

    return (ssize_t)len;
}

/*
 * VFS Operations Implementation
 */

static int procfs_vfs_open(VFSInode_t *inode, VFSFile_t *file)
{
    ProcFSEntry_t *entry;

    if ((inode == NULL) || (file == NULL))
    {
        return -EINVAL;
    }

    entry = (ProcFSEntry_t *)inode->private_data;
    if (entry == NULL)
    {
        return -EINVAL;
    }

    file->private_data = entry;
    file->pos = 0ULL;

    return 0;
}

static int procfs_vfs_close(VFSFile_t *file)
{
    if (file == NULL)
    {
        return -EINVAL;
    }

    return 0;
}

static ssize_t procfs_vfs_read(VFSFile_t *file, void *buf, uint64_t count)
{
    ProcFSEntry_t *entry;
    char *data;
    ssize_t ret;
    uint64_t avail;

    if ((file == NULL) || (buf == NULL))
    {
        return -EINVAL;
    }

    entry = (ProcFSEntry_t *)file->private_data;
    if (entry == NULL)
    {
        return -EINVAL;
    }

    if (entry->type == PROC_TYPE_DIR)
    {
        return -EISDIR;
    }

    /* Allocate temporary buffer */
    data = (char *)kmalloc(PROCFS_MAX_DATA_SIZE);
    if (data == NULL)
    {
        return -ENOMEM;
    }

    /* Call read callback if available */
    if (entry->read_cb != NULL)
    {
        ret = entry->read_cb(entry, data, PROCFS_MAX_DATA_SIZE);
    }
    else
    {
        ret = 0;
    }

    if (ret > 0)
    {
        /* Calculate available data */
        avail = (uint64_t)ret - file->pos;
        if (count > avail)
        {
            count = avail;
        }

        /* Copy data */
        (void)memcpy(buf, data + file->pos, count);
        file->pos += count;

        ret = (ssize_t)count;
    }

    kfree(data);

    return ret;
}

static ssize_t procfs_vfs_write(VFSFile_t *file, const void *buf, uint64_t count)
{
    ProcFSEntry_t *entry;
    char *data;
    ssize_t ret;

    if ((file == NULL) || (buf == NULL))
    {
        return -EINVAL;
    }

    entry = (ProcFSEntry_t *)file->private_data;
    if (entry == NULL)
    {
        return -EINVAL;
    }

    /* Allocate temporary buffer */
    data = (char *)kmalloc(count + 1U);
    if (data == NULL)
    {
        return -ENOMEM;
    }

    (void)memcpy(data, buf, count);
    data[count] = '\0';

    /* Call write callback if available */
    if (entry->write_cb != NULL)
    {
        ret = entry->write_cb(entry, data, (size_t)count);
    }
    else
    {
        ret = -EPERM;
    }

    kfree(data);

    return ret;
}

static int procfs_vfs_lookup(VFSInode_t *dir, const char *name, VFSInode_t **result)
{
    ProcFSEntry_t *parent;
    ProcFSEntry_t *child;
    VFSInode_t *inode;

    if ((dir == NULL) || (name == NULL) || (result == NULL))
    {
        return -EINVAL;
    }

    parent = (ProcFSEntry_t *)dir->private_data;
    if (parent == NULL)
    {
        return -EINVAL;
    }

    child = find_child(parent, name);
    if (child == NULL)
    {
        return -ENOENT;
    }

    /* Create VFS inode if needed */
    if (child->vfs_inode == NULL)
    {
        inode = (VFSInode_t *)kmalloc(sizeof(VFSInode_t));
        if (inode == NULL)
        {
            return -ENOMEM;
        }

        (void)memset(inode, 0, sizeof(VFSInode_t));
        inode->ino = child->ino;
        inode->mode = child->mode;
        inode->size = 0ULL;
        inode->atime = get_time_ns();
        inode->mtime = inode->atime;
        inode->ctime = inode->atime;
        inode->nlink = child->refcount;
        inode->uid = child->uid;
        inode->gid = child->gid;
        inode->private_data = child;
        inode->refcnt = 1U;

        child->vfs_inode = inode;
    }

    *result = child->vfs_inode;

    return 0;
}

/*
 * File Operations Structure
 */

static const FileOps_t procfs_file_ops = {.open = procfs_vfs_open,
                                          .close = procfs_vfs_close,
                                          .read = procfs_vfs_read,
                                          .write = procfs_vfs_write,
                                          .lseek = NULL,
                                          .ioctl = NULL,
                                          .fstat = NULL,
                                          .fsync = NULL};

/*
 * Inode Operations Structure
 */

static const InodeOps_t procfs_inode_ops = {.lookup = procfs_vfs_lookup,
                                            .create = NULL,
                                            .mkdir = NULL,
                                            .unlink = NULL,
                                            .rmdir = NULL,
                                            .rename = NULL,
                                            .stat = NULL};

/*
 * Superblock Operations
 */

static int procfs_mount(VFSMount_t *mount, const char *device, const char *opts)
{
    ProcFSEntry_t *root;
    VFSInode_t *root_inode;

    (void)device;
    (void)opts;

    if (mount == NULL)
    {
        return -EINVAL;
    }

    /* Initialize procfs if not already */
    if (!g_procfs_initialized)
    {
        int ret = procfs_init();
        if (ret != 0)
        {
            return ret;
        }
    }

    /* Get root entry */
    root = g_procfs_super.root;
    if (root == NULL)
    {
        return -ENOMEM;
    }

    /* Create VFS inode for root */
    root_inode = (VFSInode_t *)kmalloc(sizeof(VFSInode_t));
    if (root_inode == NULL)
    {
        return -ENOMEM;
    }

    (void)memset(root_inode, 0, sizeof(VFSInode_t));
    root_inode->ino = root->ino;
    root_inode->mode = root->mode | S_IFDIR;
    root_inode->size = 0ULL;
    root_inode->atime = get_time_ns();
    root_inode->mtime = root_inode->atime;
    root_inode->ctime = root_inode->atime;
    root_inode->nlink = root->refcount;
    root_inode->uid = root->uid;
    root_inode->gid = root->gid;
    root_inode->private_data = root;
    root_inode->refcnt = 1U;
    root_inode->mount = mount;

    root->vfs_inode = root_inode;

    /* Setup mount */
    mount->root = root_inode;
    mount->covered = NULL;
    mount->file_ops = &procfs_file_ops;
    mount->inode_ops = &procfs_inode_ops;
    mount->sb_ops = NULL;
    mount->private_data = &g_procfs_super;

    printk(KERN_INFO "procfs: mounted on %s\n", mount->mountpoint);

    return 0;
}

static int procfs_kill_sb(VFSMount_t *mount)
{
    (void)mount;

    printk(KERN_INFO "procfs: unmounted\n");

    return 0;
}

/*
 * Filesystem Type Structure
 */

static FileSystemType_t g_procfs_type = {
    .name = "procfs", .flags = 0U, .mount = procfs_mount, .kill_sb = procfs_kill_sb};

/*
 * ProcFS API Implementation
 */

int procfs_init(void)
{
    ProcFSEntry_t *root;

    if (g_procfs_initialized)
    {
        return 0;
    }

    /* Initialize super */
    (void)memset(&g_procfs_super, 0, sizeof(g_procfs_super));
    g_procfs_super.entry_count = 0U;
    g_procfs_super.next_ino = 1ULL;

    /* Initialize lock */
    spinlock_init(&g_procfs_super.lock);

    /* Create root directory */
    root = alloc_entry("proc", PROC_TYPE_DIR);
    if (root == NULL)
    {
        return -ENOMEM;
    }

    root->ino = g_procfs_super.next_ino++;
    root->mode = 0555U;

    g_procfs_super.root = root;
    g_procfs_super.entries = root;

    g_procfs_initialized = true;

    printk(KERN_INFO "procfs: initialized\n");

    return 0;
}

ProcFSEntry_t *procfs_create_entry(ProcFSEntry_t *parent, const char *name, ProcFSType_t type,
                                   uint32_t mode)
{
    ProcFSEntry_t *entry;

    if (name == NULL)
    {
        return NULL;
    }

    spinlock_lock(&g_procfs_super.lock);

    /* Use root if parent is NULL */
    if (parent == NULL)
    {
        parent = g_procfs_super.root;
    }

    if (parent == NULL)
    {
        spinlock_unlock(&g_procfs_super.lock);
        return NULL;
    }

    /* Check if already exists */
    entry = find_child(parent, name);
    if (entry != NULL)
    {
        spinlock_unlock(&g_procfs_super.lock);
        return NULL;
    }

    /* Allocate entry */
    entry = alloc_entry(name, type);
    if (entry == NULL)
    {
        spinlock_unlock(&g_procfs_super.lock);
        return NULL;
    }

    entry->ino = g_procfs_super.next_ino++;
    entry->mode = mode;

    /* Add to parent */
    add_child(parent, entry);

    /* Add to entry list */
    entry->next = g_procfs_super.entries;
    g_procfs_super.entries = entry;
    g_procfs_super.entry_count++;

    spinlock_unlock(&g_procfs_super.lock);

    return entry;
}

int procfs_remove_entry(ProcFSEntry_t *entry)
{
    ProcFSEntry_t *parent;
    ProcFSEntry_t *prev;
    ProcFSEntry_t *curr;

    if (entry == NULL)
    {
        return -EINVAL;
    }

    spinlock_lock(&g_procfs_super.lock);

    /* Get parent */
    parent = entry->parent;
    if (parent == NULL)
    {
        /* Cannot remove root */
        spinlock_unlock(&g_procfs_super.lock);
        return -EINVAL;
    }

    /* Remove from parent's children list */
    prev = NULL;
    curr = parent->children;

    while (curr != NULL)
    {
        if (curr == entry)
        {
            if (prev == NULL)
            {
                parent->children = curr->next;
            }
            else
            {
                prev->next = curr->next;
            }
            break;
        }
        prev = curr;
        curr = curr->next;
    }

    /* Remove from entry list */
    prev = NULL;
    curr = g_procfs_super.entries;

    while (curr != NULL)
    {
        if (curr == entry)
        {
            if (prev == NULL)
            {
                g_procfs_super.entries = curr->next;
            }
            else
            {
                prev->next = curr->next;
            }
            break;
        }
        prev = curr;
        curr = curr->next;
    }

    g_procfs_super.entry_count--;

    spinlock_unlock(&g_procfs_super.lock);

    /* Free entry */
    free_entry(entry);

    return 0;
}

ProcFSEntry_t *procfs_find_entry(const char *path)
{
    ProcFSEntry_t *current;
    ProcFSEntry_t *child;
    const char *start;
    const char *end;
    char name[MAX_NAME_LEN];
    size_t len;

    if (path == NULL)
    {
        return NULL;
    }

    spinlock_lock(&g_procfs_super.lock);

    /* Start at root */
    current = g_procfs_super.root;

    /* Skip leading slash */
    start = path;
    if (*start == '/')
    {
        start++;
    }

    /* Traverse path */
    while (*start != '\0')
    {
        /* Find next component */
        end = start;
        while ((*end != '\0') && (*end != '/'))
        {
            end++;
        }

        /* Get component length */
        len = (size_t)(end - start);
        if (len >= sizeof(name))
        {
            spinlock_unlock(&g_procfs_super.lock);
            return NULL;
        }

        /* Copy component */
        (void)memcpy(name, start, len);
        name[len] = '\0';

        /* Find child */
        child = find_child(current, name);
        if (child == NULL)
        {
            spinlock_unlock(&g_procfs_super.lock);
            return NULL;
        }

        current = child;

        /* Move to next component */
        if (*end == '/')
        {
            start = end + 1;
        }
        else
        {
            break;
        }
    }

    spinlock_unlock(&g_procfs_super.lock);

    return current;
}

int procfs_register_standard_entries(void)
{
    ProcFSEntry_t *entry;

    /* Create /proc/version */
    entry = procfs_create_entry(NULL, "version", PROC_TYPE_DYNAMIC, 0444U);
    if (entry != NULL)
    {
        entry->read_cb = proc_version_read;
    }

    /* Create /proc/cpuinfo */
    entry = procfs_create_entry(NULL, "cpuinfo", PROC_TYPE_DYNAMIC, 0444U);
    if (entry != NULL)
    {
        entry->read_cb = proc_cpuinfo_read;
    }

    /* Create /proc/meminfo */
    entry = procfs_create_entry(NULL, "meminfo", PROC_TYPE_DYNAMIC, 0444U);
    if (entry != NULL)
    {
        entry->read_cb = proc_meminfo_read;
    }

    /* Create /proc/uptime */
    entry = procfs_create_entry(NULL, "uptime", PROC_TYPE_DYNAMIC, 0444U);
    if (entry != NULL)
    {
        entry->read_cb = proc_uptime_read;
    }

    /* Create /proc/sched */
    entry = procfs_create_entry(NULL, "sched", PROC_TYPE_DYNAMIC, 0444U);
    if (entry != NULL)
    {
        entry->read_cb = proc_sched_read;
    }

    printk(KERN_INFO "procfs: registered standard entries\n");

    return 0;
}

ProcFSSuper_t *procfs_get_super(void)
{
    return &g_procfs_super;
}

int procfs_register(void)
{
    return vfs_register_filesystem(&g_procfs_type);
}

int procfs_unregister(void)
{
    return vfs_unregister_filesystem("procfs");
}
