/**
 * @file app_loader.h
 * @brief Application Loader - Boot-time ELF loader for user applications
 *
 * This file defines the interface for the application loader, which loads
 * user applications from initramfs at boot time. Unlike traditional dynamic
 * loading, applications are loaded once during system initialization.
 *
 * @note MISRA-C:2012 compliant
 * @note ISO 26262 ASIL-D compliant
 *
 * @author AISafe64 Team
 * @version 1.0
 * @date 2025-01-08
 */

#ifndef APP_LOADER_H
#define APP_LOADER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Configuration definitions */
#define CONFIG_APP_LOADER_MAX_APPS 8U
#define CONFIG_APP_LOADER_MAX_NAME_LEN 64U
#define CONFIG_APP_LOADER_MAX_PATH_LEN 256U
#define CONFIG_APP_LOADER_DEFAULT_STACK 8192U
#define CONFIG_APP_LOADER_MAX_STACK_SIZE 1048576U
#define CONFIG_APP_LOADER_SIGNATURE_LEN 64U

/* Capability flags */
#define CAP_HARDWARE_ACCESS (1U << 0) /* Hardware access */
#define CAP_NETWORK_ACCESS (1U << 1)  /* Network access */
#define CAP_FILE_IO (1U << 2)         /* File I/O */
#define CAP_IPC (1U << 3)             /* Inter-process communication */
#define CAP_RAW_IO (1U << 4)          /* Raw I/O */
#define CAP_TIMER (1U << 5)           /* Timer access */
#define CAP_SIGNAL (1U << 6)          /* Signal handling */

/**
 * @brief Application configuration structure
 *
 * This structure contains all configuration parameters for a single
 * user application to be loaded at boot time.
 */
typedef struct
{
    char name[CONFIG_APP_LOADER_MAX_NAME_LEN];          /* Application name */
    char path[CONFIG_APP_LOADER_MAX_PATH_LEN];          /* ELF file path (absolute) */
    char description[128];                              /* Application description */
    uint8_t priority;                                   /* Task priority (0-255) */
    uint32_t stack_size;                                /* Stack size in bytes */
    uint32_t cpu_affinity;                              /* CPU affinity mask */
    uint64_t max_memory;                                /* Maximum memory limit (bytes) */
    uint64_t max_cpu_time;                              /* Maximum CPU time (ms/s) */
    uint8_t signature[CONFIG_APP_LOADER_SIGNATURE_LEN]; /* ECDSA signature */
    uint8_t hash[32];                                   /* SHA-256 hash (pre-calculated) */
    uint32_t version;                                   /* Application version */
    bool enabled;                                       /* Whether to load this app */
    bool auto_restart;                                  /* Auto-restart on crash */
    uint32_t capabilities;                              /* Capability bitmap */
} AppConfig_t;

/**
 * @brief Application loading statistics
 */
typedef struct
{
    uint32_t total_apps;    /* Total applications in config */
    uint32_t loaded_apps;   /* Successfully loaded applications */
    uint32_t failed_apps;   /* Failed to load applications */
    uint32_t disabled_apps; /* Disabled applications */
    uint32_t total_memory;  /* Total memory used by applications */
    uint32_t load_time_ms;  /* Time taken to load all applications */
} AppLoaderStats_t;

/**
 * @brief Application loader initialization
 *
 * Initializes the application loader subsystem.
 * Must be called before any other app_loader functions.
 *
 * @return 0 on success, negative error code on failure
 *
 * @note MISRA compliance:
 *   - Rule 13.5: Check return value
 *   - Rule 21.3: Validate parameters
 *
 * @warning Must be called only once during system initialization
 * @warning Must be called before app_loader_load_all()
 */
int app_loader_init(void);

/**
 * @brief Load all applications from configuration file
 *
 * This is the main entry point for the application loader. It parses
 * the configuration file, validates signatures, loads ELF files,
 * creates tasks, and starts all applications.
 *
 * @param config_path Path to application configuration file
 *
 * @return Number of successfully loaded applications on success,
 *         negative error code on failure
 *
 * @note MISRA compliance:
 *   - Rule 13.5: Check pointer parameter
 *   - Rule 21.3: Validate path format
 *
 * @note Process:
 *   1. Parse configuration file
 *   2. For each enabled application:
 *      a. Read ELF file from initramfs
 *      b. Verify ECDSA signature
 *      c. Load ELF segments into memory
 *      d. Perform symbol relocation
 *      e. Create task with stack
 *      f. Start application
 *
 * @warning Must be called only once during boot
 * @warning Failing to load one application does not affect others
 * @warning Signature verification failure results in rejection
 *
 * @see app_loader_init()
 * @see app_loader_shutdown()
 */
int app_loader_load_all(const char *config_path);

/**
 * @brief Restart a crashed application
 *
 * Attempts to reload and restart an application that has crashed.
 * Only works if auto_restart was enabled in the configuration.
 *
 * @param app_name Name of the application to restart
 *
 * @return 0 on success, negative error code on failure
 *
 * @note MISRA compliance:
 *   - Rule 13.5: Check pointer parameter
 *   - Rule 21.3: Validate string length
 *
 * @warning Must be called from monitor thread context
 * @warning Application must support restart
 * @warning Old resources are cleaned up before restart
 */
int app_loader_restart(const char *app_name);

/**
 * @brief Application loader shutdown
 *
 * Cleans up resources used by the application loader.
 * Called during system shutdown.
 *
 * @return 0 on success, negative error code on failure
 *
 * @note MISRA compliance:
 *   - Rule 21.3: Validate state before cleanup
 *
 * @warning Stops all user applications
 * @warning Frees all application resources
 * @warning Cannot be undone
 */
int app_loader_shutdown(void);

/**
 * @brief Get application loader statistics
 *
 * Returns statistics about the application loading process.
 *
 * @param stats Output: statistics structure
 *
 * @return 0 on success, negative error code on failure
 *
 * @note MISRA compliance:
 *   - Rule 13.5: Check pointer parameter
 */
int app_loader_get_stats(AppLoaderStats_t *stats);

/**
 * @brief Find application by name
 *
 * Searches for an application with the given name.
 *
 * @param name Application name to search for
 *
 * @return Application configuration pointer, or NULL if not found
 *
 * @note MISRA compliance:
 *   - Rule 13.5: Check pointer parameter
 *   - Rule 18.1: String comparison bounded
 *
 * @warning Returned pointer is valid only if loader is active
 */
const AppConfig_t *app_loader_find_app(const char *name);

/**
 * @brief Validate application configuration
 *
 * Validates an application configuration structure.
 *
 * @param config Application configuration to validate
 *
 * @return true if valid, false otherwise
 *
 * @note MISRA compliance:
 *   - Rule 13.5: Check pointer parameter
 *   - Rule 21.3: Validate all fields
 *
 * @note Checks:
 *   - Name is non-empty
 *   - Path is absolute
 *   - Priority is in range
 *   - Stack size is reasonable
 *   - Signature is present
 *   - Capabilities are valid
 */
bool app_loader_validate_config(const AppConfig_t *config);

#endif /* APP_LOADER_H */
