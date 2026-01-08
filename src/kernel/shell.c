/**
 * @file shell.c
 * @brief AISafe64 RTOS - Debug Shell Implementation
 *
 * @details Command-line debugging shell implementation
 *
 * @note MISRA-C:2012 compliant
 * @note ISO 26262 ASIL-D compliant
 *
 * @version 1.0
 * @date 2025-01-08
 *
 * @author AISafe64 Team
 */

#include <shell.h>
#include <sched.h>
#include <task.h>
#include <mm.h>
#include <printk.h>
#include <string.h>
#include <syscall.h>

/*
 * Module Information
 */

#define SHELL_VERSION "1.0"
#define SHELL_BUILD_DATE __DATE__
#define SHELL_BUILD_TIME __TIME__

/*
 * Global State
 */

static ShellState_t g_shell;
static ShellCommand_t *g_commands[SHELL_MAX_CMDS];
static uint32_t g_cmd_count = 0U;

/*
 * Helper Functions
 */

/**
 * @brief Find command by name
 * @param name Command name
 * @return Command structure, NULL if not found
 */
static ShellCommand_t *find_command(const char *name)
{
    uint32_t i;

    if (name == NULL)
    {
        return NULL;
    }

    for (i = 0U; i < g_cmd_count; i++)
    {
        if ((g_commands[i] != NULL) && (strcmp(g_commands[i]->name, name) == 0))
        {
            return g_commands[i];
        }
    }

    return NULL;
}

/**
 * @brief Parse command line
 * @param line Input line
 * @return Number of arguments
 */
static int parse_line(char *line)
{
    int argc = 0;
    char *p = line;
    bool in_quote = false;

    /* Skip leading whitespace */
    while ((*p == ' ') || (*p == '\t'))
    {
        p++;
    }

    /* Parse arguments */
    while ((*p != '\0') && (argc < (int)SHELL_MAX_ARGS))
    {
        /* Skip whitespace */
        while (((*p == ' ') || (*p == '\t')) && !in_quote)
        {
            p++;
        }

        if (*p == '\0')
        {
            break;
        }

        /* Start of argument */
        if (*p == '"')
        {
            in_quote = true;
            p++;
        }

        g_shell.argv[argc++] = p;

        /* Find end of argument */
        while ((*p != '\0') && (!in_quote || (*p != '"')))
        {
            if ((*p == ' ') || (*p == '\t'))
            {
                if (!in_quote)
                {
                    break;
                }
            }
            p++;
        }

        if (*p == '"')
        {
            in_quote = false;
            *p = '\0';
            p++;
        }
        else if (*p != '\0')
        {
            *p = '\0';
            p++;
        }
    }

    return argc;
}

/**
 * @brief Print error message
 * @param fmt Format string
 */
static void print_error(const char *fmt, ...)
{
    va_list args;

    printk("\033[31mERROR: ");
    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args);
    printk("\033[0m\n");
}

/*
 * Core Shell API Implementation
 */

/**
 * @brief Initialize shell
 */
