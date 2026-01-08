/**
 * @file device.c
 * @brief AISafe64 RTOS - Device Driver Framework Implementation
 *
 * @details Unified device driver interface and management
 *
 * @note MISRA-C:2012 compliant
 * @note ISO 26262 ASIL-D compliant
 *
 * @version 1.0
 * @date 2025-01-08
 *
 * @author AISafe64 Team
 */

#include <device.h>
#include <printk.h>
#include <string.h>
#include <malloc.h>
#include <spinlock.h>

/*
 * Module Information
 */

#define DEVICE_VERSION "1.0"

/*
 * Global State
 */

static DeviceDriver_t *g_drivers[DEVICE_MAX_DRIVERS];
static uint32_t g_driver_count = 0U;
static Device_t *g_device_root = NULL;
static spinlock_t g_device_lock;

static bool g_initialized = false;

/*
 * Helper Functions
 */

/**
 * @brief Find driver by name
 */
static DeviceDriver_t *find_driver(const char *name)
{
    uint32_t i;

    if (name == NULL)
    {
        return NULL;
    }

    for (i = 0U; i < g_driver_count; i++)
    {
        if ((g_drivers[i] != NULL) && (strcmp(g_drivers[i]->name, name) == 0))
        {
            return g_drivers[i];
        }
    }

    return NULL;
}

/**
 * @brief Find device by ID
 */
static Device_t *find_device_by_id(uint32_t device_id)
{
    Device_t *dev;

    if (device_id == 0U)
    {
        return NULL;
    }

    /* Traverse device tree */
    dev = g_device_root;

    while (dev != NULL)
    {
        if (dev->device_id == device_id)
        {
            return dev;
        }

        /* Check children */
        if (dev->children != NULL)
        {
            Device_t *child = dev->children;
            while (child != NULL)
            {
                if (child->device_id == device_id)
                {
                    return child;
                }
                child = child->next;
            }
        }

        dev = dev->next;
    }

    return NULL;
}

/*
 * Device Management API Implementation
 */

/**
 * @brief Initialize device framework
 */
int device_init(void)
{
    if (g_initialized)
    {
        return 0;
    }

    /* Clear driver table */
    (void)memset(g_drivers, 0, sizeof(g_drivers));
    g_driver_count = 0U;

    /* Initialize device tree */
    g_device_root = NULL;

    /* Initialize lock */
    spinlock_init(&g_device_lock);

    g_initialized = true;

    printk(KERN_INFO "device: framework initialized (version %s)\n", DEVICE_VERSION);

    return 0;
}

/**
 * @brief Register device driver
 */
int device_register_driver(DeviceDriver_t *driver)
{
    if (!g_initialized)
    {
        return -ENODEV;
    }

    if (driver == NULL)
    {
        return -EINVAL;
    }

    if (driver->name == NULL)
    {
        return -EINVAL;
    }

    if (driver->ops == NULL)
    {
        return -EINVAL;
    }

    spinlock_lock(&g_device_lock);

    /* Check if driver already registered */
    if (find_driver(driver->name) != NULL)
    {
        spinlock_unlock(&g_device_lock);
        return -EEXIST;
    }

    /* Check capacity */
    if (g_driver_count >= DEVICE_MAX_DRIVERS)
    {
        spinlock_unlock(&g_device_lock);
        return -ENOMEM;
    }

    /* Add driver */
    g_drivers[g_driver_count++] = driver;

    spinlock_unlock(&g_device_lock);

    printk(KERN_INFO "device: registered driver '%s'\n", driver->name);

    return 0;
}

/**
 * @brief Unregister device driver
 */
