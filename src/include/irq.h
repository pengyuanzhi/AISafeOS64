/**
 * @file irq.h
 * @brief AISafe64 RTOS - 中断管理接口
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 中断管理接口定义
 *          - GICv3/v4驱动接口
 *          - 中断处理函数注册
 *          - 中断使能/禁用
 *
 * @note ARMv8-A GICv3架构
 * @note MISRA-C:2012合规
 */

#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 中断号定义
 */
#define IRQ_SPURIOUS           0x000   /**< 伪中断 */
#define IRQ_RESERVED_START     0x000   /**< 保留中断起始 */
#define IRQ_RESERVED_END       0x01F   /**< 保留中断结束 */
#define IRQ_SGI_START          0x000   /**< SGI (软件触发中断) 起始 */
#define IRQ_SGI_END            0x00F   /**< SGI 结束 */
#define IRQ_PPI_START          0x010   /**< PPI (私有外设中断) 起始 */
#define IRQ_PPI_END            0x01F   /**< PPI 结束 */
#define IRQ_SPI_START          0x020   /**< SPI (共享外设中断) 起始 */
#define IRQ_SPI_END            0x3FF   /**< SPI 结束 (最大991个SPI) */

/**
 * @brief 特殊中断号
 */
#define IRQ_TIMER0             0x01E   /**< 物理定时器中断 */
#define IRQ_TIMER1             0x01F   /**< 虚拟定时器中断 */
#define IRQ_UART0              0x01    /**< UART0中断 (QEMU virt平台) */

/**
 * @brief 中断优先级
 */
#define IRQ_PRIORITY_HIGHEST   0x00    /**< 最高优先级 */
#define IRQ_PRIORITY_HIGH      0x40    /**< 高优先级 */
#define IRQ_PRIORITY_NORMAL    0x80    /**< 正常优先级 */
#define IRQ_PRIORITY_LOW       0xC0    /**< 低优先级 */
#define IRQ_PRIORITY_LOWEST    0xFF    /**< 最低优先级 */

/**
 * @brief 中断类型
 */
typedef enum {
    IRQ_TYPE_SGI = 0,         /**< 软件触发中断 (SGI) */
    IRQ_TYPE_PPI = 1,         /**< 私有外设中断 (PPI) */
    IRQ_TYPE_SPI = 2          /**< 共享外设中断 (SPI) */
} irq_type_t;

/**
 * @brief 中断触发方式
 */
typedef enum {
    IRQ_TRIGGER_LEVEL = 0,    /**< 电平触发 */
    IRQ_TRIGGER_EDGE = 1      /**< 边沿触发 */
} irq_trigger_t;

/**
 * @brief 中断处理函数类型
 * @param irq 中断号
 * @param arg 参数
 */
typedef void (*irq_handler_t)(uint32_t irq, void *arg);

/**
 * @brief 中断描述符
 */
typedef struct irq_desc {
    uint32_t irq;             /**< 中断号 */
    irq_handler_t handler;    /**< 中断处理函数 */
    void *arg;                /**< 处理函数参数 */
    irq_type_t type;          /**< 中断类型 */
    uint32_t priority;        /**< 优先级 */
    bool enabled;             /**< 是否使能 */
    struct irq_desc *next;    /**< 下一个描述符 */
} irq_desc_t;

/**
 * @brief GIC初始化
 * @return 成功返回0，失败返回负错误码
 *
 * @details 初始化ARM Generic Interrupt Controller
 *          - 初始化GIC Distributor
 *          - 初始化GIC CPU Interface
 *          - 配置默认中断
 */
int gic_init(void);

/**
 * @brief 初始化中断子系统
 * @return 成功返回0，失败返回负错误码
 *
 * @details 初始化GIC和中断管理
 */
int irq_init_subsystem(void);

/**
 * @brief 使能中断
 * @param irq 中断号
 * @return 成功返回0，失败返回负错误码
 */
int irq_enable(uint32_t irq);

/**
 * @brief 禁用中断
 * @param irq 中断号
 * @return 成功返回0，失败返回负错误码
 */
int irq_disable(uint32_t irq);

/**
 * @brief 注册中断处理函数
 * @param irq 中断号
 * @param handler 处理函数
 * @param arg 参数
 * @return 成功返回0，失败返回负错误码
 */
int irq_register_handler(uint32_t irq, irq_handler_t handler, void *arg);

/**
 * @brief 注销中断处理函数
 * @param irq 中断号
 * @return 成功返回0，失败返回负错误码
 */
int irq_unregister_handler(uint32_t irq);

/**
 * @brief 设置中断优先级
 * @param irq 中断号
 * @param priority 优先级 (0-255)
 * @return 成功返回0，失败返回负错误码
 */
int irq_set_priority(uint32_t irq, uint32_t priority);

/**
 * @brief 设置中断触发方式
 * @param irq 中断号
 * @param trigger 触发方式
 * @return 成功返回0，失败返回负错误码
 */
int irq_set_trigger(uint32_t irq, irq_trigger_t trigger);

/**
 * @brief 发送SGI (核间中断)
 * @param target_cpu 目标CPU掩码 (bit 0 = CPU0, etc.)
 * @param sgi SGI中断号 (0-15)
 * @return 成功返回0，失败返回负错误码
 */
int irq_send_sgi(uint8_t target_cpu, uint8_t sgi);

/**
 * @brief 中断处理入口（汇编调用）
 * @details 由start.S的IRQ异常处理调用
 */
void irq_handler(void);

/**
 * @brief 全局中断使能（ARMv8-A）
 */
static inline void irq_enable_global(void) {
    __asm__ volatile("msr daifclr, #2" ::: "memory");
}

/**
 * @brief 全局中断禁用（ARMv8-A）
 */
static inline void irq_disable_global(void) {
    __asm__ volatile("msr daifset, #2" ::: "memory");
}

/**
 * @brief 保存中断状态并禁用中断
 * @return 中断状态标志
 */
static inline uint64_t irq_save_and_disable(void) {
    uint64_t flags;
    __asm__ volatile("mrs %0, daif" : "=r"(flags));
    __asm__ volatile("msr daifset, #2" ::: "memory");
    return flags;
}

/**
 * @brief 恢复中断状态
 * @param flags 中断状态标志
 */
static inline void irq_restore(uint64_t flags) {
    __asm__ volatile("msr daif, %0" :: "r"(flags) : "memory");
}

#ifdef __cplusplus
}
#endif

#endif /* IRQ_H */
