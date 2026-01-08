/**
 * @file timer.c
 * @brief AISafe64 RTOS - 软件定时器实现
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 软件定时器实现
 *          - 一次性定时器
 *          - 周期性定时器
 *          - 链表管理
 *
 * @note 线程安全：需要在关中断或加锁环境下调用
 * @note MISRA-C:2012合规
 */

#include "time.h"
#include "types.h"
#include <stddef.h>

/**
 * @brief 软件定时器链表头
 * @details 按过期时间排序（升序）
 */
static swtimer_t *g_timer_list = NULL;

/**
 * @brief 软件定时器初始化
 * @param timer 定时器指针
 * @param callback 回调函数
 * @param arg 回调函数参数
 *
 * @details 初始化软件定时器结构
 */
void swtimer_init(swtimer_t *timer, void (*callback)(void *arg), void *arg) {
    if (timer == NULL) {
        return;
    }

    timer->expire_ticks = 0UL;
    timer->period_ticks = 0UL;
    timer->active = false;
    timer->periodic = false;
    timer->callback = callback;
    timer->arg = arg;
    timer->next = NULL;
}

/**
 * @brief 从定时器链表中移除定时器
 * @param timer 定时器指针
 * @return 成功返回0，失败返回负错误码
 */
static int timer_remove(swtimer_t *timer) {
    if (timer == NULL) {
        return -ERROR_INVALID_PARAM;
    }

    if (g_timer_list == NULL) {
        return -ERROR_NOT_FOUND;
    }

    /* 检查是否为链表头 */
    if (g_timer_list == timer) {
        g_timer_list = timer->next;
        timer->next = NULL;
        return ERROR_SUCCESS;
    }

    /* 查找定时器在链表中的位置 */
    swtimer_t *prev = g_timer_list;
    swtimer_t *current = g_timer_list->next;

    while (current != NULL) {
        if (current == timer) {
            /* 找到，移除 */
            prev->next = current->next;
            timer->next = NULL;
            return ERROR_SUCCESS;
        }

        prev = current;
        current = current->next;
    }

    return -ERROR_NOT_FOUND;
}

/**
 * @brief 将定时器插入链表（按过期时间排序）
 * @param timer 定时器指针
 */
static void timer_insert(swtimer_t *timer) {
    if (timer == NULL) {
        return;
    }

    /* 链表为空或定时器应插入头部 */
    if ((g_timer_list == NULL) || (timer->expire_ticks < g_timer_list->expire_ticks)) {
        timer->next = g_timer_list;
        g_timer_list = timer;
        return;
    }

    /* 查找插入位置 */
    swtimer_t *current = g_timer_list;
    while ((current->next != NULL) &&
           (current->next->expire_ticks <= timer->expire_ticks)) {
        current = current->next;
    }

    /* 插入定时器 */
    timer->next = current->next;
    current->next = timer;
}

/**
 * @brief 启动软件定时器（内部函数）
 * @param timer 定时器指针
 * @param delay_ms 初始延迟时间（毫秒）
 * @param period_ms 周期时间（毫秒），0表示一次性定时器
 * @return 成功返回0，失败返回负错误码
 */
static int swtimer_start_internal(swtimer_t *timer, uint64_t delay_ms, uint64_t period_ms) {
    if (timer == NULL) {
        return -ERROR_INVALID_PARAM;
    }

    if (timer->callback == NULL) {
        return -ERROR_INVALID_PARAM;
    }

    /* 如果定时器已激活，先停止 */
    if (timer->active) {
        swtimer_stop(timer);
    }

    /* 设置定时器参数 */
    timer->expire_ticks = g_jiffies + MSEC_TO_TICKS(delay_ms);
    timer->period_ticks = MSEC_TO_TICKS(period_ms);
    timer->periodic = (period_ms > 0UL);
    timer->active = true;

    /* 插入定时器链表 */
    timer_insert(timer);

    return ERROR_SUCCESS;
}

/**
 * @brief 启动软件定时器（一次性）
 * @param timer 定时器指针
 * @param delay_ms 延迟时间（毫秒）
 * @return 成功返回0，失败返回负错误码
 */
