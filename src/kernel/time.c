/**
 * @file time.c
 * @brief AISafe64 RTOS - 时间管理实现
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 时间管理实现
 *          - 系统滴答（jiffies）
 *          - 延迟函数（mdelay, udelay）
 *          - 系统滴答处理
 *
 * @note ARMv8-A Generic Timer
 * @note MISRA-C:2012合规
 */

#include "time.h"
#include "sched.h"
#include "printk.h"

/**
 * @brief 系统滴答变量
 * @details 自系统启动以来的滴答数
 */
volatile uint64_t g_jiffies = 0UL;

/**
 * @brief ARMv8-A Generic Timer频率读取
 * @return 定时器频率（Hz）
 */
static uint64_t read_timer_freq(void)
{
    uint64_t freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    return freq;
}

/**
 * @brief ARMv8-A Generic Timer计数读取
 * @return 当前计数值
 */
static uint64_t read_timer_count(void)
{
    uint64_t count;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(count));
    return count;
}

/**
 * @brief 微秒级延迟（忙等待）
 * @param us 延迟微秒数
 *
 * @details 使用ARMv8-A Generic Timer实现精确延迟
 */
void udelay(uint64_t us)
{
    uint64_t freq = read_timer_freq();
    uint64_t start = read_timer_count();
    uint64_t ticks = (freq * us) / 1000000UL;

    while ((read_timer_count() - start) < ticks) {
        __asm__ volatile("wfe");
    }
}

/**
 * @brief 毫秒级延迟（忙等待）
 * @param ms 延迟毫秒数
 *
 * @details 基于udelay实现
 */
void mdelay(uint64_t ms)
{
    for (uint64_t i = 0UL; i < ms; i++) {
        udelay(1000UL);
    }
}

/**
 * @brief 时间管理初始化
 * @return 成功返回0，失败返回负错误码
 *
 * @details 初始化系统滴答和定时器管理
 */
int time_init(void)
{
    g_jiffies = 0UL;

    /* 打印定时器信息 */
    uint64_t freq = read_timer_freq();
    printk("[INIT] ARMv8-A Generic Timer frequency: %lu Hz\n", freq);
    printk("[INIT] System tick rate: %lu Hz (%lu ms per tick)\n", CONFIG_HZ, 1000UL / CONFIG_HZ);

    /* TODO: 初始化硬件定时器以产生周期性中断 */
    printk("[WARNING] Hardware timer not initialized yet\n");

    return 0;
}

/**
 * @brief 系统滴答处理函数
 * @details 由定时器中断调用
 *          - 更新jiffies
 *          - 处理软件定时器
 *          - 唤醒睡眠到期的任务
 */
extern void swtimer_tick_handler(void);

void timer_tick(void)
{
    /* 更新系统滴答 */
    g_jiffies++;

    /* 处理软件定时器 */
    swtimer_tick_handler();

    /* 检查并唤醒睡眠到期的任务 */
    check_sleeping_tasks();
}