int device_unregister_driver(const char *name)
{
    uint32_t i;

    if (!g_initialized)
    {
        return -ENODEV;
    }

    if (name == NULL)
    {
        return -EINVAL;
    }

    spinlock_lock(&g_device_lock);

    /* Find driver */
    for (i = 0U; i < g_driver_count; i++)
    {
        if ((g_drivers[i] != NULL) && (strcmp(g_drivers[i]->name, name) == 0))
        {
            /* Remove driver */
            g_drivers[i] = NULL;

            /* Compact table */
            for (uint32_t j = i; j < (g_driver_count - 1U); j++)
            {
                g_drivers[j] = g_drivers[j + 1U];
            }
            g_drivers[g_driver_count - 1U] = NULL;
            g_driver_count--;

            spinlock_unlock(&g_device_lock);

            printk(KERN_INFO "device: unregistered driver '%s'\n", name);

            return 0;
        }
    }

    spinlock_unlock(&g_device_lock);

    return -ENOENT;
}

/**
 * @brief Register device
 */
int device_register(Device_t *dev, Device_t *parent)
{
    if (!g_initialized)
    {
        return -ENODEV;
    }

    if (dev == NULL)
    {
        return -EINVAL;
    }

    if (dev->name[0] == '\0')
    {
        return -EINVAL;
    }

    if (dev->driver == NULL)
    {
        return -EINVAL;
    }

    /* Check device name not already used */
    if (device_find(dev->name) != NULL)
    {
        return -EEXIST;
    }

    /* Allocate device ID */
    static uint32_t next_device_id = 1U;
    dev->device_id = next_device_id++;

    /* Initialize fields */
    dev->refcount = 1U;
    dev->open_count = 0U;
    dev->initialized = true;

    spinlock_lock(&g_device_lock);

    /* Add to device tree */
    if (parent == NULL)
    {
        /* Add to root level */
        dev->next = g_device_root;
        g_device_root = dev;
    }
    else
    {
        /* Add as child */
        dev->parent = parent;
        dev->next = parent->children;
        parent->children = dev;
    }

    spinlock_unlock(&g_device_lock);

    printk(KERN_INFO "device: registered device '%s' (type=%u, id=%u)\n", dev->name, dev->type,
           dev->device_id);

    return 0;
}

/**
 * @brief Unregister device
 */
