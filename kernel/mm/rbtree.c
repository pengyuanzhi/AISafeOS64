/**
 * @file    rbtree.c
 * @brief   红黑树（Red-Black Tree）数据结构实现
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 *
 * @details 实现红黑树数据结构：
 *          - 红黑树插入/删除/查找
 *          - 红黑树旋转和修复
 *          - 红黑树遍历
 *
 * @note MISRA-C:2012 合规
 * @note 对应优化计划：阶段 1.4 - VMA 树结构优化
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/rbtree.h>
#include <stddef.h>
#include <stdbool.h>

/* ========================================================================
 * 红黑树插入后修复（重新平衡）
 * ======================================================================== */

/**
 * @brief 红黑树插入后修复（重新平衡）
 *
 * @details 插入新节点后，可能违反红黑树性质，需要重新平衡。
 *          根据红黑树的插入规则，进行适当的旋转和重新着色。
 *
 * @param root    红黑树根指针
 * @param node    新插入的节点
 */
static void rb_insert_fixup(rb_root_t *root, rb_node_t *node)
{
    rb_node_t *parent;
    rb_node_t *uncle;
    rb_node_t *grandparent;

    /* 父节点是红色时需要修复 */
    while ((node->parent != NULL) && (node->parent->color == RB_COLOR_RED))
    {
        parent = node->parent;
        grandparent = parent->parent;

        if (parent == grandparent->left)
        {
            /* 父节点是左子节点 */
            uncle = grandparent->right;

            if ((uncle != NULL) && (uncle->color == RB_COLOR_RED))
            {
                /* 情况 1: 叔叔节点是红色 */
                parent->color = RB_COLOR_BLACK;
                uncle->color = RB_COLOR_BLACK;
                grandparent->color = RB_COLOR_RED;
                node = grandparent;
            }
            else
            {
                if (node == parent->right)
                {
                    /* 情况 2: 叔叔节点是黑色，节点是右子节点 */
                    node = parent;
                    rb_left_rotate(root, node);
                    parent = node->parent;
                }

                /* 情况 3: 叔叔节点是黑色，节点是左子节点 */
                parent->color = RB_COLOR_BLACK;
                grandparent->color = RB_COLOR_RED;
                rb_right_rotate(root, grandparent);
            }
        }
        else
        {
            /* 父节点是右子节点 */
            uncle = grandparent->left;

            if ((uncle != NULL) && (uncle->color == RB_COLOR_RED))
            {
                /* 情况 1: 叔叔节点是红色 */
                parent->color = RB_COLOR_BLACK;
                uncle->color = RB_COLOR_BLACK;
                grandparent->color = RB_COLOR_RED;
                node = grandparent;
            }
            else
            {
                if (node == parent->left)
                {
                    /* 情况 2: 叔叔节点是黑色，节点是左子节点 */
                    node = parent;
                    rb_right_rotate(root, node);
                    parent = node->parent;
                }

                /* 情况 3: 叔叔节点是黑色，节点是右子节点 */
                parent->color = RB_COLOR_BLACK;
                grandparent->color = RB_COLOR_RED;
                rb_left_rotate(root, grandparent);
            }
        }
    }

    /* 根节点始终是黑色 */
    if (root->node != NULL)
    {
        root->node->color = RB_COLOR_BLACK;
    }
}

/* ========================================================================
 * 红黑树删除后修复（重新平衡）
 * ======================================================================== */

/**
 * @brief 红黑树删除后修复（重新平衡）
 *
 * @details 删除节点后，可能违反红黑树性质，需要重新平衡。
 *          根据红黑树的删除规则，进行适当的旋转和重新着色。
 *
 * @param root    红黑树根指针
 * @param node    要修复的节点（可能是被删除节点的子节点）
 */
