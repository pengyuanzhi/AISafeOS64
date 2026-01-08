/**
 * @file initramfs.c
 * @brief AISafe64 RTOS - InitRAMFS Implementation
 *
 * @details Initial RAM filesystem implementation
 *
 * @note MISRA-C:2012 compliant
 * @note ISO 26262 ASIL-D compliant
 *
 * @version 1.0
 * @date 2025-01-08
 *
 * @author AISafe64 Team
 */

#include <fs/initramfs.h>
#include <mm.h>
#include <printk.h>
#include <string.h>
#include <time.h>

/*
 * Global State
 */

static InitRAMFSSuper_t g_initramfs_super;
static bool g_initramfs_initialized = false;

/*
 * Helper Functions
 */

/**
 * @brief Allocate new node
 */
static InitRAMFSNode_t *alloc_node(const char *name, bool is_dir)
{
    InitRAMFSNode_t *node;

    node = (InitRAMFSNode_t *)kmalloc(sizeof(InitRAMFSNode_t));
    if (node == NULL)
    {
        return NULL;
    }

    (void)memset(node, 0, sizeof(InitRAMFSNode_t));

    if (name != NULL)
    {
        (void)strncpy(node->name, name, sizeof(node->name) - 1U);
        node->name[sizeof(node->name) - 1U] = '\0';
    }

    node->is_directory = is_dir;
    node->refcount = 1U;
    node->parent = NULL;
    node->children = NULL;
    node->next = NULL;
    node->vfs_inode = NULL;

    return node;
}

/**
 * @brief Free node
 */
static void free_node(InitRAMFSNode_t *node)
{
    if (node == NULL)
    {
        return;
    }

    /* Free VFS inode if exists */
    if (node->vfs_inode != NULL)
    {
        kfree(node->vfs_inode);
    }

    kfree(node);
}

/**
 * @brief Add child to parent directory
 */