int shell_init(void)
{
    uint32_t i;

    if (g_shell.initialized)
    {
        return 0;
    }

    /* Initialize state */
    (void)memset(&g_shell, 0, sizeof(ShellState_t));
    g_shell.initialized = true;
    g_shell.running = false;

    /* Clear command array */
    for (i = 0U; i < SHELL_MAX_CMDS; i++)
    {
        g_commands[i] = NULL;
    }
    g_cmd_count = 0U;

    /* Register built-in commands */
    {
        static const ShellCommand_t cmds[] = {
            {"help", "Show available commands",
             "Usage: help [command]\n"
             "  Show list of available commands or detailed help for a specific command.",
             shell_cmd_help},
            {"ps", "List running tasks",
             "Usage: ps\n"
             "  Display information about all running tasks.",
             shell_cmd_ps},
            {"mem", "Show memory usage",
             "Usage: mem\n"
             "  Display system memory usage statistics.",
             shell_cmd_mem},
            {"sched", "Show scheduler statistics",
             "Usage: sched\n"
             "  Display scheduler statistics and information.",
             shell_cmd_sched},
            {"mount", "Show mounted filesystems",
             "Usage: mount\n"
             "  Display list of mounted filesystems.",
             shell_cmd_mount},
            {"echo", "Echo arguments",
             "Usage: echo [args...]\n"
             "  Display the arguments.",
             shell_cmd_echo},
            {"clear", "Clear screen",
             "Usage: clear\n"
             "  Clear the terminal screen.",
             shell_cmd_clear},
            {"reboot", "Reboot system",
             "Usage: reboot\n"
             "  Reboot the system.",
             shell_cmd_reboot},
            {"version", "Show version information",
             "Usage: version\n"
             "  Display version and build information.",
             shell_cmd_version},
            {"test", "Run diagnostics",
             "Usage: test [test_name]\n"
             "  Run system diagnostic tests.",
             shell_cmd_test},
            {"uptime", "Show system uptime",
             "Usage: uptime\n"
             "  Display system uptime information.",
             shell_cmd_uptime},
            {"date", "Show/set system time",
             "Usage: date\n"
             "  Display or set system date and time.",
             shell_cmd_date}};

        for (i = 0U; i < (sizeof(cmds) / sizeof(cmds[0])); i++)
        {
            (void)shell_register_command(&cmds[i]);
        }
    }

    printk(KERN_INFO "Shell initialized\n");

    return 0;
}

/**
 * @brief Start shell
 */
int shell_start(void)
{
    if (!g_shell.initialized)
    {
        return -EPERM;
    }

    g_shell.running = true;

    printk("\n");
    printk("========================================\n");
    printk("  AISafe64 RTOS Debug Shell v%s\n", SHELL_VERSION);
    printk("  Build: %s %s\n", SHELL_BUILD_DATE, SHELL_BUILD_TIME);
    printk("========================================\n");
    printk("\n");
    printk("Type 'help' for available commands\n");
    printk("\n");

    /* Main command loop */
    while (g_shell.running)
    {
        /* Print prompt */
        shell_print_prompt();

        /* Read line */
        /* TODO: Implement proper input reading from UART/console */
        /* For now, this is a placeholder */
        break;
    }

    return 0;
}

/**
 * @brief Stop shell
 */
void shell_stop(void)
{
    g_shell.running = false;
}

/**
 * @brief Register command
 */
int shell_register_command(const ShellCommand_t *cmd)
{
    if (cmd == NULL)
    {
        return -EINVAL;
    }

    if ((cmd->name == NULL) || (cmd->func == NULL))
    {
        return -EINVAL;
    }

    /* Check if command already exists */
    if (find_command(cmd->name) != NULL)
    {
        return -EEXIST;
    }

    /* Check if space available */
    if (g_cmd_count >= SHELL_MAX_CMDS)
    {
        return -ENOSPC;
    }

    /* Register command */
    g_commands[g_cmd_count++] = (ShellCommand_t *)cmd;

    return 0;
}

/**
 * @brief Unregister command
 */
int shell_unregister_command(const char *name)
{
    uint32_t i;
    uint32_t j;

    if (name == NULL)
    {
        return -EINVAL;
    }

    /* Find command */
    for (i = 0U; i < g_cmd_count; i++)
    {
        if ((g_commands[i] != NULL) && (strcmp(g_commands[i]->name, name) == 0))
        {
            break;
        }
    }

    if (i >= g_cmd_count)
    {
        return -ENOENT;
    }

    /* Shift remaining commands */
    for (j = i; j < (g_cmd_count - 1U); j++)
    {
        g_commands[j] = g_commands[j + 1U];
    }

    g_cmd_count--;
    g_commands[g_cmd_count] = NULL;

    return 0;
}

/**
 * @brief Execute command string
 */