static void rb_delete_fixup(rb_root_t *root, rb_node_t *node)
{
    rb_node_t *sibling;

    while ((node != root->node) && ((node == NULL) || (node->color == RB_COLOR_BLACK)))
    {
        if (node == node->parent->left)
        {
            sibling = node->parent->right;

            if (sibling->color == RB_COLOR_RED)
            {
                /* 情况 1: 兄弟节点是红色 */
                sibling->color = RB_COLOR_BLACK;
                node->parent->color = RB_COLOR_RED;
                rb_left_rotate(root, node->parent);
                sibling = node->parent->right;
            }

            if (((sibling->left == NULL) || (sibling->left->color == RB_COLOR_BLACK)) &&
                ((sibling->right == NULL) || (sibling->right->color == RB_COLOR_BLACK)))
            {
                /* 情况 2: 兄弟节点是黑色，且其子节点都是黑色 */
                sibling->color = RB_COLOR_RED;
                node = node->parent;
            }
            else
            {
                if ((sibling->right == NULL) || (sibling->right->color == RB_COLOR_BLACK))
                {
                    /* 情况 3: 兄弟节点是黑色，左子节点是红色 */
                    if (sibling->left != NULL)
                    {
                        sibling->left->color = RB_COLOR_BLACK;
                    }
                    sibling->color = RB_COLOR_RED;
                    rb_right_rotate(root, sibling);
                    sibling = node->parent->right;
                }

                /* 情况 4: 兄弟节点是黑色，右子节点是红色 */
                sibling->color = node->parent->color;
                node->parent->color = RB_COLOR_BLACK;
                if (sibling->right != NULL)
                {
                    sibling->right->color = RB_COLOR_BLACK;
                }
                rb_left_rotate(root, node->parent);
                node = root->node;
            }
        }
        else
        {
            /* 对称情况 */
            sibling = node->parent->left;

            if (sibling->color == RB_COLOR_RED)
            {
                /* 情况 1: 兄弟节点是红色 */
                sibling->color = RB_COLOR_BLACK;
                node->parent->color = RB_COLOR_RED;
                rb_right_rotate(root, node->parent);
                sibling = node->parent->left;
            }

            if (((sibling->left == NULL) || (sibling->left->color == RB_COLOR_BLACK)) &&
                ((sibling->right == NULL) || (sibling->right->color == RB_COLOR_BLACK)))
            {
                /* 情况 2: 兄弟节点是黑色，且其子节点都是黑色 */
                sibling->color = RB_COLOR_RED;
                node = node->parent;
            }
            else
            {
                if ((sibling->left == NULL) || (sibling->left->color == RB_COLOR_BLACK))
                {
                    /* 情况 3: 兄弟节点是黑色，右子节点是红色 */
                    if (sibling->right != NULL)
                    {
                        sibling->right->color = RB_COLOR_BLACK;
                    }
                    sibling->color = RB_COLOR_RED;
                    rb_left_rotate(root, sibling);
                    sibling = node->parent->left;
                }

                /* 情况 4: 兄弟节点是黑色，左子节点是红色 */
                sibling->color = node->parent->color;
                node->parent->color = RB_COLOR_BLACK;
                if (sibling->left != NULL)
                {
                    sibling->left->color = RB_COLOR_BLACK;
                }
                rb_right_rotate(root, node->parent);
                node = root->node;
            }
        }
    }

    if (node != NULL)
    {
        node->color = RB_COLOR_BLACK;
    }
}

/* ========================================================================
 * 红黑树查找最小节点
 * ======================================================================== */

/**
 * @brief 红黑树查找最小节点
 *
 * @details 查找红黑树中的最小节点（最左边的节点）。
 *
 * @param node    红黑树节点指针
 *
 * @return 最小的节点指针，树为空返回 NULL
 */
static rb_node_t *rb_minimum(rb_node_t *node)
{
    while ((node != NULL) && (node->left != NULL))
    {
        node = node->left;
    }

    return node;
}

/* ========================================================================
 * 红黑树查找最大节点
 * ======================================================================== */

/**
 * @brief 红黑树查找最大节点
 *
 * @details 查找红黑树中的最大节点（最右边的节点）。
 *
 * @param node    红黑树节点指针
 *
 * @return 最大的节点指针，树为空返回 NULL
 */
static rb_node_t *rb_maximum(rb_node_t *node)
{
    while ((node != NULL) && (node->right != NULL))
    {
        node = node->right;
    }

    return node;
}

/* ========================================================================
 * 红黑树操作函数实现
 * ======================================================================== */

/**
 * @brief 红黑树插入节点
 *
 * @details 将节点插入到红黑树中，自动维护红黑树性质。
 *
 * @param root    红黑树根指针
 * @param node    要插入的节点
 * @param less    比较函数，返回 true 如果 node_a < node_b
 */
void rb_insert(rb_root_t *root, rb_node_t *node,
               bool (*less)(const rb_node_t *node_a,
                           const rb_node_t *node_b))
{
    rb_node_t *parent = NULL;
    rb_node_t *current = root->node;

    /* 初始化节点 */
    RB_NODE_INIT(node);

    /* 查找插入位置 */
    while (current != NULL)
    {
        parent = current;

        if (less(node, current))
        {
            current = current->left;
        }
        else
        {
            current = current->right;
        }
    }

    /* 插入节点 */
    node->parent = parent;

    if (parent == NULL)
    {
        /* 树为空，新节点成为根节点 */
        root->node = node;
    }
    else if (less(node, parent))
    {
        parent->left = node;
    }
    else
    {
        parent->right = node;
    }

    /* 插入后修复，维护红黑树性质 */
    rb_insert_fixup(root, node);
}

