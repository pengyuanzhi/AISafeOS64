/**
 * @file shell.h
 * @brief AISafe64 RTOS - Debug Shell Interface
 *
 * @details Command-line debugging shell for system diagnostics
 *
 * @note MISRA-C:2012 compliant
 * @note ISO 26262 ASIL-D compliant
 *
 * @version 1.0
 * @date 2025-01-08
 *
 * @author AISafe64 Team
 */

#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Shell Constants
     */

#define SHELL_MAX_LINE 256U       /**< Maximum command line length */
#define SHELL_MAX_ARGS 16U        /**< Maximum number of arguments */
#define SHELL_MAX_CMDS 32U        /**< Maximum number of commands */
#define SHELL_PROMPT "AISafe64> " /**< Shell prompt */

    /*
     * Command Handler Type
     */

    typedef int (*ShellCmdFunc_t)(int argc, char *argv[]);

    /*
     * Command Structure
     */

    typedef struct
    {
        const char *name;    /**< Command name */
        const char *desc;    /**< Command description */
        const char *help;    /**< Detailed help text */
        ShellCmdFunc_t func; /**< Command handler */

    } ShellCommand_t;

    /*
     * Shell State
     */

    typedef struct
    {
        bool initialized;           /**< Initialization flag */
        bool running;               /**< Running flag */
        uint32_t line_len;          /**< Current line length */
        char line[SHELL_MAX_LINE];  /**< Current input line */
        char *argv[SHELL_MAX_ARGS]; /**< Parsed arguments */
        int argc;                   /**< Argument count */

    } ShellState_t;

    /*
     * Core Shell API
     */

    /**
     * @brief Initialize shell
     * @return 0 on success, negative error code on failure
     */
    int shell_init(void);

    /**
     * @brief Start shell (blocking)
     * @return Never returns unless exited
     */
    int shell_start(void);

    /**
     * @brief Stop shell
     */
    void shell_stop(void);

    /**
     * @brief Register command
     * @param cmd Command structure
     * @return 0 on success, negative error code on failure
     */
    int shell_register_command(const ShellCommand_t *cmd);

    /**
     * @brief Unregister command
     * @param name Command name
     * @return 0 on success, negative error code on failure
     */
    int shell_unregister_command(const char *name);

    /**
     * @brief Execute command string
     * @param cmd_str Command string
     * @return Command return code
     */
    int shell_execute(const char *cmd_str);

    /**
     * @brief Print shell prompt
     */
    void shell_print_prompt(void);

    /*
     * Built-in Commands
     */

    /**
     * @brief Help command
     * @param argc Argument count
     * @param argv Argument vector
     * @return 0 on success
     */
    int shell_cmd_help(int argc, char *argv[]);

    /**
     * @brief List tasks (ps) command
     * @param argc Argument count
     * @param argv Argument vector
     * @return 0 on success
     */
    int shell_cmd_ps(int argc, char *argv[]);

    /**
     * @brief Memory usage command
     * @param argc Argument count
     * @param argv Argument vector
     * @return 0 on success
     */
    int shell_cmd_mem(int argc, char *argv[]);

    /**
     * @brief Scheduler statistics command
     * @param argc Argument count
     * @param argv Argument vector
     * @return 0 on success
     */
    int shell_cmd_sched(int argc, char *argv[]);

    /**
     * @brief Mount information command
     * @param argc Argument count
     * @param argv Argument vector
     * @return 0 on success
     */
    int shell_cmd_mount(int argc, char *argv[]);

    /**
     * @brief Echo command
     * @param argc Argument count
     * @param argv Argument vector
     * @return 0 on success
     */
    int shell_cmd_echo(int argc, char *argv[]);

    /**
     * @brief Clear screen command
     * @param argc Argument count
     * @param argv Argument vector
     * @return 0 on success
     */
    int shell_cmd_clear(int argc, char *argv[]);

    /**
     * @brief Reboot command
     * @param argc Argument count
     * @param argv Argument vector
     * @return 0 on success
     */
    int shell_cmd_reboot(int argc, char *argv[]);

    /**
     * @brief Version information command
     * @param argc Argument count
     * @param argv Argument vector
     * @return 0 on success
     */
    int shell_cmd_version(int argc, char *argv[]);

    /**
     * @brief Test command
     * @param argc Argument count
     * @param argv Argument vector
     * @return 0 on success
     */
    int shell_cmd_test(int argc, char *argv[]);

    /**
     * @brief Uptime command
     * @param argc Argument count
     * @param argv Argument vector
     * @return 0 on success
     */
    int shell_cmd_uptime(int argc, char *argv[]);

    /**
     * @brief Date/time command
     * @param argc Argument count
     * @param argv Argument vector
     * @return 0 on success
     */
    int shell_cmd_date(int argc, char *argv[]);

#ifdef __cplusplus
}
#endif

#endif /* SHELL_H */
