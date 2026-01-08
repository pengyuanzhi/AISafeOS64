/**
 * @file device.h
 * @brief AISafe64 RTOS - Device Driver Framework
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

#ifndef DEVICE_H
#define DEVICE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Device Constants
     */

#define DEVICE_NAME_MAX_LEN 32U /**< Maximum device name length */
#define DEVICE_MAX_DRIVERS 64U  /**< Maximum number of drivers */
#define DEVICE_MAX_OPEN 256U    /**< Maximum open files */

    /*
     * Device Types
     */

    typedef enum
    {
        DEVICE_TYPE_CHAR = 0U, /**< Character device */
        DEVICE_TYPE_BLOCK,     /**< Block device */
        DEVICE_TYPE_NET,       /**< Network device */
        DEVICE_TYPE_PLATFORM   /**< Platform device */
    } DeviceType_t;

    /*
     * Device Permissions
     */

#define DEVICE_PERM_READ (1U << 0U)  /**< Read permission */
#define DEVICE_PERM_WRITE (1U << 1U) /**< Write permission */
#define DEVICE_PERM_EXEC (1U << 2U)  /**< Execute permission */

    /*
     * Forward Declarations
     */

    typedef struct Device Device_t;
    typedef struct DeviceDriver DeviceDriver_t;
    typedef struct DeviceOperations DeviceOps_t;
    typedef struct FilePrivate FilePrivate_t;

    /*
     * Device Operations Structure
     */

    struct DeviceOperations
    {
        /**
         * @brief Open device
         * @param dev Device pointer
         * @param priv Private data (per-file)
         * @return 0 on success, negative error code on failure
         */
        int (*open)(Device_t *dev, FilePrivate_t *priv);

        /**
         * @brief Close device
         * @param dev Device pointer
         * @param priv Private data (per-file)
         * @return 0 on success, negative error code on failure
         */
        int (*close)(Device_t *dev, FilePrivate_t *priv);

        /**
         * @brief Read from device
         * @param dev Device pointer
         * @param priv Private data (per-file)
         * @param buf Buffer to read into
         * @param count Number of bytes to read
         * @param offset Offset (for block devices)
         * @return Number of bytes read, negative error code on failure
         */
        ssize_t (*read)(Device_t *dev, FilePrivate_t *priv, void *buf, size_t count,
                        uint64_t offset);

        /**
         * @brief Write to device
         * @param dev Device pointer
         * @param priv Private data (per-file)
         * @param buf Buffer to write from
         * @param count Number of bytes to write
         * @param offset Offset (for block devices)
         * @return Number of bytes written, negative error code on failure
         */
        ssize_t (*write)(Device_t *dev, FilePrivate_t *priv, const void *buf, size_t count,
                         uint64_t offset);

        /**
         * @brief IOCTL operation
         * @param dev Device pointer
         * @param priv Private data (per-file)
         * @param cmd IOCTL command
         * @param arg Argument
         * @return 0 on success, negative error code on failure
         */
        int (*ioctl)(Device_t *dev, FilePrivate_t *priv, unsigned int cmd, unsigned long arg);

        /**
         * @brief Memory map operation
         * @param dev Device pointer
         * @param priv Private data (per-file)
         * @param addr Address to map
         * @param size Size to map
         * @param prot Protection flags
         * @return Mapped address, NULL on failure
         */
        void *(*mmap)(Device_t *dev, FilePrivate_t *priv, uint64_t addr, size_t size, int prot);

        /**
         * @brief Poll operation
         * @param dev Device pointer
         * @param priv Private data (per-file)
         * @return Bitmask of events
         */
        unsigned int (*poll)(Device_t *dev, FilePrivate_t *priv);
    };

    /*
     * Device Structure
     */

    struct Device
    {
        char name[DEVICE_NAME_MAX_LEN]; /**< Device name */
        DeviceType_t type;              /**< Device type */
        uint32_t permissions;           /**< Permissions */
        uint32_t flags;                 /**< Flags */

        DeviceDriver_t *driver; /**< Driver */
        void *driver_data;      /**< Driver-specific data */
        void *platform_data;    /**< Platform-specific data */

        uint32_t refcount;   /**< Reference count */
        uint32_t open_count; /**< Open count */

        struct Device *parent;   /**< Parent device */
        struct Device *children; /**< Child devices */
        struct Device *next;     /**< Next device */

        uint32_t device_id; /**< Device ID */
        bool initialized;   /**< Initialization flag */
    };

    /*
     * Device Driver Structure
     */

    struct DeviceDriver
    {
        const char *name;       /**< Driver name */
        DeviceType_t type;      /**< Supported device type */
        const DeviceOps_t *ops; /**< Operations */
        uint32_t flags;         /**< Flags */

        /**
         * @brief Probe device
         * @param dev Device to probe
         * @return 0 on success, negative error code on failure
         */
        int (*probe)(Device_t *dev);

        /**
         * @brief Remove device
         * @param dev Device to remove
         * @return 0 on success, negative error code on failure
         */
        int (*remove)(Device_t *dev);

        /**
         * @brief Shutdown driver
         * @param dev Device
         * @return 0 on success, negative error code on failure
         */
        int (*shutdown)(Device_t *dev);
    };

    /*
     * File Private Data
     */

    struct FilePrivate
    {
        uint32_t flags;     /**< File flags */
        uint64_t offset;    /**< Current offset */
        void *private_data; /**< Driver-specific data */
    };

    /*
     * Device Management API
     */

    /**
     * @brief Initialize device framework
     * @return 0 on success, negative error code on failure
     */
    int device_init(void);

    /**
     * @brief Register device driver
     * @param driver Driver structure
     * @return 0 on success, negative error code on failure
     */
    int device_register_driver(DeviceDriver_t *driver);

    /**
     * @brief Unregister device driver
     * @param name Driver name
     * @return 0 on success, negative error code on failure
     */
    int device_unregister_driver(const char *name);

    /**
     * @brief Register device
     * @param dev Device structure
     * @param parent Parent device (NULL for root)
     * @return 0 on success, negative error code on failure
     */
    int device_register(Device_t *dev, Device_t *parent);

    /**
     * @brief Unregister device
     * @param dev Device structure
     * @return 0 on success, negative error code on failure
     */
    int device_unregister(Device_t *dev);

    /**
     * @brief Find device by name
     * @param name Device name
     * @return Device pointer, NULL if not found
     */
    Device_t *device_find(const char *name);

    /**
     * @brief Open device
     * @param name Device name
     * @param flags Open flags
     * @return File private data, NULL on failure
     */
    FilePrivate_t *device_open(const char *name, uint32_t flags);

    /**
     * @brief Close device
     * @param priv File private data
     * @return 0 on success, negative error code on failure
     */
    int device_close(FilePrivate_t *priv);

    /**
     * @brief Read from device
     * @param priv File private data
     * @param buf Buffer
     * @param count Byte count
     * @return Bytes read, negative error code on failure
     */
    ssize_t device_read(FilePrivate_t *priv, void *buf, size_t count);

    /**
     * @brief Write to device
     * @param priv File private data
     * @param buf Buffer
     * @param count Byte count
     * @return Bytes written, negative error code on failure
     */
    ssize_t device_write(FilePrivate_t *priv, const void *buf, size_t count);

    /**
     * @brief IOCTL operation
     * @param priv File private data
     * @param cmd Command
     * @param arg Argument
     * @return 0 on success, negative error code on failure
     */
    int device_ioctl(FilePrivate_t *priv, unsigned int cmd, unsigned long arg);

    /*
     * Character Device Helpers
     */

    /**
     * @brief Register character device
     * @param name Device name
     * @param ops Operations
     * @return 0 on success, negative error code on failure
     */
    int device_register_char(const char *name, const DeviceOps_t *ops);

    /*
     * Standard Device Drivers
     */

    /**
     * @brief Initialize null device (/dev/null)
     * @return 0 on success, negative error code on failure
     */
    int device_null_init(void);

    /**
     * @brief Initialize zero device (/dev/zero)
     * @return 0 on success, negative error code on failure
     */
    int device_zero_init(void);

    /**
     * @brief Initialize random device (/dev/random)
     * @return 0 on success, negative error code on failure
     */
    int device_random_init(void);

    /**
     * @brief Initialize console device (/dev/console)
     * @return 0 on success, negative error code on failure
     */
    int device_console_init(void);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_H */
