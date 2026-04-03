/**
 * @file    test_capability.c
 * @brief   AISafe64 RTOS - 能力撤销与传递降权单元测试（宿主机）
 * @author  AISafe64 Team
 * @date    2026-04-03
 * @version 1.0
 *
 * @details 能力系统 cap_revoke（级联撤销）与 cap_copy 降权测试：
 *          1. 单个能力撤销（无子能力）
 *          2. 级联撤销（父撤销时子能力全部撤销）
 *          3. 撤销权限检查（无 CAP_RIGHT_REVOKE 应失败）
 *          4. 空闲槽撤销（应返回错误）
 *          5. 已撤销能力再次撤销（应返回错误）
 *          6. 多级级联撤销（A->B->C 链式）
 *          7. 大规模撤销（多个子能力同时撤销）
 *          8. 能力复制时降权（子能力权限不能超过父能力）
 *          9. 传递降权（A->B->C，每级权限递减）
 *
 * @note 宿主机单线程模拟
 * @note 对应需求: KR-015（能力撤销）、KR-014（能力传递降权）
 */

#include "mock_kernel.h"

/* ========================================================================
 * 侵入式双向链表（与 kernel/list.h 一致，测试内使用不同函数名）
 * ======================================================================== */

struct list_head
{
    struct list_head *next;
    struct list_head *prev;
};

static inline void tl_init(struct list_head *h)
{
    if (h == NULL) { return; }
    h->next = h;
    h->prev = h;
}

static inline int tl_empty(const struct list_head *h)
{
    if (h == NULL) { return 1; }
    return (h->next == h) ? 1 : 0;
}

static inline void tl_add_tail(struct list_head *n, struct list_head *h)
{
    if (n == NULL || h == NULL) { return; }
    h->prev->next = n;
    n->prev = h->prev;
    n->next = h;
    h->prev = n;
}

static inline void tl_del_init(struct list_head *e)
{
    if (e == NULL) { return; }
    if (e->prev != NULL && e->next != NULL)
    {
        e->prev->next = e->next;
        e->next->prev = e->prev;
    }
    tl_init(e);
}

#undef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* ========================================================================
 * 配置常量
 * ======================================================================== */

#define TEST_CSPACE_CAPACITY    64U
#define TEST_MAX_CSPACES        32U
#define KOBJ_ID_INVALID         ((kobj_id_t)0U)

/* ========================================================================
 * 权限位定义（与 kernel/capability.h 一致）
 * ======================================================================== */

#define CAP_RIGHT_READ      (1U << 0U)
#define CAP_RIGHT_WRITE     (1U << 1U)
#define CAP_RIGHT_EXECUTE   (1U << 2U)
#define CAP_RIGHT_GRANT     (1U << 3U)
#define CAP_RIGHT_REVOKE    (1U << 4U)
#define CAP_RIGHT_ALL       (CAP_RIGHT_READ | CAP_RIGHT_WRITE | \
                             CAP_RIGHT_EXECUTE | CAP_RIGHT_GRANT | \
                             CAP_RIGHT_REVOKE)
#define CAP_RIGHT_NONE      0U

#define CAP_SLOT_INVALID    ((uint32_t)0xFFFFFFFFU)

/* ========================================================================
 * 内核对象类型（最小子集）
 * ======================================================================== */

typedef enum
{
    KOBJ_THREAD = 0U,
    KOBJ_ENDPOINT,
    KOBJ_CSPACE,
    KOBJ_TYPE_COUNT
} test_kobj_type_t;

/* ========================================================================
 * 能力状态枚举
 * ======================================================================== */

typedef enum
{
    CAP_STATE_FREE = 0U,
    CAP_STATE_VALID,
    CAP_STATE_REVOKED
} test_cap_state_t;

/* ========================================================================
 * cap_t 能力描述符（与 kernel/capability.h 一致）
 * ======================================================================== */

typedef uint32_t test_cap_slot_t;

typedef struct
{
    test_cap_state_t   state;
    test_kobj_type_t   kobj_type;
    uint8_t            rights;
    uint16_t           badge;
    kobj_id_t          kobj_id;
    test_cap_slot_t    parent_slot;
    test_cap_slot_t    cspace_root;
    struct list_head   children;
    struct list_head   sibling;
} test_cap_t;