/**
 * @brief 红黑树删除节点
 *
 * @details 从红黑树中删除节点，自动维护红黑树性质。
 *
 * @param root    红黑树根指针
 * @param node    要删除的节点
 */
void rb_delete(rb_root_t *root, rb_node_t *node)
{
    rb_node_t *child;
    rb_node_t *succ;
    rb_color_t original_color;

    if (node == NULL)
    {
        return;
    }

    original_color = node->color;

    if (node->left == NULL)
    {
        /* 没有左子节点 */
        child = node->right;

        if (node->parent == NULL)
        {
            root->node = child;
        }
        else if (node == node->parent->left)
        {
            node->parent->left = child;
        }
        else
        {
            node->parent->right = child;
        }

        if (child != NULL)
        {
            child->parent = node->parent;
        }
    }
    else if (node->right == NULL)
    {
        /* 没有右子节点 */
        child = node->left;

        if (node->parent == NULL)
        {
            root->node = child;
        }
        else if (node == node->parent->left)
        {
            node->parent->left = child;
        }
        else
        {
            node->parent->right = child;
        }

        if (child != NULL)
        {
            child->parent = node->parent;
        }
    }
    else
    {
        /* 有两个子节点，找到后继节点 */
        succ = rb_minimum(node->right);
        original_color = succ->color;
        child = succ->right;

        if (succ->parent == node)
        {
            child->parent = succ;
        }
        else
        {
            if (succ->parent != NULL)
            {
                succ->parent->left = child;
            }
            if (child != NULL)
            {
                child->parent = succ->parent;
            }
            succ->right = node->right;
            node->right->parent = succ;
        }

        if (node->parent == NULL)
        {
            root->node = succ;
        }
        else if (node == node->parent->left)
        {
            node->parent->left = succ;
        }
        else
        {
            node->parent->right = succ;
        }

        succ->left = node->left;
        node->left->parent = succ;
        succ->color = node->color;
        succ->parent = node->parent;
    }

    /* 如果删除的是黑色节点，需要修复 */
    if (original_color == RB_COLOR_BLACK)
    {
        if (child != NULL)
        {
            rb_delete_fixup(root, child);
        }
        else
        {
            /* 特殊情况：删除的是黑色叶子节点 */
            rb_delete_fixup(root, node);
        }
    }
}

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
                   int32_t (*compare)(const void *key, const rb_node_t *node))
{
    rb_node_t *node = root->node;
    int32_t result;

    while (node != NULL)
    {
        result = compare(key, node);

        if (result < 0)
        {
            node = node->left;
        }
        else if (result > 0)
        {
            node = node->right;
        }
        else
        {
            return node;
        }
    }

    return NULL;
}

/**
 * @brief 红黑树查找最小节点
 *
 * @param root    红黑树根指针
 *
 * @return 最小的节点指针，树为空返回 NULL
 */
rb_node_t *rb_first(rb_root_t *root)
{
    return rb_minimum(root->node);
}

/**
 * @brief 红黑树查找最大节点
 *
 * @param root    红黑树根指针
 *
 * @return 最大的节点指针，树为空返回 NULL
 */
rb_node_t *rb_last(rb_root_t *root)
{
    return rb_maximum(root->node);
}

/**
 * @brief 红黑树查找下一个节点（中序遍历）
 *
 * @param node    当前节点
 *
 * @return 下一个节点指针，无下一个返回 NULL
 */
rb_node_t *rb_next(rb_node_t *node)
{
    rb_node_t *parent;

    if (node == NULL)
    {
        return NULL;
    }

    /* 如果有右子节点，下一个是最右子节点的最左节点 */
    if (node->right != NULL)
    {
        return rb_minimum(node->right);
    }

    /* 否则，向上找到第一个是左子节点的祖先 */
    parent = node->parent;

    while ((parent != NULL) && (node == parent->right))
    {
        node = parent;
        parent = parent->parent;
    }

    return parent;
}

/**
 * @brief 红黑树查找上一个节点（中序遍历）
 *
 * @param node    当前节点
 *
 * @return 上一个节点指针，无上一个返回 NULL
 */
rb_node_t *rb_prev(rb_node_t *node)
{
    rb_node_t *parent;

    if (node == NULL)
    {
        return NULL;
    }

    /* 如果有左子节点，上一个是最左子节点的最右节点 */
    if (node->left != NULL)
    {
        return rb_maximum(node->left);
    }

    /* 否则，向上找到第一个是右子节点的祖先 */
    parent = node->parent;

    while ((parent != NULL) && (node == parent->left))
    {
        node = parent;
        parent = parent->parent;
    }

    return parent;
}