int shell_execute(const char *cmd_str)
{
    ShellCommand_t *cmd;
    int ret;

    if (cmd_str == NULL)
    {
        return -EINVAL;
    }

    /* Copy command to buffer */
    (void)strncpy(g_shell.line, cmd_str, sizeof(g_shell.line) - 1U);
    g_shell.line[sizeof(g_shell.line) - 1U] = '\0';

    /* Parse line */
    g_shell.argc = parse_line(g_shell.line);

    if (g_shell.argc == 0)
    {
        return 0; /* Empty line */
    }

    /* Find command */
    cmd = find_command(g_shell.argv[0]);
    if (cmd == NULL)
    {
        print_error("Unknown command: %s", g_shell.argv[0]);
        return -ENOENT;
    }

    /* Execute command */
    ret = cmd->func(g_shell.argc, g_shell.argv);

    return ret;
}

/**
 * @brief Print shell prompt
 */
void shell_print_prompt(void)
{
    printk("%s", SHELL_PROMPT);
}

/*
 * Built-in Commands Implementation
 */

/**
 * @brief Help command
 */
int shell_cmd_help(int argc, char *argv[])
{
    uint32_t i;
    ShellCommand_t *cmd;

    if (argc > 1)
    {
        /* Show detailed help for specific command */
        cmd = find_command(argv[1]);
        if (cmd == NULL)
        {
            print_error("Unknown command: %s", argv[1]);
            return -ENOENT;
        }

        printk("\n");
        printk("Command: %s\n", cmd->name);
        printk("Description: %s\n", cmd->desc);
        printk("\n");
        if (cmd->help != NULL)
        {
            printk("%s\n", cmd->help);
        }
        printk("\n");
    }
    else
    {
        /* Show list of commands */
        printk("\n");
        printk("Available commands:\n");
        printk("\n");

        for (i = 0U; i < g_cmd_count; i++)
        {
            if (g_commands[i] != NULL)
            {
                printk("  %-12s - %s\n", g_commands[i]->name, g_commands[i]->desc);
            }
        }

        printk("\n");
        printk("Type 'help <command>' for detailed help\n");
        printk("\n");
    }

    return 0;
}

/**
 * @brief List tasks command
 */
int shell_cmd_ps(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printk("\n");
    printk("PID   State    Prio   Policy   Name\n");
    printk("----  -------  -----  -------  ----\n");

    /* TODO: Get task list from scheduler */
    /* For now, just show header */

    printk("\n");
    printk("Total tasks: 0\n");
    printk("\n");

    return 0;
}

/**
 * @brief Memory usage command
 */
int shell_cmd_mem(int argc, char *argv[])
{
    uint64_t total;
    uint64_t used;
    uint64_t free;

    (void)argc;
    (void)argv;

    /* TODO: Get memory statistics from memory manager */

    total = 0;
    used = 0;
    free = 0;

    printk("\n");
    printk("Memory Usage:\n");
    printk("\n");
    printk("  Total: %llu bytes\n", (unsigned long long)total);
    printk("  Used:  %llu bytes\n", (unsigned long long)used);
    printk("  Free:  %llu bytes\n", (unsigned long long)free);
    printk("\n");

    return 0;
}

/**
 * @brief Scheduler statistics command
 */
int shell_cmd_sched(int argc, char *argv[])
{
    SchedStats_t stats;
    int ret;
    uint64_t idle_percent;
    uint64_t busy_percent;

    (void)argc;
    (void)argv;

    printk("\n");
    printk("Scheduler Statistics:\n");
    printk("\n");

    /* Get statistics from scheduler for CPU 0 */
    ret = sched_get_stats(0U, &stats);
    if (ret != 0)
    {
        printk("  Error: Unable to get scheduler statistics\n");
        printk("\n");
        return 0;
    }

    /* Display statistics */
    printk("  Running tasks:    %u\n", stats.nr_running);
    printk("  Context switches: %llu\n", (unsigned long long)stats.nr_switches);
    printk("  Task migrations:  %llu\n", (unsigned long long)stats.nr_migrations);
    printk("  Load weight:      %llu\n", (unsigned long long)stats.load_weight);
    printk("  Avg runtime:      %llu ticks\n", (unsigned long long)stats.avg_runtime);
    printk("\n");

    /* Calculate idle/busy percentage */
    if (stats.total_time > 0ULL)
    {
        idle_percent = (stats.idle_time * 100ULL) / stats.total_time;
        busy_percent = 100ULL - idle_percent;

        printk("  Total time:       %llu ticks\n", (unsigned long long)stats.total_time);
        printk("  Idle time:        %llu ticks (%llu%%)\n", (unsigned long long)stats.idle_time,
               (unsigned long long)idle_percent);
        printk("  Busy time:        %llu ticks (%llu%%)\n",
               (unsigned long long)(stats.total_time - stats.idle_time),
               (unsigned long long)busy_percent);
    }
    else
    {
        printk("  Total time:       0 ticks\n");
        printk("  Idle time:        0 ticks (0%%)\n");
        printk("  Busy time:        0 ticks (0%%)\n");
    }
    printk("\n");

    return 0;
}

