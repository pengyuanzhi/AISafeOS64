/**
 * @file rbtree.h
 * @brief AISafe64 RTOS - 红黑树
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 红黑树实现（Linux kernel风格）
 *          - O(log n)查找、插入、删除
 *          - 用于EDF和CFS调度器
 *
 * @note MISRA-C:2012合规
 */

#ifndef RBTREE_H
#define RBTREE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 红黑树节点颜色
 */
#define RB_RED     0
#define RB_BLACK   1

/**
 * @brief 红黑树节点
 */
struct rb_node {
    uint64_t __rb_parent_color;
    struct rb_node *rb_right;
    struct rb_node *rb_left;
};

/**
 * @brief 红黑树根
 */
struct rb_root {
    struct rb_node *rb_node;
};

/**
 * @brief 初始化红黑树节点
 */
#define RB_ROOT  (struct rb_root) { NULL, }

/**
 * @brief 初始化红黑树节点指针
 */
#define RB_EMPTY_ROOT  (struct rb_root) { NULL, }

/**
 * @brief 初始化节点
 */
static inline void RB_CLEAR_NODE(struct rb_node *node) {
    node->__rb_parent_color = 0U;
    node->rb_left = NULL;
    node->rb_right = NULL;
}

/**
 * @brief 获取父节点
 */
static inline struct rb_node *rb_parent(const struct rb_node *node) {
    return (struct rb_node *)(node->__rb_parent_color & ~3UL);
}

/**
 * @brief 设置父节点
 */
static inline void rb_set_parent(struct rb_node *node, struct rb_node *parent) {
    node->__rb_parent_color = (node->__rb_parent_color & 3UL) |
                             (uint64_t)parent;
}

/**
 * @brief 获取节点颜色
 */
static inline int rb_color(const struct rb_node *node) {
    return (int)(node->__rb_parent_color & 1UL);
}

/**
 * @brief 设置节点颜色
 */
static inline void rb_set_color(struct rb_node *node, int color) {
    node->__rb_parent_color = (node->__rb_parent_color & ~1UL) | color;
}

/**
 * @brief 节点是否为红色
 */
static inline bool rb_is_red(const struct rb_node *node) {
    return rb_color(node) == RB_RED;
}

/**
 * @brief 节点是否为黑色
 */
static inline bool rb_is_black(const struct rb_node *node) {
    return rb_color(node) == RB_BLACK;
}

/**
 * @brief 设置节点为红色
 */
static inline void rb_set_red(struct rb_node *node) {
    node->__rb_parent_color &= ~1UL;
}

/**
 * @brief 设置节点为黑色
 */
static inline void rb_set_black(struct rb_node *node) {
    node->__rb_parent_color |= 1UL;
}

/**
 * @brief 检查节点是否为空
 */
static inline bool RB_EMPTY_NODE(const struct rb_node *node) {
    return node->__rb_parent_color == 0UL;
}

/**
 * @brief 检查树是否为空
 */
static inline bool RB_EMPTY_ROOT(const struct rb_root *root) {
    return root->rb_node == NULL;
}

/**
 * @brief 获取包含该节点的结构体指针
 */
#define rb_entry(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/**
 * @brief 红黑树查找
 * @param root 红黑树根
 * @param node 要查找的节点
 * @param compare 比较函数
 * @return 找到的节点，NULL表示未找到
 */
typedef int (*rb_compare_func)(const struct rb_node *a,
                                const struct rb_node *b);

struct rb_node *rb_find(const struct rb_root *root,
                        const struct rb_node *node,
                        rb_compare_func compare);

/**
 * @brief 红黑树插入
 * @param root 红黑树根
 * @param node 要插入的节点
 * @param compare 比较函数
 */
void rb_insert(struct rb_root *root,
               struct rb_node *node,
               rb_compare_func compare);

/**
 * @brief 红黑树删除
 * @param root 红黑树根
 * @param node 要删除的节点
 */
void rb_erase(struct rb_root *root, struct rb_node *node);

/**
 * @brief 获取第一个节点（最左节点）
 * @param root 红黑树根
 * @return 第一个节点，NULL表示树为空
 */
struct rb_node *rb_first(const struct rb_root *root);

/**
 * @brief 获取最后一个节点（最右节点）
 * @param root 红黑树根
 * @return 最后一个节点，NULL表示树为空
 */
struct rb_node *rb_last(const struct rb_root *root);

/**
 * @brief 获取下一个节点
 * @param node 当前节点
 * @return 下一个节点，NULL表示已是最后一个
 */
struct rb_node *rb_next(const struct rb_node *node);

/**
 * @brief 获取前一个节点
 * @param node 当前节点
 * @return 前一个节点，NULL表示已是第一个
 */
struct rb_node *rb_prev(const struct rb_node *node);

/**
 * @brief 替换节点
 * @param victim 被替换的节点
 * @param new_node 新节点
 */
void rb_replace_node(struct rb_node *victim,
                     struct rb_node *new_node,
                     struct rb_root *root);

/**
 * @brief 遍历红黑树
 */
#define rb_for_each(pos, root) \
    for (pos = rb_first(root); pos != NULL; pos = rb_next(pos))

/**
 * @brief 遍历红黑树节点对应的结构体
 */
#define rb_for_each_entry(pos, root, member) \
    for (pos = rb_entry(rb_first(root), typeof(*pos), member); \
         &pos->member != NULL; \
         pos = rb_entry(rb_next(&pos->member), typeof(*pos), member))

/**
 * @brief 后序遍历释放红黑树
 */
void rb_free_subtree(struct rb_node *node,
                     void (*free_func)(struct rb_node *node));

#ifdef __cplusplus
}
#endif

#endif /* RBTREE_H */
