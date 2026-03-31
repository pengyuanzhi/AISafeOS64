/**
 * @file    list.h
 * @brief   内核侵入式双向链表
 * @author  AISafe64 Team
 * @date    2026-03-31
 * @version 2.0
 *
 * @details 本文件实现了侵入式双向循环链表数据结构，参考 Linux 内核
 *          list_head 设计。侵入式链表将链表节点嵌入到宿主结构体中，
 *          避免了额外的内存分配，适用于内核中各种对象的链式管理。
 *
 *          特性：
 *          - O(1) 的插入和删除操作
 *          - 无需动态内存分配
 *          - 类型安全的 container_of 宏
 *          - 支持正向遍历和安全删除遍历
 *
 * @note    MISRA-C:2012 合规
 * @warning 遍历过程中修改链表必须使用 _safe 系列宏
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_LIST_H
#define KERNEL_LIST_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief 侵入式双向链表头/节点结构
 *
 * @details 链表节点仅包含前驱和后继指针，需要嵌入到宿主结构体中使用。
 *          空链表的 head->next 和 head->prev 都指向 head 自身。
 */
struct list_head
{
    struct list_head *next; /**< @brief 指向下一个节点 */
    struct list_head *prev; /**< @brief 指向前一个节点 */
};

/**
 * @def LIST_HEAD_INIT
 * @brief 链表静态初始化宏
 *
 * @details 将链表头的前驱和后继指针都指向自身，形成空循环链表。
 *
 * @param name 链表头变量名
 */
#define LIST_HEAD_INIT(name) { &(name), &(name) }

/**
 * @def LIST_HEAD
 * @brief 定义并静态初始化一个链表头
 *
 * @details 声明一个 struct list_head 变量并初始化为空链表。
 *
 * @param name 链表头变量名
 *
 * @par 示例
 * @code
 * LIST_HEAD(task_ready_queue);
 * @endcode
 */
#define LIST_HEAD(name) \
    struct list_head name = LIST_HEAD_INIT(name)

/**
 * @brief 运行时初始化链表头
 *
 * @details 将链表头的前驱和后继指针都指向自身，形成空循环链表。
 *
 * @param[in] ptr 指向待初始化的链表头的指针
 */
static inline void INIT_LIST_HEAD(struct list_head *ptr)
{
    if (ptr != NULL)
    {
        ptr->next = ptr;
        ptr->prev = ptr;
    }
}

/**
 * @brief 内部函数：在两个已知节点之间插入新节点
 *
 * @details 将 new 节点插入到 prev 和 next 之间。
 *          此函数仅供内部使用，外部应使用 list_add 或 list_add_tail。
 *
 * @param[in] new  待插入的新节点
 * @param[in] prev 插入位置的前驱节点
 * @param[in] next 插入位置的后继节点
 */
static inline void __list_add(struct list_head *new_node,
                              struct list_head *prev,
                              struct list_head *next)
{
    if ((new_node == NULL) || (prev == NULL) || (next == NULL))
    {
        return;
    }

    next->prev = new_node;
    new_node->next = next;
    new_node->prev = prev;
    prev->next = new_node;
}

/**
 * @brief 在链表头部插入新节点
 *
 * @details 将 new_node 插入到 head 之后，即成为链表的第一个节点。
 *          适用于栈（LIFO）语义的场景。
 *
 * @param[in] new_node 待插入的新节点
 * @param[in] head     链表头指针
 *
 * @par 示例
 * @code
 * list_add(&task->list, &ready_queue);
 * @endcode
 */
static inline void list_add(struct list_head *new_node,
                            struct list_head *head)
{
    if ((new_node == NULL) || (head == NULL))
    {
        return;
    }

    __list_add(new_node, head, head->next);
}

/**
 * @brief 在链表尾部插入新节点
 *
 * @details 将 new_node 插入到 head 之前（即链表尾部）。
 *          适用于队列（FIFO）语义的场景。
 *
 * @param[in] new_node 待插入的新节点
 * @param[in] head     链表头指针
 *
 * @par 示例
 * @code
 * list_add_tail(&task->list, &wait_queue);
 * @endcode
 */
static inline void list_add_tail(struct list_head *new_node,
                                 struct list_head *head)
{
    if ((new_node == NULL) || (head == NULL))
    {
        return;
    }

    __list_add(new_node, head->prev, head);
}

/**
 * @brief 内部函数：从链表中删除指定节点
 *
 * @details 将 prev 和 next 直接相连，解除 entry 的链接关系。
 *          此函数仅供内部使用，外部应使用 list_del 或 list_del_init。
 *
 * @param[in] entry 待删除的节点的后继
 * @param[in] prev  待删除节点的前驱
 */
static inline void __list_del(struct list_head *prev, struct list_head *next)
{
    if ((prev == NULL) || (next == NULL))
    {
        return;
    }

    next->prev = prev;
    prev->next = next;
}