/* ========================================================================
 * cspace_t（简化版，用于测试）
 * ======================================================================== */

typedef struct
{
    uint8_t          type;
    kobj_id_t        id;
    test_cap_t       *cap_table;
    uint32_t         capacity;
    uint32_t         used_count;
    test_cap_slot_t  root_slot;
    test_cap_slot_t  free_head;
    TicketLock_t     lock;
} test_cspace_t;

/* ========================================================================
 * 测试专用静态存储
 * ======================================================================== */

static test_cspace_t s_tcs[4U];
static test_cap_t   s_tcaps[4U][TEST_CSPACE_CAPACITY];
static uint32_t     s_next_id = 100U;

/* ========================================================================
 * 显式栈最大深度
 * ======================================================================== */

#define REVOKE_STACK_SIZE   128U

/* ========================================================================
 * 测试辅助函数
 * ======================================================================== */

/**
 * @brief 初始化测试 CSpace
 */
static void t_cspace_init(uint32_t idx, uint32_t capacity)
{
    test_cspace_t *cs;
    uint32_t i;

    if (idx >= 4U) { return; }

    cs = &s_tcs[idx];
    cs->cap_table = &s_tcaps[idx][0U];
    cs->capacity = capacity;
    cs->used_count = 0U;
    cs->root_slot = 0U;
    cs->free_head = 1U;
    cs->type = (uint8_t)KOBJ_CSPACE;
    cs->id = (kobj_id_t)(idx + 1U);

    for (i = 0U; i < capacity; i++)
    {
        test_cap_t *cap = &cs->cap_table[i];
        cap->state = CAP_STATE_FREE;
        cap->rights = 0U;
        cap->kobj_type = KOBJ_TYPE_COUNT;
        cap->kobj_id = KOBJ_ID_INVALID;
        cap->parent_slot = CAP_SLOT_INVALID;
        cap->cspace_root = 0U;
        cap->badge = 0U;
        tl_init(&cap->children);
        tl_init(&cap->sibling);
    }

    /* 构建空闲链表 slot 1 ~ capacity-1 */
    for (i = 1U; i < capacity; i++)
    {
        if (i + 1U < capacity)
        {
            s_tcaps[idx][i].sibling.next =
                (struct list_head *)&s_tcaps[idx][i + 1U];
        }
    }

    /* 创建根能力 slot 0 */
    {
        test_cap_t *root = &cs->cap_table[0U];
        root->state = CAP_STATE_VALID;
        root->kobj_type = KOBJ_CSPACE;
        root->rights = (uint8_t)CAP_RIGHT_ALL;
        root->kobj_id = cs->id;
        root->parent_slot = CAP_SLOT_INVALID;
        root->cspace_root = 0U;
        tl_init(&root->children);
        tl_init(&root->sibling);
    }

    cs->used_count = 1U;
    ticket_lock_init(&cs->lock);
}

/**
 * @brief 在 CSpace 槽位插入能力
 */
static kernel_status_t t_cspace_insert(
    uint32_t cs_idx,
    test_cap_slot_t slot,
    test_kobj_type_t kobj_type,
    kobj_id_t kobj_id,
    uint8_t rights,
    test_cap_slot_t parent_slot)
{
    test_cspace_t *cs;
    test_cap_t *cap;
    test_cap_t *parent;

    if (cs_idx >= 4U) { return -(int32_t)22; }

    cs = &s_tcs[cs_idx];
    if (slot >= cs->capacity) { return -(int32_t)22; }

    cap = &cs->cap_table[slot];
    if (cap->state != CAP_STATE_FREE) { return -(int32_t)22; }

    cap->state = CAP_STATE_VALID;
    cap->kobj_type = kobj_type;
    cap->rights = rights;
    cap->kobj_id = kobj_id;
    cap->parent_slot = parent_slot;
    cap->cspace_root = cs->root_slot;
    cap->badge = 0U;
    tl_init(&cap->children);
    tl_init(&cap->sibling);

    if (parent_slot != CAP_SLOT_INVALID)
    {
        if (parent_slot >= cs->capacity)
        {
            cap->state = CAP_STATE_FREE;
            return -(int32_t)22;
        }
        parent = &cs->cap_table[parent_slot];
        if (parent->state != CAP_STATE_VALID)
        {
            cap->state = CAP_STATE_FREE;
            return -(int32_t)22;
        }
        tl_add_tail(&cap->sibling, &parent->children);
    }

    cs->used_count++;
    return KERNEL_OK;
}