static void add_child(InitRAMFSNode_t *parent, InitRAMFSNode_t *child)
{
    InitRAMFSNode_t *prev;

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
 * @brief Remove child from parent directory
 */
static void remove_child(InitRAMFSNode_t *parent, InitRAMFSNode_t *child)
{
    InitRAMFSNode_t *prev;
    InitRAMFSNode_t *curr;

    if ((parent == NULL) || (child == NULL))
    {
        return;
    }

    prev = NULL;
    curr = parent->children;

    while (curr != NULL)
    {
        if (curr == child)
        {
            if (prev == NULL)
            {
                parent->children = curr->next;
            }
            else
            {
                prev->next = curr->next;
            }
            curr->next = NULL;
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

/**
 * @brief Find child by name
 */
static InitRAMFSNode_t *find_child(InitRAMFSNode_t *parent, const char *name)
{
    InitRAMFSNode_t *child;

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

/**
 * @brief Split path into directory and filename
 */
static void split_path(const char *path, char *dir, char *file, size_t size)
{
    const char *last_slash;
    size_t dir_len;
    size_t i;

    if (path == NULL)
    {
        return;
    }

    /* Find last slash */
    last_slash = strrchr(path, '/');
    if (last_slash == NULL)
    {
        /* No directory component */
        if (dir != NULL)
        {
            dir[0] = '\0';
        }
        if (file != NULL)
        {
            (void)strncpy(file, path, size - 1U);
            file[size - 1U] = '\0';
        }
        return;
    }

    /* Get directory length */
    dir_len = (size_t)(last_slash - path);

    /* Copy directory */
    if (dir != NULL)
    {
        if (dir_len == 0U)
        {
            dir[0] = '/';
            dir[1] = '\0';
        }
        else
        {
            (void)strncpy(dir, path, dir_len);
            dir[dir_len] = '\0';
        }
    }

    /* Copy filename */
    if (file != NULL)
    {
        i = 0U;
        last_slash++;

        while ((*last_slash != '\0') && (i < (size - 1U)))
        {
            file[i++] = *last_slash++;
        }
        file[i] = '\0';
    }
}

/*
 * VFS Operations Implementation
 */

static int initramfs_vfs_open(VFSInode_t *inode, VFSFile_t *file)
{
    InitRAMFSNode_t *node;

    if ((inode == NULL) || (file == NULL))
    {
        return -EINVAL;
    }

    node = (InitRAMFSNode_t *)inode->private_data;
    if (node == NULL)
    {
        return -EINVAL;
    }

    file->private_data = node;
    file->pos = 0ULL;

    return 0;
}

static int initramfs_vfs_close(VFSFile_t *file)
{
    if (file == NULL)
    {
        return -EINVAL;
    }

    /* Nothing to do */
    return 0;
}

static ssize_t initramfs_vfs_read(VFSFile_t *file, void *buf, uint64_t count)
{
    InitRAMFSNode_t *node;
    uint64_t offset;
    int ret;

    if ((file == NULL) || (buf == NULL))
    {
        return -EINVAL;
    }

    node = (InitRAMFSNode_t *)file->private_data;
    if (node == NULL)
    {
        return -EINVAL;
    }

    if (node->is_directory)
    {
        return -EISDIR;
    }

    offset = file->pos;

    ret = initramfs_read_file(node, buf, count, offset);
    if (ret > 0)
    {
        file->pos += (uint64_t)ret;
    }

    return (ssize_t)ret;
}

static ssize_t initramfs_vfs_write(VFSFile_t *file, const void *buf, uint64_t count)
{
    InitRAMFSNode_t *node;
    int ret;

    if ((file == NULL) || (buf == NULL))
    {
        return -EINVAL;
    }

    node = (InitRAMFSNode_t *)file->private_data;
    if (node == NULL)
    {
        return -EINVAL;
    }

    if (node->is_directory)
    {
        return -EISDIR;
    }

    /* Check if enough space */
    if ((g_initramfs_super.data_used + count) > g_initramfs_super.data_size)
    {
        return -ENOSPC;
    }

    ret = initramfs_write_file(node, buf, count);
    if (ret > 0)
    {
        file->pos = node->size;
    }

    return (ssize_t)ret;
}

static int initramfs_vfs_lookup(VFSInode_t *dir, const char *name, VFSInode_t **result)
{
    InitRAMFSNode_t *parent;
    InitRAMFSNode_t *child;
    VFSInode_t *inode;

    if ((dir == NULL) || (name == NULL) || (result == NULL))
    {
        return -EINVAL;
    }

    parent = (InitRAMFSNode_t *)dir->private_data;
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
        inode->size = child->size;
        inode->atime = child->atime;
        inode->mtime = child->mtime;
        inode->ctime = child->ctime;
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

static int initramfs_vfs_create(VFSInode_t *dir, const char *name, uint32_t mode,
                                VFSInode_t **result)
{
    InitRAMFSNode_t *parent;
    InitRAMFSNode_t *node;
    char full_path[MAX_PATH_LEN];
    VFSInode_t *inode;

    if ((dir == NULL) || (name == NULL) || (result == NULL))
    {
        return -EINVAL;
    }

    parent = (InitRAMFSNode_t *)dir->private_data;
    if (parent == NULL)
    {
        return -EINVAL;
    }

    /* Check if already exists */
    node = find_child(parent, name);
    if (node != NULL)
    {
        return -EEXIST;
    }

    /* Build full path */
    (void)snprintf(full_path, sizeof(full_path), "/%s/%s", parent->name, name);

    /* Create file */
    node = initramfs_create_file(full_path, mode);
    if (node == NULL)
    {
        return -ENOMEM;
    }

    /* Create VFS inode */
    inode = (VFSInode_t *)kmalloc(sizeof(VFSInode_t));
    if (inode == NULL)
    {
        free_node(node);
        return -ENOMEM;
    }

    (void)memset(inode, 0, sizeof(VFSInode_t));
    inode->ino = node->ino;
    inode->mode = node->mode;
    inode->size = node->size;
    inode->atime = node->atime;
    inode->mtime = node->mtime;
    inode->ctime = node->ctime;
    inode->nlink = node->refcount;
    inode->uid = node->uid;
    inode->gid = node->gid;
    inode->private_data = node;
    inode->refcnt = 1U;

    node->vfs_inode = inode;

    *result = inode;

    return 0;
}

/*
 * File Operations Structure
 */

static const FileOps_t initramfs_file_ops = {.open = initramfs_vfs_open,
                                             .close = initramfs_vfs_close,
                                             .read = initramfs_vfs_read,
                                             .write = initramfs_vfs_write,
                                             .lseek = NULL, /* Use default */
                                             .ioctl = NULL,
                                             .fstat = NULL,
                                             .fsync = NULL};

/*
 * Inode Operations Structure
 */

static const InodeOps_t initramfs_inode_ops = {.lookup = initramfs_vfs_lookup,
                                               .create = initramfs_vfs_create,
                                               .mkdir = NULL,
                                               .unlink = NULL,
                                               .rmdir = NULL,
                                               .rename = NULL,
                                               .stat = NULL};

/*
 * Superblock Operations
 */

static int initramfs_mount(VFSMount_t *mount, const char *device, const char *opts)
{
    InitRAMFSNode_t *root;
    VFSInode_t *root_inode;

    (void)device;
    (void)opts;

    if (mount == NULL)
    {
        return -EINVAL;
    }

    /* Initialize super if not already */
    if (!g_initramfs_initialized)
    {
        int ret = initramfs_init(INITRAMFS_MAX_SIZE);
        if (ret != 0)
        {
            return ret;
        }
    }

    /* Get root node */
    root = g_initramfs_super.root;
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
    root_inode->mode = root->mode;
    root_inode->size = root->size;
    root_inode->atime = root->atime;
    root_inode->mtime = root->mtime;
    root_inode->ctime = root->ctime;
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
    mount->file_ops = &initramfs_file_ops;
    mount->inode_ops = &initramfs_inode_ops;
    mount->sb_ops = NULL;
    mount->private_data = &g_initramfs_super;

    printk(KERN_INFO "initramfs: mounted on %s\n", mount->mountpoint);

    return 0;
}

static int initramfs_kill_sb(VFSMount_t *mount)
{
    (void)mount;

    printk(KERN_INFO "initramfs: unmounted\n");

    return 0;
}

/*
 * Filesystem Type Structure
 */

static FileSystemType_t g_initramfs_type = {
    .name = "initramfs", .flags = 0U, .mount = initramfs_mount, .kill_sb = initramfs_kill_sb};

/*
 * InitRAMFS API Implementation
 */

int initramfs_init(uint64_t size)
{
    InitRAMFSNode_t *root;

    if (g_initramfs_initialized)
    {
        return 0;
    }

    /* Limit size */
    if (size > INITRAMFS_MAX_SIZE)
    {
        size = INITRAMFS_MAX_SIZE;
    }

    /* Initialize super */
    (void)memset(&g_initramfs_super, 0, sizeof(g_initramfs_super));

    /* Allocate data area */
    g_initramfs_super.data = (uint8_t *)kmalloc(size);
    if (g_initramfs_super.data == NULL)
    {
        return -ENOMEM;
    }

    g_initramfs_super.data_size = size;
    g_initramfs_super.data_used = 0ULL;
    g_initramfs_super.file_count = 0U;
    g_initramfs_super.next_ino = 1ULL;

    /* Initialize lock */
    spinlock_init(&g_initramfs_super.lock);

    /* Create root directory */
    root = alloc_node("/", true);
    if (root == NULL)
    {
        kfree(g_initramfs_super.data);
        return -ENOMEM;
    }

    root->ino = g_initramfs_super.next_ino++;
    root->mode = 0755U;
    root->size = 0ULL;
    root->uid = 0U;
    root->gid = 0U;
    root->atime = get_time_ns();
    root->mtime = root->atime;
    root->ctime = root->atime;

    g_initramfs_super.root = root;
    g_initramfs_super.files = root;

    g_initramfs_initialized = true;

    printk(KERN_INFO "initramfs: initialized with %llu bytes\n", size);

    return 0;
}

InitRAMFSNode_t *initramfs_create_file(const char *path, uint32_t mode)
{
    InitRAMFSNode_t *parent;
    InitRAMFSNode_t *node;
    char dir_path[MAX_PATH_LEN];
    char file_name[MAX_NAME_LEN];
    uint64_t now;

    if (path == NULL)
    {
        return NULL;
    }

    spinlock_lock(&g_initramfs_super.lock);

    /* Split path */
    split_path(path, dir_path, file_name, sizeof(file_name));

    /* Find parent directory */
    if (strcmp(dir_path, "/") == 0)
    {
        parent = g_initramfs_super.root;
    }
    else
    {
        parent = initramfs_lookup(dir_path);
        if (parent == NULL)
        {
            spinlock_unlock(&g_initramfs_super.lock);
            return NULL;
        }
    }

    if (parent == NULL)
    {
        spinlock_unlock(&g_initramfs_super.lock);
        return NULL;
    }

    /* Check if already exists */
    node = find_child(parent, file_name);
    if (node != NULL)
    {
        spinlock_unlock(&g_initramfs_super.lock);
        return NULL;
    }

    /* Allocate node */
    node = alloc_node(file_name, false);
    if (node == NULL)
    {
        spinlock_unlock(&g_initramfs_super.lock);
        return NULL;
    }

    /* Initialize node */
    now = get_time_ns();
    node->ino = g_initramfs_super.next_ino++;
    node->mode = mode;
    node->size = 0ULL;
    node->data_offset = g_initramfs_super.data_used;
    node->uid = 0U;
    node->gid = 0U;
    node->atime = now;
    node->mtime = now;
    node->ctime = now;

    /* Add to parent */
    add_child(parent, node);

    /* Add to file list */
    node->next = g_initramfs_super.files;
    g_initramfs_super.files = node;
    g_initramfs_super.file_count++;

    spinlock_unlock(&g_initramfs_super.lock);

    return node;
}

InitRAMFSNode_t *initramfs_create_dir(const char *path, uint32_t mode)
{
    InitRAMFSNode_t *parent;
    InitRAMFSNode_t *node;
    char dir_path[MAX_PATH_LEN];
    char dir_name[MAX_NAME_LEN];
    uint64_t now;

    if (path == NULL)
    {
        return NULL;
    }

    spinlock_lock(&g_initramfs_super.lock);

    /* Split path */
    split_path(path, dir_path, dir_name, sizeof(dir_name));

    /* Find parent directory */
    if (strcmp(dir_path, "/") == 0)
    {
        parent = g_initramfs_super.root;
    }
    else
    {
        parent = initramfs_lookup(dir_path);
        if (parent == NULL)
        {
            spinlock_unlock(&g_initramfs_super.lock);
            return NULL;
        }
    }

    if (parent == NULL)
    {
        spinlock_unlock(&g_initramfs_super.lock);
        return NULL;
    }

    /* Check if already exists */
    node = find_child(parent, dir_name);
    if (node != NULL)
    {
        spinlock_unlock(&g_initramfs_super.lock);
        return NULL;
    }

    /* Allocate node */
    node = alloc_node(dir_name, true);
    if (node == NULL)
    {
        spinlock_unlock(&g_initramfs_super.lock);
        return NULL;
    }

    /* Initialize node */
    now = get_time_ns();
    node->ino = g_initramfs_super.next_ino++;
    node->mode = mode | S_IFDIR;
    node->size = 0ULL;
    node->data_offset = 0ULL;
    node->uid = 0U;
    node->gid = 0U;
    node->atime = now;
    node->mtime = now;
    node->ctime = now;

    /* Add to parent */
    add_child(parent, node);

    /* Add to file list */
    node->next = g_initramfs_super.files;
    g_initramfs_super.files = node;
    g_initramfs_super.file_count++;

    spinlock_unlock(&g_initramfs_super.lock);

    return node;
}

int initramfs_write_file(InitRAMFSNode_t *node, const void *data, uint64_t size)
{
    if ((node == NULL) || (data == NULL))
    {
        return -EINVAL;
    }

    if (node->is_directory)
    {
        return -EISDIR;
    }

    spinlock_lock(&g_initramfs_super.lock);

    /* Check if enough space */
    if ((g_initramfs_super.data_used + size) > g_initramfs_super.data_size)
    {
        spinlock_unlock(&g_initramfs_super.lock);
        return -ENOSPC;
    }

    /* Write data */
    (void)memcpy(g_initramfs_super.data + node->data_offset + node->size, data, size);
    node->size += size;
    g_initramfs_super.data_used += size;
    node->mtime = get_time_ns();

    spinlock_unlock(&g_initramfs_super.lock);

    return (int)size;
}

int initramfs_read_file(InitRAMFSNode_t *node, void *data, uint64_t size, uint64_t offset)
{
    uint64_t avail;

    if ((node == NULL) || (data == NULL))
    {
        return -EINVAL;
    }

    if (node->is_directory)
    {
        return -EISDIR;
    }

    spinlock_lock(&g_initramfs_super.lock);

    /* Check bounds */
    if (offset >= node->size)
    {
        spinlock_unlock(&g_initramfs_super.lock);
        return 0;
    }

    avail = node->size - offset;
    if (size > avail)
    {
        size = avail;
    }

    /* Read data */
    (void)memcpy(data, g_initramfs_super.data + node->data_offset + offset, size);
    node->atime = get_time_ns();

    spinlock_unlock(&g_initramfs_super.lock);

    return (int)size;
}

InitRAMFSNode_t *initramfs_lookup(const char *path)
{
    InitRAMFSNode_t *current;
    InitRAMFSNode_t *child;
    const char *start;
    const char *end;
    char name[MAX_NAME_LEN];
    size_t len;

    if (path == NULL)
    {
        return NULL;
    }

    spinlock_lock(&g_initramfs_super.lock);

    /* Start at root */
    current = g_initramfs_super.root;

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
            spinlock_unlock(&g_initramfs_super.lock);
            return NULL;
        }

        /* Copy component */
        (void)memcpy(name, start, len);
        name[len] = '\0';

        /* Find child */
        child = find_child(current, name);
        if (child == NULL)
        {
            spinlock_unlock(&g_initramfs_super.lock);
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

    spinlock_unlock(&g_initramfs_super.lock);

    return current;
}

int initramfs_unlink(const char *path)
{
    InitRAMFSNode_t *node;
    InitRAMFSNode_t *parent;

    if (path == NULL)
    {
        return -EINVAL;
    }

    spinlock_lock(&g_initramfs_super.lock);

    /* Find node */
    node = initramfs_lookup(path);
    if (node == NULL)
    {
        spinlock_unlock(&g_initramfs_super.lock);
        return -ENOENT;
    }

    /* Cannot unlink directories */
    if (node->is_directory)
    {
        spinlock_unlock(&g_initramfs_super.lock);
        return -EISDIR;
    }

    /* Get parent */
    parent = node->parent;
    if (parent == NULL)
    {
        spinlock_unlock(&g_initramfs_super.lock);
        return -EINVAL;
    }

    /* Remove from parent */
    remove_child(parent, node);

    /* Free data */
    g_initramfs_super.data_used -= node->size;

    /* Free node */
    free_node(node);
    g_initramfs_super.file_count--;

    spinlock_unlock(&g_initramfs_super.lock);

    return 0;
}

InitRAMFSSuper_t *initramfs_get_super(void)
{
    return &g_initramfs_super;
}

int initramfs_register(void)
{
    return vfs_register_filesystem(&g_initramfs_type);
}

int initramfs_unregister(void)
{
    return vfs_unregister_filesystem("initramfs");
}
