/**
 * @file app_loader.c
 * @brief Application Loader Implementation
 *
 * Boot-time ELF loader for user applications.
 * Loads applications from initramfs during system initialization.
 *
 * @note MISRA-C:2012 compliant
 * @note ISO 26262 ASIL-D compliant
 *
 * @author AISafe64 Team
 * @version 1.0
 * @date 2025-01-08
 */

#include "app_loader.h"
#include "elf_loader.h"
#include <kernel/task.h>
#include <kernel/mm.h>
#include <kernel/printk.h>
#include <kernel/string.h>
#include <crypto/sha256.h>
#include <crypto/ecdsa.h>
#include <fs/vfs.h>
#include <fs/ini_parser.h>

/* Module identification */
#define MODULE_NAME "app_loader"
#define MODULE_VERSION "1.0"

/* Error codes */
#define ERR_INVALID_CONFIG -1
#define ERR_SIGNATURE_FAIL -2
#define ERR_LOAD_FAIL -3
#define ERR_NO_MEMORY -4

/* Global state */
static bool g_initialized = false;
static AppLoaderStats_t g_stats;
static AppConfig_t g_apps[CONFIG_APP_LOADER_MAX_APPS];
static uint32_t g_app_count = 0U;

/* Forward declarations */
static int app_parse_config(const char *config_path);
static int app_load_single(const AppConfig_t *config);
static int app_create_task(const AppConfig_t *config, uint64_t entry);
static int app_validate_config_internal(const AppConfig_t *config);

/**
 * @brief Initialize application loader
 */
int app_loader_init(void)
{
    int ret = 0;

    /* Check if already initialized */
    if (g_initialized) {
        printk(KERN_WARNING "App loader already initialized\n");
        return 0;
    }

    /* Clear statistics */
    (void)memset(&g_stats, 0, sizeof(g_stats));

    /* Clear application array */
    (void)memset(g_apps, 0, sizeof(g_apps));
    g_app_count = 0U;

    /* Initialize sub-systems */
    ret = elf_loader_init();
    if (ret != 0) {
        printk(KERN_ERR "Failed to initialize ELF loader: %d\n", ret);
        return ret;
    }

    g_initialized = true;
    printk(KERN_INFO "Application loader initialized\n");

    return 0;
}

/**
 * @brief Load all applications from configuration file
 */
int app_loader_load_all(const char *config_path)
{
    uint32_t i;
    int loaded_count = 0;
    uint64_t start_time;
    uint64_t end_time;

    /* Parameter validation */
    if (config_path == NULL) {
        printk(KERN_ERR "NULL config path\n");
        return -EINVAL;
    }

    /* Check if initialized */
    if (!g_initialized) {
        printk(KERN_ERR "App loader not initialized\n");
        return -EPERM;
    }

    printk(KERN_INFO "Loading applications from %s\n", config_path);

    /* Get start time */
    start_time = get_system_time_ms();

    /* Parse configuration file */
    {
        int ret = app_parse_config(config_path);
        if (ret != 0) {
            printk(KERN_ERR "Failed to parse config: %d\n", ret);
            return ret;
        }
    }

    g_stats.total_apps = g_app_count;

    /* Load each application */
    for (i = 0U; i < g_app_count; i++) {
        const AppConfig_t *config = &g_apps[i];
        int ret;

        /* Check if enabled */
        if (!config->enabled) {
            printk(KERN_INFO "Application '%s' is disabled\n", config->name);
            g_stats.disabled_apps++;
            continue;
        }

        printk(KERN_INFO "Loading application '%s'\n", config->name);

        /* Load application */
        ret = app_load_single(config);
        if (ret != 0) {
            printk(KERN_ERR "Failed to load '%s': %d\n", config->name, ret);
            g_stats.failed_apps++;
            continue;
        }

        loaded_count++;
        g_stats.loaded_apps++;
        printk(KERN_INFO "Successfully loaded '%s'\n", config->name);
    }

    /* Get end time */
    end_time = get_system_time_ms();
    g_stats.load_time_ms = (uint32_t)(end_time - start_time);

    printk(KERN_INFO "Application loader finished: %d/%d loaded\n", loaded_count,
           g_stats.total_apps);

    return loaded_count;
}

/**
 * @brief Restart a crashed application
 */