/**
 * @brief 从链表中删除指定节点
 *
 * @details 将 entry 从链表中移除。删除后 entry 的 next 和 prev 指针
 *          均被置为 NULL，以防止对已删除节点的误用。
 *
 * @param[in] entry 待删除的节点
 *
 * @warning 删除后 entry 的指针不可再用于链表遍历
 * @note    删除后应调用 INIT_LIST_HEAD(entry) 或 list_del_init(entry)
 *          以便重新使用该节点
 */
static inline void list_del(struct list_head *entry)
{
    if (entry == NULL)
    {
        return;
    }

    __list_del(entry->prev, entry->next);
    entry->next = NULL;
    entry->prev = NULL;
}

/**
 * @brief 从链表中删除指定节点并重新初始化
 *
 * @details 将 entry 从链表中移除，然后将其重新初始化为空链表。
 *          适用于需要重复使用该节点的场景。
 *
 * @param[in] entry 待删除并重新初始化的节点
 */
static inline void list_del_init(struct list_head *entry)
{
    if (entry == NULL)
    {
        return;
    }

    __list_del(entry->prev, entry->next);
    INIT_LIST_HEAD(entry);
}

/**
 * @brief 判断链表是否为空
 *
 * @details 检查链表头的前驱是否指向自身。空链表的头节点的 next 指针
 *          指向 head 自身。
 *
 * @param[in] head 链表头指针
 *
 * @return 非0表示链表为空，0表示链表非空
 */
static inline int list_empty(const struct list_head *head)
{
    if (head == NULL)
    {
        return 1;
    }

    return (head->next == head) ? 1 : 0;
}

/**
 * @brief 判断节点是否是链表的第一个节点
 *
 * @details 检查 entry 是否紧接在 head 之后。
 *
 * @param[in] entry 待检查的节点
 * @param[in] head  链表头指针
 *
 * @return 非0表示 entry 是第一个节点，0表示不是
 */
static inline int list_is_first(const struct list_head *entry,
                                const struct list_head *head)
{
    if ((entry == NULL) || (head == NULL))
    {
        return 0;
    }

    return (head->next == entry) ? 1 : 0;
}

/**
 * @brief 判断节点是否是链表的最后一个节点
 *
 * @details 检查 entry 是否紧接在 head 之前（即链表尾部）。
 *
 * @param[in] entry 待检查的节点
 * @param[in] head  链表头指针
 *
 * @return 非0表示 entry 是最后一个节点，0表示不是
 */
static inline int list_is_last(const struct list_head *entry,
                               const struct list_head *head)
{
    if ((entry == NULL) || (head == NULL))
    {
        return 0;
    }

    return (head->prev == entry) ? 1 : 0;
}

/**
 * @brief 将一个链表合并到另一个链表的头部
 *
 * @details 将 list 链表中的所有节点插入到 head 链表的头部。
 *          操作完成后 list 链表将被重新初始化为空链表。
 *
 * @param[in] list 待合并的源链表（操作后变为空链表）
 * @param[in] head 目标链表头
 *
 * @note 如果 list 为空，则不做任何操作
 */
static inline void list_splice(struct list_head *list,
                               struct list_head *head)
{
    if ((list == NULL) || (head == NULL))
    {
        return;
    }

    if (list_empty(list) != 0)
    {
        return;
    }

    struct list_head *first = list->next;
    struct list_head *last = list->prev;

    first->prev = head;
    head->next->prev = last;
    last->next = head->next;
    head->next = first;
    INIT_LIST_HEAD(list);
}

/**
 * @def list_entry
 * @brief 通过链表节点指针获取包含该节点的宿主结构体指针
 *
 * @details 利用 container_of 原理，根据链表节点在宿主结构体中的偏移量，
 *          反算出宿主结构体的首地址。
 *
 * @param ptr     链表节点指针
 * @param type    宿主结构体类型
 * @param member  链表节点在宿主结构体中的成员名
 *
 * @return 指向宿主结构体的指针
 *
 * @par 示例
 * @code
 * struct list_head *pos = &task->list;
 * TCB_t *task = list_entry(pos, TCB_t, list);
 * @endcode
 */
#ifndef container_of
/**
 * @def container_of
 * @brief 通过成员指针获取包含结构体的指针
 *
 * @param ptr     成员指针
 * @param type    包含该成员的结构体类型
 * @param member  成员在结构体中的名称
 */
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

#define list_entry(ptr, type, member) \
    container_of(ptr, type, member)

/**
 * @def list_first_entry
 * @brief 获取链表中的第一个条目
 *
 * @details 返回链表中第一个节点对应的宿主结构体指针。
 *
 * @param head    链表头指针
 * @param type    宿主结构体类型
 * @param member  链表节点在宿主结构体中的成员名
 *
 * @return 指向第一个条目的指针
 *
 * @warning 调用前必须确保链表非空
 */
#define list_first_entry(head, type, member) \
    list_entry((head)->next, type, member)