/**
 * @brief 从 sibling 节点反算 test_cap_t 指针
 */
static inline test_cap_t *cap_from_sibling(struct list_head *node)
{
    return container_of(node, test_cap_t, sibling);
}

/**
 * @brief 能力复制（可降权）
 */
static kernel_status_t t_cap_copy(
    uint32_t src_cs_idx,
    test_cap_slot_t src_slot,
    uint32_t dst_cs_idx,
    test_cap_slot_t dst_slot,
    uint8_t rights_mask)
{
    test_cspace_t *src_cs;
    test_cspace_t *dst_cs;
    test_cap_t *src_cap;
    test_cap_t *dst_cap;
    uint8_t dest_rights;

    if (src_cs_idx >= 4U || dst_cs_idx >= 4U) { return -(int32_t)22; }

    src_cs = &s_tcs[src_cs_idx];
    dst_cs = &s_tcs[dst_cs_idx];

    src_cap = &src_cs->cap_table[src_slot];
    if (src_cap->state != CAP_STATE_VALID) { return -(int32_t)2; }

    /* 检查 GRANT 权限 */
    if ((src_cap->rights & CAP_RIGHT_GRANT) == 0U)
    {
        return -(int32_t)13;  /* EACCES */
    }

    /* 确定目标权限 */
    if (rights_mask != 0U)
    {
        dest_rights = src_cap->rights & rights_mask;
    }
    else
    {
        dest_rights = src_cap->rights;
    }

    /* 权限不可提升 */
    if ((dest_rights & src_cap->rights) != dest_rights)
    {
        return -(int32_t)13;  /* EACCES */
    }

    /* 插入目标能力 */
    kernel_status_t ret = t_cspace_insert(dst_cs_idx, dst_slot,
                                           src_cap->kobj_type,
                                           src_cap->kobj_id,
                                           dest_rights,
                                           src_slot);
    if (ret != KERNEL_OK) { return ret; }

    /* 建立父子关系 */
    dst_cap = &dst_cs->cap_table[dst_slot];
    if (dst_cap != NULL)
    {
        tl_add_tail(&dst_cap->sibling, &src_cap->children);
    }

    return KERNEL_OK;
}

/**
 * @brief 能力撤销（级联，显式栈，非递归）
 */
static kernel_status_t t_cap_revoke(uint32_t cs_idx, test_cap_slot_t slot)
{
    test_cspace_t *cs;
    test_cap_t *target;
    static test_cap_slot_t stack[REVOKE_STACK_SIZE];
    uint32_t top;

    if (cs_idx >= 4U) { return -(int32_t)22; }

    cs = &s_tcs[cs_idx];

    if (slot >= cs->capacity) { return -(int32_t)22; }

    target = &cs->cap_table[slot];
    if (target == NULL) { return -(int32_t)2; }
    if (target->state != CAP_STATE_VALID) { return -(int32_t)2; }

    /* 检查 REVOKE 权限 */
    if ((target->rights & CAP_RIGHT_REVOKE) == 0U)
    {
        return -(int32_t)13;  /* EACCES */
    }

    /* 初始化显式栈 */
    top = 0U;
    stack[top] = slot;
    top++;

    /* 非递归 DFS */
    while (top > 0U)
    {
        test_cap_slot_t cur_slot;
        test_cap_t *cur;

        top--;
        cur_slot = stack[top];

        cur = &cs->cap_table[cur_slot];
        if (cur->state != CAP_STATE_VALID)
        {
            continue;
        }

        /* 将子能力压栈 */
        {
            struct list_head *pos;
            struct list_head *n;

            for (pos = cur->children.next, n = pos->next;
                 pos != &cur->children;
                 pos = n, n = pos->next)
            {
                test_cap_t *child = cap_from_sibling(pos);

                if (top < REVOKE_STACK_SIZE)
                {
                    test_cap_slot_t child_idx =
                        (test_cap_slot_t)(child - &cs->cap_table[0U]);
                    stack[top] = child_idx;
                    top++;
                }
            }
        }

        /* 撤销当前能力 */
        cur->state = CAP_STATE_REVOKED;

        tl_del_init(&cur->sibling);
        tl_init(&cur->children);
    }

    return KERNEL_OK;
}

