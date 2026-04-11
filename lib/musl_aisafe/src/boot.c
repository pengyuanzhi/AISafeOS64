/**
 * @file    boot.c
 * @brief   AISafeOS64 musl C 库启动初始化
 * @version 0.1
 *
 * musl 启动流程适配：
 * - __init_libc: C 库初始化（设置 environ, pthread key 等）
 * - __sysinfo: 已在 syscall_dispatch.c 中设置
 *
 * @note AISafeOS64 使用静态链接，简化了启动流程
 */

/* musl 标准入口 __libc_start_main 适配 */
/* 静态链接时由 crt1.o 调用 __libc_start_main → main */
/* Phase 1: 先确保 __sysinfo 正确设置即可 */
