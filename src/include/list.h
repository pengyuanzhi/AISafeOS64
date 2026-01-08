/**
 * @file list.h
 * @brief AISafe64 RTOS - 双向链表
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 双向链表实现（Linux kernel风格）
 *          - 内嵌式链表节点
 *          - O(1)插入/删除
 *
 * @note MISRA-C:2012合规
 */

#ifndef LIST_H
#define LIST_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 双向链表节点
     */
    struct list_head
    {
        struct list_head *next;
        struct list_head *prev;
    };

/**
 * @brief 初始化链表头
 */
#define LIST_HEAD_INIT(name) {&(name), &(name)}

/**
 * @brief 定义并初始化链表头
 */
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)

    /**
     * @brief 初始化链表节点
     */
    static inline void INIT_LIST_HEAD(struct list_head *list)
    {
        list->next = list;
        list->prev = list;
    }

    /**
     * @brief 在两个节点之间插入新节点
     */
    static inline void __list_add(struct list_head *new_node, struct list_head *prev,
                                  struct list_head *next)
    {
        next->prev = new_node;
        new_node->next = next;
        new_node->prev = prev;
        prev->next = new_node;
    }

    /**
     * @brief 在链表头部插入节点
     */
    static inline void list_add(struct list_head *new_node, struct list_head *head)
    {
        __list_add(new_node, head, head->next);
    }

    /**
     * @brief 在链表尾部插入节点
     */
    static inline void list_add_tail(struct list_head *new_node, struct list_head *head)
    {
        __list_add(new_node, head->prev, head);
    }

    /**
     * @brief 删除节点
     */
    static inline void __list_del(struct list_head *prev, struct list_head *next)
    {
        next->prev = prev;
        prev->next = next;
    }

    /**
     * @brief 删除节点
     */
    static inline void list_del(struct list_head *entry)
    {
        __list_del(entry->prev, entry->next);
        entry->next = NULL;
        entry->prev = NULL;
    }

    /**
     * @brief 删除节点并初始化
     */
    static inline void list_del_init(struct list_head *entry)
    {
        __list_del(entry->prev, entry->next);
        INIT_LIST_HEAD(entry);
    }

    /**
     * @brief 替换节点
     */
    static inline void list_replace(struct list_head *old, struct list_head *new_node)
    {
        new_node->next = old->next;
        new_node->next->prev = new_node;
        new_node->prev = old->prev;
        new_node->prev->next = new_node;
    }

    /**
     * @brief 替换节点并初始化
     */
    static inline void list_replace_init(struct list_head *old, struct list_head *new_node)
    {
        list_replace(old, new_node);
        INIT_LIST_HEAD(old);
    }

    /**
     * @brief 移动节点到新链表头部
     */
    static inline void list_move(struct list_head *list, struct list_head *head)
    {
        __list_del(list->prev, list->next);
        list_add(list, head);
    }

    /**
     * @brief 移动节点到新链表尾部
     */
    static inline void list_move_tail(struct list_head *list, struct list_head *head)
    {
        __list_del(list->prev, list->next);
        list_add_tail(list, head);
    }

    /**
     * @brief 检查链表是否为空
     */
    static inline int list_empty(const struct list_head *head)
    {
        return head->next == head;
    }

    /**
     * @brief 检查链表是否只包含一个节点
     */
    static inline int list_is_singular(const struct list_head *head)
    {
        return !list_empty(head) && (head->next == head->prev);
    }

    /**
     * @brief 切割链表
     */
    static inline void __list_cut_position(struct list_head *list, struct list_head *head,
                                           struct list_head *entry)
    {
        struct list_head *new_first = entry->next;
        list->next = head->next;
        list->next->prev = list;
        list->prev = entry;
        entry->next = list;
        head->next = new_first;
        new_first->prev = head;
    }

    /**
     * @brief 切割链表
     */
    static inline void list_cut_position(struct list_head *list, struct list_head *head,
                                         struct list_head *entry)
    {
        if (list_empty(head))
        {
            return;
        }
        if (list_is_singular(head) && (head->next != entry))
        {
            return;
        }
        __list_cut_position(list, head, entry);
    }

    /**
     * @brief 拼接链表
     */
    static inline void __list_splice(const struct list_head *list, struct list_head *prev,
                                     struct list_head *next)
    {
        struct list_head *first = list->next;
        struct list_head *last = list->prev;
        first->prev = prev;
        prev->next = first;
        last->next = next;
        next->prev = last;
    }

    /**
     * @brief 拼接链表
     */
    static inline void list_splice(const struct list_head *list, struct list_head *head)
    {
        if (!list_empty(list))
        {
            __list_splice(list, head, head->next);
        }
    }

    /**
     * @brief 拼接链表并初始化
     */
    static inline void list_splice_init(struct list_head *list, struct list_head *head)
    {
        if (!list_empty(list))
        {
            __list_splice(list, head, head->next);
            INIT_LIST_HEAD(list);
        }
    }

/**
 * @brief 获取包含该节点的结构体指针
 */
#define list_entry(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))

/**
 * @brief 遍历链表
 */
#define list_for_each(pos, head) for (pos = (head)->next; pos != (head); pos = pos->next)

/**
 * @brief 反向遍历链表
 */
#define list_for_each_prev(pos, head) for (pos = (head)->prev; pos != (head); pos = pos->prev)

/**
 * @brief 安全遍历链表（支持删除）
 */
#define list_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; pos != (head); pos = n, n = pos->next)

/**
 * @brief 遍历链表节点对应的结构体
 */
#define list_for_each_entry(pos, head, member)                                         \
    for (pos = list_entry((head)->next, typeof(*pos), member); &pos->member != (head); \
         pos = list_entry(pos->member.next, typeof(*pos), member))

/**
 * @brief 反向遍历链表节点对应的结构体
 */
#define list_for_each_entry_reverse(pos, head, member)                                 \
    for (pos = list_entry((head)->prev, typeof(*pos), member); &pos->member != (head); \
         pos = list_entry(pos->member.prev, typeof(*pos), member))

/**
 * @brief 安全遍历链表节点对应的结构体
 */
#define list_for_each_entry_safe(pos, n, head, member)          \
    for (pos = list_entry((head)->next, typeof(*pos), member),  \
        n = list_entry(pos->member.next, typeof(*pos), member); \
         &pos->member != (head); pos = n, n = list_entry(n->member.next, typeof(*pos), member))

#ifdef __cplusplus
}
#endif

#endif /* LIST_H */