int swtimer_start(swtimer_t *timer, uint64_t delay_ms) {
    return swtimer_start_internal(timer, delay_ms, 0UL);
}

/**
 * @brief 启动软件定时器（周期性）
 * @param timer 定时器指针
 * @param delay_ms 初始延迟时间（毫秒）
 * @param period_ms 周期时间（毫秒）
 * @return 成功返回0，失败返回负错误码
 */
int swtimer_start_periodic(swtimer_t *timer, uint64_t delay_ms, uint64_t period_ms) {
    if (period_ms == 0UL) {
        return -ERROR_INVALID_PARAM;
    }

    return swtimer_start_internal(timer, delay_ms, period_ms);
}

/**
 * @brief 停止软件定时器
 * @param timer 定时器指针
 * @return 成功返回0，失败返回负错误码
 */
int swtimer_stop(swtimer_t *timer) {
    if (timer == NULL) {
        return -ERROR_INVALID_PARAM;
    }

    if (!timer->active) {
        return -ERROR_INVALID_STATE;
    }

    /* 从链表中移除 */
    timer_remove(timer);

    /* 标记为未激活 */
    timer->active = false;

    return ERROR_SUCCESS;
}

/**
 * @brief 删除软件定时器
 * @param timer 定时器指针
 * @return 成功返回0，失败返回负错误码
 */
int swtimer_delete(swtimer_t *timer) {
    if (timer == NULL) {
        return -ERROR_INVALID_PARAM;
    }

    /* 停止定时器（如果激活） */
    if (timer->active) {
        swtimer_stop(timer);
    }

    /* 清零定时器结构 */
    timer->expire_ticks = 0UL;
    timer->period_ticks = 0UL;
    timer->active = false;
    timer->periodic = false;
    timer->callback = NULL;
    timer->arg = NULL;
    timer->next = NULL;

    return ERROR_SUCCESS;
}

/**
 * @brief 检查定时器是否激活
 * @param timer 定时器指针
 * @return 激活返回true
 */
bool swtimer_is_active(swtimer_t *timer) {
    if (timer == NULL) {
        return false;
    }

    return timer->active;
}

/**
 * @brief 软件定时器滴答处理函数
 * @details 由timer_tick()调用，处理过期的定时器
 */
void swtimer_tick_handler(void) {
    swtimer_t *current = g_timer_list;

    while (current != NULL) {
        /* 检查定时器是否过期 */
        if (current->expire_ticks > g_jiffies) {
            /* 链表按过期时间排序，后续定时器都未过期 */
            break;
        }

        /* 保存下一个定时器（因为回调可能修改链表） */
        swtimer_t *next = current->next;

        /* 标记为未激活（一次性定时器） */
        if (!current->periodic) {
            current->active = false;
        }

        /* 调用回调函数 */
        if (current->callback != NULL) {
            current->callback(current->arg);
        }

        /* 处理周期性定时器 */
        if (current->periodic) {
            /* 重新计算过期时间 */
            current->expire_ticks = g_jiffies + current->period_ticks;

            /* 从链表中移除并重新插入（保持排序） */
            timer_remove(current);
            timer_insert(current);
        } else {
            /* 一次性定时器，从链表中移除 */
            timer_remove(current);
        }

        /* 继续处理下一个定时器 */
        current = next;
    }
}

/**
 * @brief 任务睡眠（毫秒）
 * @param ms 睡眠时间（毫秒）
 * @return 成功返回0，失败返回负错误码
 *
 * @details TODO: 集成任务管理器实现阻塞睡眠
 */
int msleep(uint64_t ms) {
    if (ms == 0UL) {
        return ERROR_SUCCESS;
    }

    /* 简单实现：忙等待（TODO: 改为阻塞睡眠） */
    mdelay(ms);

    return ERROR_SUCCESS;
}

/**
 * @brief 任务睡眠（秒）
 * @param sec 睡眠时间（秒）
 * @return 成功返回0，失败返回负错误码
 */
int ssleep(uint64_t sec) {
    return msleep(sec * 1000UL);
}