/* ========================================================================
 * POSIX 错误码（补充）
 * ======================================================================== */

#ifndef EACCES
#define EACCES  13U
#endif
#ifndef ENOENT
#define ENOENT   2U
#endif

/* ========================================================================
 * 测试全局状态重置
 * ======================================================================== */

static void test_setup(void)
{
    uint32_t i;

    for (i = 0U; i < 4U; i++)
    {
        (void)memset(&s_tcs[i], 0, sizeof(test_cspace_t));
        (void)memset(&s_tcaps[i], 0, sizeof(test_cap_t) * TEST_CSPACE_CAPACITY);
    }

    s_next_id = 100U;

    for (i = 0U; i < 4U; i++)
    {
        t_cspace_init(i, TEST_CSPACE_CAPACITY);
    }
}

/* ========================================================================
 * 测试 1: 单个能力撤销（无子能力）
 * ======================================================================== */
static void test_revoke_single_cap(void)
{
    kernel_status_t ret;
    test_cap_t *cap;

    printf("  测试 1: 单个能力撤销（无子能力）\n");

    ret = t_cspace_insert(0U, 1U, KOBJ_ENDPOINT, 200U,
                          CAP_RIGHT_READ | CAP_RIGHT_REVOKE, 0U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    ret = t_cap_revoke(0U, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    cap = &s_tcs[0U].cap_table[1U];
    TEST_ASSERT_EQ((int)cap->state, (int)CAP_STATE_REVOKED);

    /* 根能力不受影响 */
    cap = &s_tcs[0U].cap_table[0U];
    TEST_ASSERT_EQ((int)cap->state, (int)CAP_STATE_VALID);
}

/* ========================================================================
 * 测试 2: 级联撤销
 * ======================================================================== */
static void test_revoke_cascade(void)
{
    kernel_status_t ret;
    test_cap_t *parent_cap;
    test_cap_t *child1;
    test_cap_t *child2;

    printf("  测试 2: 级联撤销\n");

    /* slot 1: 父能力 */
    ret = t_cspace_insert(0U, 1U, KOBJ_ENDPOINT, 200U,
                          CAP_RIGHT_READ | CAP_RIGHT_WRITE |
                          CAP_RIGHT_GRANT | CAP_RIGHT_REVOKE, 0U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* slot 2: 子能力1 */
    ret = t_cspace_insert(0U, 2U, KOBJ_ENDPOINT, 200U,
                          CAP_RIGHT_READ | CAP_RIGHT_WRITE, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* slot 3: 子能力2 */
    ret = t_cspace_insert(0U, 3U, KOBJ_ENDPOINT, 200U,
                          CAP_RIGHT_READ | CAP_RIGHT_WRITE, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 撤销父能力 */
    ret = t_cap_revoke(0U, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    parent_cap = &s_tcs[0U].cap_table[1U];
    child1 = &s_tcs[0U].cap_table[2U];
    child2 = &s_tcs[0U].cap_table[3U];

    TEST_ASSERT_EQ((int)parent_cap->state, (int)CAP_STATE_REVOKED);
    TEST_ASSERT_EQ((int)child1->state, (int)CAP_STATE_REVOKED);
    TEST_ASSERT_EQ((int)child2->state, (int)CAP_STATE_REVOKED);
}

/* ========================================================================
 * 测试 3: 撤销权限检查
 * ======================================================================== */
static void test_revoke_no_permission(void)
{
    kernel_status_t ret;

    printf("  测试 3: 撤销权限检查\n");

    /* 创建只有 READ 权限的能力 */
    ret = t_cspace_insert(0U, 1U, KOBJ_ENDPOINT, 200U,
                          CAP_RIGHT_READ, 0U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 尝试撤销（无 REVOKE 权限） */
    ret = t_cap_revoke(0U, 1U);
    TEST_ASSERT_EQ(ret, -(int32_t)EACCES);
}

/* ========================================================================
 * 测试 4: 空闲槽撤销
 * ======================================================================== */
static void test_revoke_free_slot(void)
{
    kernel_status_t ret;

    printf("  测试 4: 空闲槽撤销\n");

    /* slot 1 保持空闲，直接尝试撤销 */
    ret = t_cap_revoke(0U, 1U);
    TEST_ASSERT_EQ(ret, -(int32_t)ENOENT);
}

/* ========================================================================
 * 测试 5: 已撤销能力再次撤销
 * ======================================================================== */
static void test_revoke_already_revoked(void)
{
    kernel_status_t ret;

    printf("  测试 5: 已撤销能力再次撤销\n");

    ret = t_cspace_insert(0U, 1U, KOBJ_ENDPOINT, 200U,
                          CAP_RIGHT_READ | CAP_RIGHT_REVOKE, 0U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 第一次撤销 */
    ret = t_cap_revoke(0U, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 第二次撤销应失败 */
    ret = t_cap_revoke(0U, 1U);
    TEST_ASSERT_EQ(ret, -(int32_t)ENOENT);
}

/* ========================================================================
 * 测试 6: 多级级联撤销（A->B->C）
 * ======================================================================== */
static void test_revoke_multi_level(void)
{
    kernel_status_t ret;
    test_cap_t *a;
    test_cap_t *b;
    test_cap_t *c;

    printf("  测试 6: 多级级联撤销（A->B->C）\n");

    /* A: slot 1 */
    ret = t_cspace_insert(0U, 1U, KOBJ_ENDPOINT, 300U,
                          CAP_RIGHT_ALL, 0U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* B: slot 2 (A 的子能力) */
    ret = t_cspace_insert(0U, 2U, KOBJ_ENDPOINT, 300U,
                          CAP_RIGHT_READ | CAP_RIGHT_WRITE |
                          CAP_RIGHT_GRANT | CAP_RIGHT_REVOKE, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* C: slot 3 (B 的子能力) */
    ret = t_cspace_insert(0U, 3U, KOBJ_ENDPOINT, 300U,
                          CAP_RIGHT_READ, 2U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 撤销 A */
    ret = t_cap_revoke(0U, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    a = &s_tcs[0U].cap_table[1U];
    b = &s_tcs[0U].cap_table[2U];
    c = &s_tcs[0U].cap_table[3U];

    TEST_ASSERT_EQ((int)a->state, (int)CAP_STATE_REVOKED);
    TEST_ASSERT_EQ((int)b->state, (int)CAP_STATE_REVOKED);
    TEST_ASSERT_EQ((int)c->state, (int)CAP_STATE_REVOKED);
}

/* ========================================================================
 * 测试 7: 大规模撤销（10 个子能力）
 * ======================================================================== */
static void test_revoke_many_children(void)
{
    kernel_status_t ret;
    test_cap_t *parent;
    uint32_t i;
    uint32_t child_count;

    printf("  测试 7: 大规模撤销（10 个子能力）\n");

    ret = t_cspace_insert(0U, 1U, KOBJ_ENDPOINT, 400U,
                          CAP_RIGHT_ALL, 0U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    for (i = 2U; i <= 11U; i++)
    {
        ret = t_cspace_insert(0U, i, KOBJ_ENDPOINT, 400U,
                              CAP_RIGHT_READ | CAP_RIGHT_WRITE, 1U);
        TEST_ASSERT_EQ(ret, KERNEL_OK);
    }

    /* 验证子能力数量 */
    parent = &s_tcs[0U].cap_table[1U];
    child_count = 0U;
    {
        struct list_head *pos;
        for (pos = parent->children.next;
             pos != &parent->children;
             pos = pos->next)
        {
            child_count++;
        }
    }
    TEST_ASSERT_EQ((int)child_count, 10);

    ret = t_cap_revoke(0U, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    parent = &s_tcs[0U].cap_table[1U];
    TEST_ASSERT_EQ((int)parent->state, (int)CAP_STATE_REVOKED);

    for (i = 2U; i <= 11U; i++)
    {
        test_cap_t *child = &s_tcs[0U].cap_table[i];
        TEST_ASSERT_EQ((int)child->state, (int)CAP_STATE_REVOKED);
    }
}

/* ========================================================================
 * 测试 8: 能力复制时降权
 * ======================================================================== */
static void test_copy_rights_demotion(void)
{
    kernel_status_t ret;
    test_cap_t *src;
    test_cap_t *dst;

    printf("  测试 8: 能力复制时降权\n");

    /* slot 1: 全部权限 */
    ret = t_cspace_insert(0U, 1U, KOBJ_ENDPOINT, 500U,
                          CAP_RIGHT_ALL, 0U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 复制到 slot 2，降权为只读 */
    ret = t_cap_copy(0U, 1U, 0U, 2U, CAP_RIGHT_READ);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    dst = &s_tcs[0U].cap_table[2U];
    TEST_ASSERT_EQ((int)dst->rights, (int)CAP_RIGHT_READ);

    src = &s_tcs[0U].cap_table[1U];
    TEST_ASSERT_EQ((int)src->rights, (int)CAP_RIGHT_ALL);

    /* slot 3: 只有 READ + GRANT */
    ret = t_cspace_insert(0U, 3U, KOBJ_ENDPOINT, 501U,
                          CAP_RIGHT_READ | CAP_RIGHT_GRANT, 0U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 复制时请求 READ+WRITE，但源只有 READ+GRANT，结果只有 READ */
    ret = t_cap_copy(0U, 3U, 0U, 4U,
                     CAP_RIGHT_READ | CAP_RIGHT_WRITE);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    dst = &s_tcs[0U].cap_table[4U];
    TEST_ASSERT_EQ((int)dst->rights, (int)CAP_RIGHT_READ);
}

/* ========================================================================
 * 测试 9: 传递降权（A->B->C）
 * ======================================================================== */
static void test_transitive_rights_demotion(void)
{
    kernel_status_t ret;
    test_cap_t *a;
    test_cap_t *b;
    test_cap_t *c;

    printf("  测试 9: 传递降权（A->B->C）\n");

    /* A: 全部权限 */
    ret = t_cspace_insert(0U, 1U, KOBJ_ENDPOINT, 600U,
                          CAP_RIGHT_ALL, 0U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* B: 从 A 复制，降权为 READ|WRITE|GRANT|REVOKE */
    ret = t_cap_copy(0U, 1U, 0U, 2U,
                     CAP_RIGHT_READ | CAP_RIGHT_WRITE |
                     CAP_RIGHT_GRANT | CAP_RIGHT_REVOKE);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    b = &s_tcs[0U].cap_table[2U];
    TEST_ASSERT_EQ((int)b->rights,
                    (int)(CAP_RIGHT_READ | CAP_RIGHT_WRITE |
                          CAP_RIGHT_GRANT | CAP_RIGHT_REVOKE));

    /* C: 从 B 复制，降权为 READ|WRITE */
    ret = t_cap_copy(0U, 2U, 0U, 3U,
                     CAP_RIGHT_READ | CAP_RIGHT_WRITE);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    c = &s_tcs[0U].cap_table[3U];
    TEST_ASSERT_EQ((int)c->rights,
                    (int)(CAP_RIGHT_READ | CAP_RIGHT_WRITE));

    /* 验证权限递减 */
    a = &s_tcs[0U].cap_table[1U];
    TEST_ASSERT_EQ((int)a->rights, (int)CAP_RIGHT_ALL);
    TEST_ASSERT((b->rights & a->rights) == b->rights);
    TEST_ASSERT((c->rights & b->rights) == c->rights);

    /* C 没有 GRANT 权限，不能复制 */
    ret = t_cap_copy(0U, 3U, 0U, 4U, CAP_RIGHT_READ);
    TEST_ASSERT_EQ(ret, -(int32_t)EACCES);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("\n============================================\n");
    printf("  AISafeOS64 - 能力撤销与传递降权测试\n");
    printf("============================================\n\n");

    TEST_RESET();

    test_setup();
    test_revoke_single_cap();

    test_setup();
    test_revoke_cascade();

    test_setup();
    test_revoke_no_permission();

    test_setup();
    test_revoke_free_slot();

    test_setup();
    test_revoke_already_revoked();

    test_setup();
    test_revoke_multi_level();

    test_setup();
    test_revoke_many_children();

    test_setup();
    test_copy_rights_demotion();

    test_setup();
    test_transitive_rights_demotion();

    printf("\n");
    TEST_SUMMARY("capability");

    return TEST_RESULT();
}
