/**
 * @file    tls.c
 * @brief   AISafeOS64 musl TLS (Thread-Local Storage) 适配
 * @version 0.1
 *
 * ARM64 使用 tpidr_el0 寄存器存储 TLS 指针。
 * errno 通过 TLS 存储实现线程安全。
 *
 * musl 的 TLS 模型：
 * - TLS_ABOVE_TP: TLS 数据位于 TP (thread pointer) 之上
 * - GAP_ABOVE_TP=16: TP 与第一个 TLS 段之间有 16 字节间隔
 * - errno 存储在 TLS 区域
 *
 * @note Phase 1: 由 musl 标准 __init_tp 处理，此文件为扩展预留
 */

/* Phase 1: musl 标准 TLS 机制足够使用 */
/* Phase 2: 可添加安全增强（TLS 区域隔离、访问权限检查等） */