/**
 * @def list_last_entry
 * @brief 获取链表中的最后一个条目
 *
 * @details 返回链表中最后一个节点对应的宿主结构体指针。
 *
 * @param head    链表头指针
 * @param type    宿主结构体类型
 * @param member  链表节点在宿主结构体中的成员名
 *
 * @return 指向最后一个条目的指针
 *
 * @warning 调用前必须确保链表非空
 */
#define list_last_entry(head, type, member) \
    list_entry((head)->prev, type, member)

/**
 * @def list_for_each
 * @brief 正向遍历链表
 *
 * @details 从 head->next 开始，依次遍历链表中的每个节点，直到回到 head。
 *
 * @param pos  循环变量，指向当前节点的 struct list_head 指针
 * @param head 链表头指针
 *
 * @warning 遍历过程中不可删除当前节点，否则会导致未定义行为。
 *          如需删除请使用 list_for_each_safe。
 *
 * @par 示例
 * @code
 * struct list_head *pos;
 * list_for_each(pos, &task_queue)
 * {
 *     TCB_t *task = list_entry(pos, TCB_t, list);
 *     process_task(task);
 * }
 * @endcode
 */
#define list_for_each(pos, head) \
    for ((pos) = (head)->next; (pos) != (head); (pos) = (pos)->next)

/**
 * @def list_for_each_safe
 * @brief 安全的正向遍历链表（支持删除当前节点）
 *
 * @details 使用临时变量 n 预先保存下一个节点的指针，
 *          因此即使删除当前节点 pos 也不会影响遍历。
 *
 * @param pos  循环变量，指向当前节点的 struct list_head 指针
 * @param n    临时变量，用于保存 pos 的下一个节点
 * @param head 链表头指针
 *
 * @par 示例
 * @code
 * struct list_head *pos, *n;
 * list_for_each_safe(pos, n, &task_queue)
 * {
 *     TCB_t *task = list_entry(pos, TCB_t, list);
 *     list_del(pos);
 *     free_task(task);
 * }
 * @endcode
 */
#define list_for_each_safe(pos, n, head) \
    for ((pos) = (head)->next, (n) = (pos)->next; \
         (pos) != (head); \
         (pos) = (n), (n) = (pos)->next)

/**
 * @def list_for_each_entry
 * @brief 遍历链表中的每个宿主结构体条目
 *
 * @details 结合 list_entry 宏，直接遍历宿主结构体而非链表节点。
 *
 * @param pos    循环变量，指向当前宿主结构体的指针
 * @param head   链表头指针
 * @param member 链表节点在宿主结构体中的成员名
 *
 * @warning 遍历过程中不可删除当前节点，请使用 list_for_each_entry_safe。
 *
 * @par 示例
 * @code
 * TCB_t *task;
 * list_for_each_entry(task, &task_queue, list)
 * {
 *     process_task(task);
 * }
 * @endcode
 */
#define list_for_each_entry(pos, head, member) \
    for ((pos) = list_entry((head)->next, typeof(*(pos)), member); \
         &(pos)->member != (head); \
         (pos) = list_entry((pos)->member.next, typeof(*(pos)), member))

/**
 * @def list_for_each_entry_safe
 * @brief 安全遍历链表中的每个宿主结构体条目（支持删除当前节点）
 *
 * @details 使用临时变量 n 预先保存下一个条目，因此即使删除当前条目
 *          也不会影响遍历。
 *
 * @param pos    循环变量，指向当前宿主结构体的指针
 * @param n      临时变量，用于保存下一个宿主结构体条目
 * @param head   链表头指针
 * @param member 链表节点在宿主结构体中的成员名
 *
 * @par 示例
 * @code
 * TCB_t *task, *tmp;
 * list_for_each_entry_safe(task, tmp, &task_queue, list)
 * {
 *     list_del(&task->list);
 *     free_task(task);
 * }
 * @endcode
 */
#define list_for_each_entry_safe(pos, n, head, member) \
    for ((pos) = list_entry((head)->next, typeof(*(pos)), member), \
         (n) = list_entry((pos)->member.next, typeof(*(pos)), member); \
         &(pos)->member != (head); \
         (pos) = (n), \
         (n) = list_entry((n)->member.next, typeof(*(n)), member))

/**
 * @brief 计算链表中的节点数量
 *
 * @details 遍历整个链表并计数节点数量。
 *          时间复杂度为 O(n)，在性能敏感路径中慎用。
 *
 * @param[in] head 链表头指针
 *
 * @return 链表中的节点数量，空链表返回 0
 *
 * @note 此函数需要遍历整个链表，请勿在热路径中频繁调用
 */
static inline uint32_t list_count_nodes(const struct list_head *head)
{
    uint32_t count = 0U;
    const struct list_head *pos;

    if (head == NULL)
    {
        return 0U;
    }

    for (pos = head->next; pos != head; pos = pos->next)
    {
        count++;
    }

    return count;
}

#endif /* KERNEL_LIST_H */