int app_loader_restart(const char *app_name)
{
    const AppConfig_t *config;
    uint32_t i;

    /* Parameter validation */
    if (app_name == NULL) {
        return -EINVAL;
    }

    /* Find application */
    for (i = 0U; i < g_app_count; i++) {
        if (strcmp(g_apps[i].name, app_name) == 0) {
            config = &g_apps[i];
            break;
        }
    }

    if (i >= g_app_count) {
        printk(KERN_ERR "Application '%s' not found\n", app_name);
        return -ENOENT;
    }

    /* Check if auto-restart is enabled */
    if (!config->auto_restart) {
        printk(KERN_WARNING "Auto-restart disabled for '%s'\n", app_name);
        return -EPERM;
    }

    printk(KERN_INFO "Restarting application '%s'\n", app_name);

    /* Reload application */
    return app_load_single(config);
}

/**
 * @brief Application loader shutdown
 */
int app_loader_shutdown(void)
{
    /* Check if initialized */
    if (!g_initialized) {
        return 0;
    }

    printk(KERN_INFO "Application loader shutting down\n");

    /* Stop all applications */
    /* TODO: Implement application cleanup */

    /* Clear state */
    (void)memset(g_apps, 0, sizeof(g_apps));
    g_app_count = 0U;
    (void)memset(&g_stats, 0, sizeof(g_stats));

    g_initialized = false;

    return 0;
}

/**
 * @brief Get application loader statistics
 */
int app_loader_get_stats(AppLoaderStats_t *stats)
{
    if (stats == NULL) {
        return -EINVAL;
    }

    if (!g_initialized) {
        return -EPERM;
    }

    (void)memcpy(stats, &g_stats, sizeof(AppLoaderStats_t));

    return 0;
}

/**
 * @brief Find application by name
 */
const AppConfig_t *app_loader_find_app(const char *name)
{
    uint32_t i;

    if (name == NULL) {
        return NULL;
    }

    for (i = 0U; i < g_app_count; i++) {
        if (strcmp(g_apps[i].name, name) == 0) {
            return &g_apps[i];
        }
    }

    return NULL;
}

/**
 * @brief Validate application configuration
 */
bool app_loader_validate_config(const AppConfig_t *config)
{
    if (config == NULL) {
        return false;
    }

    return (app_validate_config_internal(config) == 0);
}

/**
 * @brief Parse application configuration file
 */
static int app_parse_config(const char *config_path)
{
    INIParser_t *parser;
    uint32_t i;
    int ret;

    /* Open configuration file */
    ret = ini_parser_create(config_path, &parser);
    if (ret != 0) {
        printk(KERN_ERR "Failed to open config file: %d\n", ret);
        return ret;
    }

    /* Parse each application section */
    g_app_count = 0U;

    for (i = 0U; i < CONFIG_APP_LOADER_MAX_APPS; i++) {
        AppConfig_t *app = &g_apps[i];
        char section[32];
        int value;

        /* Build section name */
        ret = snprintf(section, sizeof(section), "app%u", i);
        if ((ret < 0) || ((uint32_t)ret >= sizeof(section))) {
            break;
        }

        /* Check if section exists */
        if (!ini_parser_has_section(parser, section)) {
            break;
        }

        /* Read name */
        ret = ini_parser_get_string(parser, section, "name", app->name, sizeof(app->name));
        if (ret != 0) {
            printk(KERN_WARNING "App %u: missing name\n", i);
            continue;
        }

        /* Read path */
        ret = ini_parser_get_string(parser, section, "path", app->path, sizeof(app->path));
        if (ret != 0) {
            printk(KERN_WARNING "App %u: missing path\n", i);
            continue;
        }

        /* Validate path */
        if (app->path[0] != '/') {
            printk(KERN_WARNING "App %u: invalid path (not absolute)\n", i);
            continue;
        }

        if (strstr(app->path, "..") != NULL) {
            printk(KERN_WARNING "App %u: invalid path (contains ..)\n", i);
            continue;
        }

        /* Read priority */
        ret = ini_parser_get_int(parser, section, "priority", &value);
        app->priority = (ret == 0) ? (uint8_t)value : 128U;

        /* Read stack size */
        ret = ini_parser_get_int(parser, section, "stack_size", &value);
        app->stack_size = (ret == 0) ? (uint32_t)value : CONFIG_APP_LOADER_DEFAULT_STACK;

        /* Read CPU affinity */
        ret = ini_parser_get_int(parser, section, "cpu_affinity", &value);
        app->cpu_affinity = (ret == 0) ? (uint32_t)value : 0xFFFFFFFFU;

        /* Read max memory */
        ret = ini_parser_get_int(parser, section, "max_memory", &value);
        app->max_memory = (ret == 0) ? (uint64_t)value : 0UL;

        /* Read max CPU time */
        ret = ini_parser_get_int(parser, section, "max_cpu_time", &value);
        app->max_cpu_time = (ret == 0) ? (uint64_t)value : 0UL;

        /* Read enabled flag */
        ret = ini_parser_get_bool(parser, section, "enabled", &value);
        app->enabled = (ret == 0) ? (value != 0) : true;

        /* Read auto_restart flag */
        ret = ini_parser_get_bool(parser, section, "auto_restart", &value);
        app->auto_restart = (ret == 0) ? (value != 0) : false;

        /* Read capabilities */
        ret = ini_parser_get_int(parser, section, "capabilities", &value);
        app->capabilities = (ret == 0) ? (uint32_t)value : 0xFFFFFFFFU;

        /* Read signature (hex string) */
        ret = ini_parser_get_string(parser, section, "signature", (char *)app->signature,
                                    sizeof(app->signature));
        if (ret != 0) {
            printk(KERN_WARNING "App %u: missing signature\n", i);
            continue;
        }

        /* Validate configuration */
        if (app_validate_config_internal(app) != 0) {
            printk(KERN_WARNING "App %u: invalid configuration\n", i);
            continue;
        }

        g_app_count++;
    }

    /* Destroy parser */
    ini_parser_destroy(parser);

    printk(KERN_INFO "Parsed %u application configurations\n", g_app_count);

    return 0;
}