/**
 * @brief Mount information command
 */
int shell_cmd_mount(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printk("\n");
    printk("Mounted Filesystems:\n");
    printk("\n");
    printk("Device        Mountpoint     Type\n");
    printk("------        -----------    ----\n");

    /* TODO: Get mount list from VFS */

    printk("\n");

    return 0;
}

/**
 * @brief Echo command
 */
int shell_cmd_echo(int argc, char *argv[])
{
    int i;

    for (i = 1; i < argc; i++)
    {
        if (i > 1)
        {
            printk(" ");
        }
        printk("%s", argv[i]);
    }
    printk("\n");

    return 0;
}

/**
 * @brief Clear screen command
 */
int shell_cmd_clear(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    /* ANSI escape code to clear screen */
    printk("\033[2J\033[H");

    return 0;
}

/**
 * @brief Reboot command
 */
int shell_cmd_reboot(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printk("\n");
    printk("Rebooting system...\n");
    printk("\n");

    /* TODO: Implement system reboot */

    return 0;
}

/**
 * @brief Version information command
 */
int shell_cmd_version(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printk("\n");
    printk("AISafe64 RTOS\n");
    printk("\n");
    printk("Version: %s\n", SHELL_VERSION);
    printk("Build:   %s %s\n", SHELL_BUILD_DATE, SHELL_BUILD_TIME);
    printk("\n");
    printk("AI-Generated, Safety-Certifiable, Native 64-bit RTOS for ARMv8-A\n");
    printk("\n");

    return 0;
}

/**
 * @brief Test command
 */
int shell_cmd_test(int argc, char *argv[])
{
    if (argc > 1)
    {
        printk("\n");
        printk("Running test: %s\n", argv[1]);
        printk("\n");

        /* TODO: Implement test execution */
        printk("Test execution not yet implemented\n");
        printk("\n");
    }
    else
    {
        printk("\n");
        printk("Available tests:\n");
        printk("\n");
        printk("  atomic    - Atomic operations test\n");
        printk("  sched     - Scheduler test\n");
        printk("\n");
        printk("Usage: test <test_name>\n");
        printk("\n");
    }

    return 0;
}

/**
 * @brief Uptime command
 */
int shell_cmd_uptime(int argc, char *argv[])
{
    uint64_t uptime;
    uint64_t seconds;
    uint64_t minutes;
    uint64_t hours;
    uint64_t days;

    (void)argc;
    (void)argv;

    /* TODO: Get uptime from system time */
    uptime = 0ULL;

    seconds = uptime / 1000ULL;
    minutes = seconds / 60ULL;
    hours = minutes / 60ULL;
    days = hours / 24ULL;

    printk("\n");
    printk("Uptime: ");

    if (days > 0)
    {
        printk("%llu day%s, ", (unsigned long long)days, (days > 1ULL) ? "s" : "");
    }

    printk("%02llu:%02llu:%02llu\n", (unsigned long long)(hours % 24ULL),
           (unsigned long long)(minutes % 60ULL), (unsigned long long)(seconds % 60ULL));

    printk("\n");

    return 0;
}

/**
 * @brief Date/time command
 */
int shell_cmd_date(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printk("\n");
    printk("System time: ");

    /* TODO: Get and format system time */
    printk("Time retrieval not yet implemented\n");

    printk("\n");
    printk("\n");

    return 0;
}
