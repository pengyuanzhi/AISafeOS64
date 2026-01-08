/**
 * @file rbtree.c
 * @brief AISafe64 RTOS - 红黑树实现
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

#include "rbtree.h"
#include <stddef.h>

/**
 * @brief 红黑树左旋转
 */
static void rb_rotate_left(struct rb_node *node, struct rb_root *root)
{
    struct rb_node *right = node->rb_right;
    struct rb_node *parent = rb_parent(node);

    if (right == NULL)
    {
        return;
    }

    node->rb_right = right->rb_left;
    if (right->rb_left != NULL)
    {
        rb_set_parent(right->rb_left, node);
    }

    right->rb_left = node;
    rb_set_parent(right, parent);

    if (parent != NULL)
    {
        if (node == parent->rb_left)
        {
            parent->rb_left = right;
        }
        else
        {
            parent->rb_right = right;
        }
    }
    else
    {
        root->rb_node = right;
    }
    rb_set_parent(node, right);
}

/**
 * @brief 红黑树右旋转
 */
static void rb_rotate_right(struct rb_node *node, struct rb_root *root)
{
    struct rb_node *left = node->rb_left;
    struct rb_node *parent = rb_parent(node);

    if (left == NULL)
    {
        return;
    }

    node->rb_left = left->rb_right;
    if (left->rb_right != NULL)
    {
        rb_set_parent(left->rb_right, node);
    }

    left->rb_right = node;
    rb_set_parent(left, parent);

    if (parent != NULL)
    {
        if (node == parent->rb_right)
        {
            parent->rb_right = left;
        }
        else
        {
            parent->rb_left = left;
        }
    }
    else
    {
        root->rb_node = left;
    }
    rb_set_parent(node, left);
}

/**
 * @brief 红黑树插入修复
 */
static void rb_insert_fixup(struct rb_node *node, struct rb_root *root)
{
    struct rb_node *parent, *gparent;

    while ((parent = rb_parent(node)) != NULL && rb_is_red(parent))
    {
        gparent = rb_parent(parent);

        if (parent == gparent->rb_left)
        {
            struct rb_node *uncle = gparent->rb_right;

            if (uncle != NULL && rb_is_red(uncle))
            {
                rb_set_black(uncle);
                rb_set_black(parent);
                rb_set_red(gparent);
                node = gparent;
                continue;
            }

            if (node == parent->rb_right)
            {
                parent = node;
                rb_rotate_left(parent, root);
                node = parent->rb_left;
            }

            rb_set_black(parent);
            rb_set_red(gparent);
            rb_rotate_right(gparent, root);
        }
        else
        {
            struct rb_node *uncle = gparent->rb_left;

            if (uncle != NULL && rb_is_red(uncle))
            {
                rb_set_black(uncle);
                rb_set_black(parent);
                rb_set_red(gparent);
                node = gparent;
                continue;
            }

            if (node == parent->rb_left)
            {
                parent = node;
                rb_rotate_right(parent, root);
                node = parent->rb_right;
            }

            rb_set_black(parent);
            rb_set_red(gparent);
            rb_rotate_left(gparent, root);
        }
    }

    rb_set_black(root->rb_node);
}

/**
 * @brief 红黑树查找
 */
struct rb_node *rb_find(const struct rb_root *root, const struct rb_node *node,
                        rb_compare_func compare)
{
    struct rb_node *current = root->rb_node;

    while (current != NULL)
    {
        int result = compare(node, current);

        if (result < 0)
        {
            current = current->rb_left;
        }
        else if (result > 0)
        {
            current = current->rb_right;
        }
        else
        {
            return current;
        }
    }

    return NULL;
}

/**
 * @brief 红黑树插入
 */
void rb_insert(struct rb_root *root, struct rb_node *node, rb_compare_func compare)
{
    struct rb_node *parent = NULL;
    struct rb_node **link = &root->rb_node;

    /* 查找插入位置 */
    while (*link != NULL)
    {
        parent = *link;
        int result = compare(node, parent);

        if (result < 0)
        {
            link = &(*link)->rb_left;
        }
        else if (result > 0)
        {
            link = &(*link)->rb_right;
        }
        else
        {
            /* 节点已存在，不插入 */
            return;
        }
    }

    /* 插入节点 */
    rb_set_parent(node, parent);
    rb_set_color(node, RB_RED);
    node->rb_left = NULL;
    node->rb_right = NULL;
    *link = node;

    /* 修复红黑树性质 */
    rb_insert_fixup(node, root);
}

/**
 * @brief 红黑树删除修复
 */
static void rb_erase_fixup(struct rb_node *node, struct rb_node *parent, struct rb_root *root)
{
    struct rb_node *other;

    while ((node == NULL || rb_is_black(node)) && node != root->rb_node)
    {
        if (parent->rb_left == node)
        {
            other = parent->rb_right;
            if (rb_is_red(other))
            {
                rb_set_black(other);
                rb_set_red(parent);
                rb_rotate_left(parent, root);
                other = parent->rb_right;
            }
            if ((other->rb_left == NULL || rb_is_black(other->rb_left)) &&
                (other->rb_right == NULL || rb_is_black(other->rb_right)))
            {
                rb_set_red(other);
                node = parent;
                parent = rb_parent(node);
            }
            else
            {
                if (other->rb_right == NULL || rb_is_black(other->rb_right))
                {
                    rb_set_black(other->rb_left);
                    rb_set_red(other);
                    rb_rotate_right(other, root);
                    other = parent->rb_right;
                }
                rb_set_color(other, rb_color(parent));
                rb_set_black(parent);
                rb_set_black(other->rb_right);
                rb_rotate_left(parent, root);
                node = root->rb_node;
                break;
            }
        }
        else
        {
            other = parent->rb_left;
            if (rb_is_red(other))
            {
                rb_set_black(other);
                rb_set_red(parent);
                rb_rotate_right(parent, root);
                other = parent->rb_left;
            }
            if ((other->rb_left == NULL || rb_is_black(other->rb_left)) &&
                (other->rb_right == NULL || rb_is_black(other->rb_right)))
            {
                rb_set_red(other);
                node = parent;
                parent = rb_parent(node);
            }
            else
            {
                if (other->rb_left == NULL || rb_is_black(other->rb_left))
                {
                    rb_set_black(other->rb_right);
                    rb_set_red(other);
                    rb_rotate_left(other, root);
                    other = parent->rb_left;
                }
                rb_set_color(other, rb_color(parent));
                rb_set_black(parent);
                rb_set_black(other->rb_left);
                rb_rotate_right(parent, root);
                node = root->rb_node;
                break;
            }
        }
    }

    if (node != NULL)
    {
        rb_set_black(node);
    }
}

