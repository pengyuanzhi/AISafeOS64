/**
 * @file    pthread_arch.h
 * @brief   AISafeOS64 musl 适配层 — 线程本地存储
 *
 * 使用 ARM64 tpidr_el0 寄存器存储 TLS 指针。
 * 与标准 musl/aarch64 相同，因为这是硬件特性，不依赖内核。
 */

static inline uintptr_t __get_tp()
{
    uintptr_t tp;
    __asm__ ("mrs %0,tpidr_el0" : "=r"(tp));
    return tp;
}

#define TLS_ABOVE_TP
#define GAP_ABOVE_TP 16

#define MC_PC pc
