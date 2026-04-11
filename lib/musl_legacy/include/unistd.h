/**
 * @file    unistd.h
 * @brief   POSIX 标准符号接口
 * @author  AISafe64 Team
 * @date    2026-04-11
 * @version 1.0
 *
 * @details 提供 POSIX 标准符号常量和函数声明：
 *          - 标准文件描述符（STDIN/STDOUT/STDERR）
 *          - 文件 I/O 操作（read, write, close, lseek）
 *          - 进程管理（getpid, fork, execve, _exit）
 *          - 其他实用函数（sleep, usleep, pipe, dup, isatty）
 *
 * @note MISRA-C:2012 合规
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef AISAFE_UNISTD_H
#define AISAFE_UNISTD_H

#include <sys/types.h>

/* ========================================================================
 * 标准文件描述符
 * ======================================================================== */

#define STDIN_FILENO   0   /**< @brief 标准输入 */
#define STDOUT_FILENO  1   /**< @brief 标准输出 */
#define STDERR_FILENO  2   /**< @brief 标准错误 */

/* ========================================================================
 * lseek whence 值
 * ======================================================================== */

#define SEEK_SET  0   /**< @brief 文件起始位置 */
#define SEEK_CUR  1   /**< @brief 当前位置 */
#define SEEK_END  2   /**< @brief 文件末尾 */

/* ========================================================================
 * pathconf 常量
 * ======================================================================== */

#define _PC_PATH_MAX    1   /**< @brief 路径最大长度 */
#define _PC_NAME_MAX    2   /**< @brief 文件名最大长度 */

/* ========================================================================
 * 函数声明
 * ======================================================================== */

/**
 * @brief 从文件描述符读取数据
 *
 * @param fd    文件描述符
 * @param buf   接收缓冲区
 * @param count 读取字节数
 *
 * @return 成功返回读取的字节数，失败返回 -1 并设置 errno
 */
ssize_t read(int fd, void *buf, size_t count);

/**
 * @brief 向文件描述符写入数据
 *
 * @param fd    文件描述符
 * @param buf   写入缓冲区
 * @param count 写入字节数
 *
 * @return 成功返回写入的字节数，失败返回 -1 并设置 errno
 */
ssize_t write(int fd, const void *buf, size_t count);

/**
 * @brief 关闭文件描述符
 *
 * @param fd 文件描述符
 *
 * @return 成功返回 0，失败返回 -1 并设置 errno
 */
int close(int fd);

/**
 * @brief 移动文件偏移量
 *
 * @param fd     文件描述符
 * @param offset 偏移量
 * @param whence 基准位置（SEEK_SET, SEEK_CUR, SEEK_END）
 *
 * @return 成功返回新偏移量，失败返回 -1 并设置 errno
 */
off_t lseek(int fd, off_t offset, int whence);

/**
 * @brief 获取进程 ID
 *
 * @return 当前进程 ID
 */
pid_t getpid(void);

/**
 * @brief 获取父进程 ID
 *
 * @return 父进程 ID
 */
pid_t getppid(void);

/**
 * @brief 创建子进程
 *
 * @return 父进程返回子进程 ID，子进程返回 0，失败返回 -1
 */
int fork(void);

/**
 * @brief 执行程序
 *
 * @param pathname 程序路径
 * @param argv     参数数组
 * @param envp     环境变量数组
 *
 * @return 成功不返回，失败返回 -1 并设置 errno
 */
int execve(const char *pathname, char *const argv[], char *const envp[]);

/**
 * @brief 立即终止进程
 *
 * @param status 退出状态码
 */
void _exit(int status);

/**
 * @brief 休眠指定秒数
 *
 * @param seconds 休眠秒数
 *
 * @return 剩余未休眠的秒数
 */
unsigned int sleep(unsigned int seconds);

/**
 * @brief 休眠指定微秒数
 *
 * @param usec 休眠微秒数
 *
 * @return 成功返回 0，失败返回 -1
 */
int usleep(unsigned int usec);

/**
 * @brief 创建管道
 *
 * @param pipefd 输出文件描述符数组 [读端, 写端]
 *
 * @return 成功返回 0，失败返回 -1 并设置 errno
 */
int pipe(int pipefd[2]);

/**
 * @brief 复制文件描述符
 *
 * @param oldfd 源文件描述符
 *
 * @return 成功返回新文件描述符，失败返回 -1 并设置 errno
 */
int dup(int oldfd);

/**
 * @brief 复制文件描述符到指定编号
 *
 * @param oldfd 源文件描述符
 * @param newfd 目标文件描述符
 *
 * @return 成功返回新文件描述符，失败返回 -1 并设置 errno
 */
int dup2(int oldfd, int newfd);

/**
 * @brief 检查文件描述符是否为终端
 *
 * @param fd 文件描述符
 *
 * @return 是终端返回 1，否则返回 0
 */
int isatty(int fd);

/**
 * @brief 获取路径配置限制
 *
 * @param path 文件路径
 * @param name 配置项名称
 *
 * @return 配置值，失败返回 -1
 */
long pathconf(const char *path, int name);

/**
 * @brief 获取文件描述符配置限制
 *
 * @param fd   文件描述符
 * @param name 配置项名称
 *
 * @return 配置值，失败返回 -1
 */
long fpathconf(int fd, int name);

#endif /* AISAFE_UNISTD_H */
