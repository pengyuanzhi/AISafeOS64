/**
 * @file time.h
 * @brief AISafe64 RTOS - 时间管理接口
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 时间管理接口定义
 *          - 系统滴答（jiffies）
 *          - 软件定时器
 *          - 延迟函数
 *
 * @note ARMv8-A Generic Timer
 * @note MISRA-C:2012合规
 */

#ifndef TIME_H
#define TIME_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 系统滴答频率（Hz）
 */
#define CONFIG_HZ 1000UL /**< 1000Hz = 1ms tick */

/**
 * @brief 毫秒转换为系统滴答
 */
#define MSEC_TO_TICKS(msec) ((msec) * (CONFIG_HZ / 1000UL))

/**
 * @brief 系统滴答转换为毫秒
 */
#define TICKS_TO_MSEC(ticks) ((ticks) / (CONFIG_HZ / 1000UL))

/**
 * @brief 秒转换为系统滴答
 */
#define SEC_TO_TICKS(sec) ((sec) * CONFIG_HZ)

/**
 * @brief 系统滴答变量
 * @details 自系统启动以来的滴答数
 */
extern volatile uint64_t g_jiffies;

/**
 * @brief 软件定时器结构
 */
typedef struct software_timer
{
    uint64_t expire_ticks;       /**< 过期时间（滴答数） */
    uint32_t period_ticks;       /**< 周期（滴答数），0表示一次性定时器 */
    bool active;                 /**< 是否激活 */
    bool periodic;               /**< 是否为周期性定时器 */
    void (*callback)(void *arg); /**< 回调函数 */
    void *arg;                   /**< 回调函数参数 */
    struct software_timer *next; /**< 下一个定时器（链表） */
} swtimer_t;

/**
 * @brief 时间管理初始化
 * @return 成功返回0，失败返回负错误码
 *
 * @details 初始化系统滴答和定时器管理
 *          - 初始化硬件定时器（TODO）
 *          - 初始化软件定时器链表
 */
int time_init(void);

/**
 * @brief 获取系统滴答数
 * @return 当前系统滴答数
 *
 * @details jiffies: 自系统启动以来的滴答数
 */
static inline uint64_t get_jiffies(void)
{
    return g_jiffies;
}

/**
 * @brief 获取系统时间（毫秒）
 * @return 系统运行时间（毫秒）
 */
static inline uint64_t get_system_time_ms(void)
{
    return TICKS_TO_MSEC(g_jiffies);
}

/**
 * @brief 获取系统时间（秒）
 * @return 系统运行时间（秒）
 */
static inline uint64_t get_system_time_sec(void)
{
    return g_jiffies / CONFIG_HZ;
}

/**
 * @brief 毫秒级延迟（忙等待）
 * @param ms 延迟毫秒数
 *
 * @details 忙等待，不阻塞任务
 */
void mdelay(uint64_t ms);

/**
 * @brief 微秒级延迟（忙等待）
 * @param us 延迟微秒数
 */
void udelay(uint64_t us);

/**
 * @brief 系统滴答处理函数
 * @details 由定时器中断调用
 *          - 更新jiffies
 *          - 处理软件定时器
 *          - 唤醒延迟的任务（TODO）
 */
void timer_tick(void);

/**
 * @brief 软件定时器初始化
 * @param timer 定时器指针
 * @param callback 回调函数
 * @param arg 回调函数参数
 *
 * @details 初始化软件定时器结构
 */
void swtimer_init(swtimer_t *timer, void (*callback)(void *arg), void *arg);

/**
 * @brief 启动软件定时器（一次性）
 * @param timer 定时器指针
 * @param delay_ms 延迟时间（毫秒）
 * @return 成功返回0，失败返回负错误码
 */
int swtimer_start(swtimer_t *timer, uint64_t delay_ms);

/**
 * @brief 启动软件定时器（周期性）
 * @param timer 定时器指针
 * @param delay_ms 初始延迟时间（毫秒）
 * @param period_ms 周期时间（毫秒）
 * @return 成功返回0，失败返回负错误码
 */
int swtimer_start_periodic(swtimer_t *timer, uint64_t delay_ms, uint64_t period_ms);

/**
 * @brief 停止软件定时器
 * @param timer 定时器指针
 * @return 成功返回0，失败返回负错误码
 */
int swtimer_stop(swtimer_t *timer);

/**
 * @brief 删除软件定时器
 * @param timer 定时器指针
 * @return 成功返回0，失败返回负错误码
 */
int swtimer_delete(swtimer_t *timer);

/**
 * @brief 检查定时器是否激活
 * @param timer 定时器指针
 * @return 激活返回true
 */
bool swtimer_is_active(swtimer_t *timer);

/**
 * @brief 任务睡眠（毫秒）
 * @param ms 睡眠时间（毫秒）
 * @return 成功返回0，失败返回负错误码
 *
 * @details 阻塞当前任务指定时间（TODO）
 */
int msleep(uint64_t ms);

/**
 * @brief 任务睡眠（秒）
 * @param sec 睡眠时间（秒）
 * @return 成功返回0，失败返回负错误码
 */
int ssleep(uint64_t sec);

#ifdef __cplusplus
}
#endif

#endif /* TIME_H */