/**
 * @brief Load a single application
 */
static int app_load_single(const AppConfig_t *config)
{
    ElfLoadContext_t elf_ctx;
    uint64_t entry_point;
    int ret;

    /* Initialize ELF context */
    (void)memset(&elf_ctx, 0, sizeof(elf_ctx));

    /* Read ELF file */
    ret = elf_load_from_file(config->path, &elf_ctx);
    if (ret != 0) {
        return ERR_LOAD_FAIL;
    }

    /* Verify signature */
    ret = elf_verify_signature(elf_ctx.elf_data, elf_ctx.elf_size, config->signature,
                               g_system_pubkey);
    if (ret != 0) {
        free((void *)elf_ctx.elf_data);
        return ERR_SIGNATURE_FAIL;
    }

    /* Load segments */
    ret = elf_load_segments(&elf_ctx);
    if (ret != 0) {
        free((void *)elf_ctx.elf_data);
        return ERR_LOAD_FAIL;
    }

    /* Get entry point */
    entry_point = elf_get_entry_point(&elf_ctx);

    /* Create task */
    ret = app_create_task(config, entry_point);
    if (ret != 0) {
        free((void *)elf_ctx.elf_data);
        return ERR_LOAD_FAIL;
    }

    /* Free ELF data */
    free((void *)elf_ctx.elf_data);

    return 0;
}

/**
 * @brief Create task for application
 */
static int app_create_task(const AppConfig_t *config, uint64_t entry)
{
    TCB_t *tcb;
    uint8_t *stack;
    uint64_t *stack_top;

    /* Allocate stack */
    stack = (uint8_t *)kmalloc((uint64_t)config->stack_size);
    if (stack == NULL) {
        return -ENOMEM;
    }

    /* Calculate stack top */
    stack_top = (uint64_t *)(stack + config->stack_size);

    /* Create task */
    tcb = task_create(config->name, (TaskEntry_t)entry, stack_top, config->priority,
                      config->cpu_affinity);

    if (tcb == NULL) {
        kfree(stack);
        return -ENOMEM;
    }

    /* Set task attributes */
    tcb->flags = TASK_FLAG_USER_SPACE;
    tcb->capabilities = config->capabilities;
    tcb->max_memory = config->max_memory;
    tcb->max_cpu_time = config->max_cpu_time;
    tcb->auto_restart = config->auto_restart;

    printk(KERN_INFO "Created task '%s' (priority=%u, stack=%u)\n", config->name, config->priority,
           config->stack_size);

    return 0;
}

/**
 * @brief Internal configuration validation
 */
static int app_validate_config_internal(const AppConfig_t *config)
{
    /* Check name */
    if (config->name[0] == '\0') {
        return -EINVAL;
    }

    /* Check path */
    if (config->path[0] != '/') {
        return -EINVAL;
    }

    if (strstr(config->path, "..") != NULL) {
        return -EINVAL;
    }

    /* Check priority */
    if (config->priority >= PRIORITY_LEVELS) {
        return -EINVAL;
    }

    /* Check stack size */
    if ((config->stack_size < 4096U) || (config->stack_size > CONFIG_APP_LOADER_MAX_STACK_SIZE)) {
        return -EINVAL;
    }

    /* Check signature */
    /* TODO: Verify signature format */

    return 0;
}