/**
 * @brief 红黑树删除
 */
void rb_erase(struct rb_root *root, struct rb_node *node)
{
    struct rb_node *child, *parent;
    int color;

    if (node->rb_right == NULL)
    {
        child = node->rb_left;
    }
    else if (node->rb_left == NULL)
    {
        child = node->rb_right;
    }
    else
    {
        struct rb_node *old = node;
        struct rb_node *left;

        node = node->rb_right;
        while ((left = node->rb_left) != NULL)
        {
            node = left;
        }

        child = node->rb_right;
        parent = rb_parent(node);
        color = rb_color(node);

        if (child != NULL)
        {
            rb_set_parent(child, parent);
        }
        if (parent == old)
        {
            parent->rb_right = child;
            parent = node;
        }
        else
        {
            parent->rb_left = child;
        }

        node->__rb_parent_color = old->__rb_parent_color;
        node->rb_right = old->rb_right;
        node->rb_left = old->rb_left;

        if (rb_parent(old) != NULL)
        {
            if (old == rb_parent(old)->rb_left)
            {
                rb_parent(old)->rb_left = node;
            }
            else
            {
                rb_parent(old)->rb_right = node;
            }
        }
        else
        {
            root->rb_node = node;
        }

        rb_set_parent(old->rb_left, node);
        if (old->rb_right != NULL)
        {
            rb_set_parent(old->rb_right, node);
        }

        goto color;
    }

    parent = rb_parent(node);
    color = rb_color(node);

    if (child != NULL)
    {
        rb_set_parent(child, parent);
    }

    if (parent != NULL)
    {
        if (parent->rb_left == node)
        {
            parent->rb_left = child;
        }
        else
        {
            parent->rb_right = child;
        }
    }
    else
    {
        root->rb_node = child;
    }

color:
    if (color == RB_BLACK)
    {
        rb_erase_fixup(child, parent, root);
    }
}

/**
 * @brief 获取第一个节点（最左节点）
 */
struct rb_node *rb_first(const struct rb_root *root)
{
    struct rb_node *node = root->rb_node;

    if (node == NULL)
    {
        return NULL;
    }

    while (node->rb_left != NULL)
    {
        node = node->rb_left;
    }

    return node;
}

/**
 * @brief 获取最后一个节点（最右节点）
 */
struct rb_node *rb_last(const struct rb_root *root)
{
    struct rb_node *node = root->rb_node;

    if (node == NULL)
    {
        return NULL;
    }

    while (node->rb_right != NULL)
    {
        node = node->rb_right;
    }

    return node;
}

/**
 * @brief 获取下一个节点
 */
struct rb_node *rb_next(const struct rb_node *node)
{
    struct rb_node *parent;

    if (node->rb_right != NULL)
    {
        node = node->rb_right;
        while (node->rb_left != NULL)
        {
            node = node->rb_left;
        }
        return (struct rb_node *)node;
    }

    while ((parent = rb_parent(node)) != NULL && node == parent->rb_right)
    {
        node = parent;
    }

    return parent;
}

/**
 * @brief 获取前一个节点
 */
struct rb_node *rb_prev(const struct rb_node *node)
{
    struct rb_node *parent;

    if (node->rb_left != NULL)
    {
        node = node->rb_left;
        while (node->rb_right != NULL)
        {
            node = node->rb_right;
        }
        return (struct rb_node *)node;
    }

    while ((parent = rb_parent(node)) != NULL && node == parent->rb_left)
    {
        node = parent;
    }

    return parent;
}

/**
 * @brief 替换节点
 */
void rb_replace_node(struct rb_node *victim, struct rb_node *new_node, struct rb_root *root)
{
    struct rb_node *parent = rb_parent(victim);

    if (parent != NULL)
    {
        if (victim == parent->rb_left)
        {
            parent->rb_left = new_node;
        }
        else
        {
            parent->rb_right = new_node;
        }
    }
    else
    {
        root->rb_node = new_node;
    }

    if (victim->rb_left != NULL)
    {
        rb_set_parent(victim->rb_left, new_node);
    }

    if (victim->rb_right != NULL)
    {
        rb_set_parent(victim->rb_right, new_node);
    }

    *new_node = *victim;
}

/**
 * @brief 后序遍历释放红黑树
 */
void rb_free_subtree(struct rb_node *node, void (*free_func)(struct rb_node *node))
{
    if (node == NULL)
    {
        return;
    }

    rb_free_subtree(node->rb_left, free_func);
    rb_free_subtree(node->rb_right, free_func);

    if (free_func != NULL)
    {
        free_func(node);
    }
}