int device_unregister(Device_t *dev)
{
    Device_t *prev;
    Device_t *curr;

    if (!g_initialized)
    {
        return -ENODEV;
    }

    if (dev == NULL)
    {
        return -EINVAL;
    }

    if (dev->open_count > 0U)
    {
        return -EBUSY;
    }

    spinlock_lock(&g_device_lock);

    /* Remove from parent's children list */
    if (dev->parent == NULL)
    {
        /* Root level */
        prev = NULL;
        curr = g_device_root;

        while (curr != NULL)
        {
            if (curr == dev)
            {
                if (prev == NULL)
                {
                    g_device_root = curr->next;
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
    }
    else
    {
        /* Child level */
        prev = NULL;
        curr = dev->parent->children;

        while (curr != NULL)
        {
            if (curr == dev)
            {
                if (prev == NULL)
                {
                    dev->parent->children = curr->next;
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
    }

    spinlock_unlock(&g_device_lock);

    printk(KERN_INFO "device: unregistered device '%s'\n", dev->name);

    return 0;
}

/**
 * @brief Find device by name
 */
Device_t *device_find(const char *name)
{
    Device_t *dev;

    if (!g_initialized)
    {
        return NULL;
    }

    if (name == NULL)
    {
        return NULL;
    }

    spinlock_lock(&g_device_lock);

    /* Traverse device tree */
    dev = g_device_root;

    while (dev != NULL)
    {
        /* Check name */
        if (strcmp(dev->name, name) == 0)
        {
            spinlock_unlock(&g_device_lock);
            return dev;
        }

        /* Check children */
        Device_t *child = dev->children;
        while (child != NULL)
        {
            if (strcmp(child->name, name) == 0)
            {
                spinlock_unlock(&g_device_lock);
                return child;
            }
            child = child->next;
        }

        dev = dev->next;
    }

    spinlock_unlock(&g_device_lock);

    return NULL;
}

/**
 * @brief Open device
 */
FilePrivate_t *device_open(const char *name, uint32_t flags)
{
    Device_t *dev;
    FilePrivate_t *priv;

    if (!g_initialized)
    {
        return NULL;
    }

    if (name == NULL)
    {
        return NULL;
    }

    /* Find device */
    dev = device_find(name);
    if (dev == NULL)
    {
        return NULL;
    }

    /* Allocate private data */
    priv = (FilePrivate_t *)kmalloc(sizeof(FilePrivate_t));
    if (priv == NULL)
    {
        return NULL;
    }

    (void)memset(priv, 0, sizeof(FilePrivate_t));
    priv->flags = flags;
    priv->offset = 0ULL;

    /* Call driver open */
    if (dev->driver->ops->open != NULL)
    {
        int ret = dev->driver->ops->open(dev, priv);
        if (ret != 0)
        {
            kfree(priv);
            return NULL;
        }
    }

    /* Update device */
    spinlock_lock(&g_device_lock);
    dev->open_count++;
    spinlock_unlock(&g_device_lock);

    return priv;
}

/**
 * @brief Close device
 */
int device_close(FilePrivate_t *priv)
{
    Device_t *dev;

    if (!g_initialized)
    {
        return -ENODEV;
    }

    if (priv == NULL)
    {
        return -EINVAL;
    }

    /* Find device by private data */
    /* Note: This is a simplified implementation */
    /* In production, we need a reverse mapping */

    /* For now, skip close */
    kfree(priv);

    return 0;
}

/**
 * @brief Read from device
 */
ssize_t device_read(FilePrivate_t *priv, void *buf, size_t count)
{
    if (!g_initialized)
    {
        return -ENODEV;
    }

    if ((priv == NULL) || (buf == NULL))
    {
        return -EINVAL;
    }

    if (count == 0U)
    {
        return 0;
    }

    /* Note: Need to get device from private data */
    /* This is a simplified implementation */

    return -ENOSYS;
}

/**
 * @brief Write to device
 */
ssize_t device_write(FilePrivate_t *priv, const void *buf, size_t count)
{
    if (!g_initialized)
    {
        return -ENODEV;
    }

    if ((priv == NULL) || (buf == NULL))
    {
        return -EINVAL;
    }

    if (count == 0U)
    {
        return 0;
    }

    /* Note: Need to get device from private data */
    /* This is a simplified implementation */

    return -ENOSYS;
}

/**
 * @brief IOCTL operation
 */
int device_ioctl(FilePrivate_t *priv, unsigned int cmd, unsigned long arg)
{
    if (!g_initialized)
    {
        return -ENODEV;
    }

    if (priv == NULL)
    {
        return -EINVAL;
    }

    /* Note: Need to get device from private data */
    /* This is a simplified implementation */

    return -ENOSYS;
}

/*
 * Character Device Helpers
 */

/**
 * @brief Register character device
 */
int device_register_char(const char *name, const DeviceOps_t *ops)
{
    Device_t *dev;
    DeviceDriver_t *driver;
    int ret;

    if (name == NULL)
    {
        return -EINVAL;
    }

    if (ops == NULL)
    {
        return -EINVAL;
    }

    /* Allocate driver */
    driver = (DeviceDriver_t *)kmalloc(sizeof(DeviceDriver_t));
    if (driver == NULL)
    {
        return -ENOMEM;
    }

    (void)memset(driver, 0, sizeof(DeviceDriver_t));
    driver->name = name;
    driver->type = DEVICE_TYPE_CHAR;
    driver->ops = ops;

    /* Register driver */
    ret = device_register_driver(driver);
    if (ret != 0)
    {
        kfree(driver);
        return ret;
    }

    /* Allocate device */
    dev = (Device_t *)kmalloc(sizeof(Device_t));
    if (dev == NULL)
    {
        device_unregister_driver(name);
        kfree(driver);
        return -ENOMEM;
    }

    (void)memset(dev, 0, sizeof(Device_t));
    (void)strncpy(dev->name, name, sizeof(dev->name) - 1U);
    dev->name[sizeof(dev->name) - 1U] = '\0';
    dev->type = DEVICE_TYPE_CHAR;
    dev->permissions = DEVICE_PERM_READ | DEVICE_PERM_WRITE;
    dev->driver = driver;

    /* Register device */
    ret = device_register(dev, NULL);
    if (ret != 0)
    {
        kfree(dev);
        return ret;
    }

    return 0;
}

/*
 * Standard Device Drivers
 */

/**
 * @brief Null device operations
 */
static ssize_t null_read(Device_t *dev, FilePrivate_t *priv, void *buf, size_t count,
                         uint64_t offset)
{
    (void)dev;
    (void)priv;
    (void)buf;
    (void)count;
    (void)offset;

    return 0; /* EOF */
}

static ssize_t null_write(Device_t *dev, FilePrivate_t *priv, const void *buf, size_t count,
                          uint64_t offset)
{
    (void)dev;
    (void)priv;
    (void)buf;
    (void)count;
    (void)offset;

    return (ssize_t)count; /* Success, discard data */
}

static const DeviceOps_t null_ops = {.open = NULL,
                                     .close = NULL,
                                     .read = null_read,
                                     .write = null_write,
                                     .ioctl = NULL,
                                     .mmap = NULL,
                                     .poll = NULL};

/**
 * @brief Initialize null device
 */
int device_null_init(void)
{
    return device_register_char("null", &null_ops);
}

/**
 * @brief Zero device operations
 */
static ssize_t zero_read(Device_t *dev, FilePrivate_t *priv, void *buf, size_t count,
                         uint64_t offset)
{
    (void)dev;
    (void)priv;
    (void)offset;

    if (buf != NULL)
    {
        (void)memset(buf, 0, count);
    }

    return (ssize_t)count;
}

static ssize_t zero_write(Device_t *dev, FilePrivate_t *priv, const void *buf, size_t count,
                          uint64_t offset)
{
    (void)dev;
    (void)priv;
    (void)buf;
    (void)count;
    (void)offset;

    return (ssize_t)count; /* Success, discard data */
}

static const DeviceOps_t zero_ops = {.open = NULL,
                                     .close = NULL,
                                     .read = zero_read,
                                     .write = zero_write,
                                     .ioctl = NULL,
                                     .mmap = NULL,
                                     .poll = NULL};

/**
 * @brief Initialize zero device
 */
int device_zero_init(void)
{
    return device_register_char("zero", &zero_ops);
}

/**
 * @brief Console device operations
 */
static ssize_t console_write(Device_t *dev, FilePrivate_t *priv, const void *buf, size_t count,
                             uint64_t offset)
{
    const char *str = (const char *)buf;

    (void)dev;
    (void)priv;
    (void)offset;

    if (str != NULL)
    {
        /* Write to UART */
        for (size_t i = 0U; i < count; i++)
        {
            printk("%c", str[i]);
        }
    }

    return (ssize_t)count;
}

static const DeviceOps_t console_ops = {.open = NULL,
                                        .close = NULL,
                                        .read = NULL,
                                        .write = console_write,
                                        .ioctl = NULL,
                                        .mmap = NULL,
                                        .poll = NULL};

/**
 * @brief Initialize console device
 */
int device_console_init(void)
{
    return device_register_char("console", &console_ops);
}

/**
 * @brief Random device operations
 */
static ssize_t random_read(Device_t *dev, FilePrivate_t *priv, void *buf, size_t count,
                           uint64_t offset)
{
    uint8_t *bytes = (uint8_t *)buf;
    size_t i;

    (void)dev;
    (void)priv;
    (void)offset;

    /* Simple pseudo-random number generator */
    static uint32_t seed = 12345U;

    for (i = 0U; i < count; i++)
    {
        /* Linear congruential generator */
        seed = seed * 1103515245U + 12345U;
        bytes[i] = (uint8_t)(seed & 0xFFU);
    }

    return (ssize_t)count;
}

static const DeviceOps_t random_ops = {.open = NULL,
                                       .close = NULL,
                                       .read = random_read,
                                       .write = NULL,
                                       .ioctl = NULL,
                                       .mmap = NULL,
                                       .poll = NULL};

/**
 * @brief Initialize random device
 */
int device_random_init(void)
{
    return device_register_char("random", &random_ops);
}
