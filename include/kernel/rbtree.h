/**
 * @file    rbtree.h
 * @brief   红黑树（Red-Black Tree）数据结构
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 *
 * @details 本文件定义了红黑树数据结构：
 *          - 红黑树节点结构
 *          - 红黑树查找/插入/删除
 *          - 红黑树遍历
 *
 * @note MISRA-C:2012 合规
 * @note 对应优化计划：阶段 1.4 - VMA 树结构优化
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_RBTREE_H
#define KERNEL_RBTREE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* ========================================================================
 * 红黑树常量
 * ======================================================================== */

/** @brief 红黑树颜色 */
typedef enum
{
    RB_COLOR_RED = 0U,    /**< @brief 红色节点 */
    RB_COLOR_BLACK         /**< @brief 黑色节点 */
} rb_color_t;

/* ========================================================================
 * 红黑树节点结构
 * ======================================================================== */

/**
 * @brief 红黑树节点
 *
 * @details 红黑树节点包含颜色、父节点、左子节点和右子节点。
 *          实际数据通过 container_of 宏从节点获取。
 */
typedef struct rb_node
{
    struct rb_node *parent;    /**< @brief 父节点 */
    struct rb_node *left;      /**< @brief 左子节点 */
    struct rb_node *right;     /**< @brief 右子节点 */
    rb_color_t      color;     /**< @brief 节点颜色（红/黑） */
} rb_node_t;

/* ========================================================================
 * 红黑树根节点
 * ======================================================================== */

/**
 * @brief 红黑树根节点
 *
 * @details 红黑树根节点，NULL 表示空树。
 */
typedef struct rb_root
{
    struct rb_node *node;      /**< @brief 根节点指针 */
} rb_root_t;

/* ========================================================================
 * 红黑树宏定义
 * ======================================================================== */

/** @brief 初始化红黑树根节点为空 */
#define RB_ROOT    ((rb_root_t){ .node = NULL })

/** @brief 从红黑树节点获取数据结构指针 */
#define rb_entry(ptr, type, member) \
    ((type *)((char *)(ptr) - (uint32_t)offsetof(type, member)))

/** @brief 初始化红黑树节点 */
#define RB_NODE_INIT(ptr) \
    do { \
        (ptr)->parent = NULL; \
        (ptr)->left = NULL; \
        (ptr)->right = NULL; \
        (ptr)->color = RB_COLOR_RED; \
    } while (0)

/** @brief 判断节点是否为空 */
#define rb_empty_node(node) \
    ((node) == NULL)

/** @brief 判断红黑树是否为空 */
#define rb_empty_root(root) \
    ((root).node == NULL)

/* ========================================================================
 * 红黑树操作函数声明
 * ======================================================================== */

/**
 * @brief 红黑树左旋
 *
 * @param root    红黑树根指针
 * @param node    要旋转的节点
 */
static inline void rb_left_rotate(rb_root_t *root, rb_node_t *node);

/**
 * @brief 红黑树右旋
 *
 * @param root    红黑树根指针
 * @param node    要旋转的节点
 */
static inline void rb_right_rotate(rb_root_t *root, rb_node_t *node);

/**
 * @brief 红黑树插入后修复（重新平衡）
 *
 * @param root    红黑树根指针
 * @param node    新插入的节点
 */
static void rb_insert_fixup(rb_root_t *root, rb_node_t *node);

/**
 * @brief 红黑树删除后修复（重新平衡）
 *
 * @param root    红黑树根指针
 * @param node    要修复的节点
 */
static void rb_delete_fixup(rb_root_t *root, rb_node_t *node);

/**
 * @brief 红黑树插入节点
 *
 * @details 将节点插入到红黑树中，自动维护红黑树性质。
 *
 * @param root    红黑树根指针
 * @param node    要插入的节点
 * @param less    比较函数，返回 true 如果 node < node_in_tree
 */
void rb_insert(rb_root_t *root, rb_node_t *node,
               bool (*less)(const rb_node_t *node_a,
                           const rb_node_t *node_b));

/**
 * @brief 红黑树删除节点
 *
 * @details 从红黑树中删除节点，自动维护红黑树性质。
 *
 * @param root    红黑树根指针
 * @param node    要删除的节点
 */
void rb_delete(rb_root_t *root, rb_node_t *node);

/**
 * @brief 红黑树查找节点
 *
 * @details 在红黑树中查找节点。
 *
 * @param root    红黑树根指针
 * @param key     查找的键
 * @param compare 比较函数，返回 <0 如果 key < node，0 如果 key == node，>0 如果 key > node
 *
 * @return 找到的节点指针，未找到返回 NULL
 */
rb_node_t *rb_search(rb_root_t *root,
                   const void *key,
                   int32_t (*compare)(const void *key, const rb_node_t *node));

/**
 * @brief 红黑树查找最小节点
 *
 * @param root    红黑树根指针
 *
 * @return 最小的节点指针，树为空返回 NULL
 */
rb_node_t *rb_first(rb_root_t *root);

/**
 * @brief 红黑树查找最大节点
 *
 * @param root    红黑树根指针
 *
 * @return 最大的节点指针，树为空返回 NULL
 */
rb_node_t *rb_last(rb_root_t *root);

/**
 * @brief 红黑树查找下一个节点（中序遍历）
 *
 * @param node    当前节点
 *
 * @return 下一个节点指针，无下一个返回 NULL
 */
rb_node_t *rb_next(rb_node_t *node);

/**
 * @brief 红黑树查找上一个节点（中序遍历）
 *
 * @param node    当前节点
 *
 * @return 上一个节点指针，无上一个返回 NULL
 */
rb_node_t *rb_prev(rb_node_t *node);

/* ========================================================================
 * 红黑树旋转函数（内联）
 * ======================================================================== */

/**
 * @brief 红黑树左旋
 */
static inline void rb_left_rotate(rb_root_t *root, rb_node_t *node)
{
    rb_node_t *right = node->right;

    if (right == NULL)
    {
        return;
    }

    node->right = right->left;

    if (right->left != NULL)
    {
        right->left->parent = node;
    }

    right->parent = node->parent;

    if (node->parent == NULL)
    {
        root->node = right;
    }
    else if (node == node->parent->left)
    {
        node->parent->left = right;
    }
    else
    {
        node->parent->right = right;
    }

    right->left = node;
    node->parent = right;
}

/**
 * @brief 红黑树右旋
 */
static inline void rb_right_rotate(rb_root_t *root, rb_node_t *node)
{
    rb_node_t *left = node->left;

    if (left == NULL)
    {
        return;
    }

    node->left = left->right;

    if (left->right != NULL)
    {
        left->right->parent = node;
    }

    left->parent = node->parent;

    if (node->parent == NULL)
    {
        root->node = left;
    }
    else if (node == node->parent->right)
    {
        node->parent->right = left;
    }
    else
    {
        node->parent->left = left;
    }

    left->right = node;
    node->parent = left;
}

#endif /* KERNEL_RBTREE_H */
