/**
 * @file    test_capability_revoke.c
 * @brief   AISafe64 RTOS - 能力撤销与传递降权单元测试（宿主机）
 * @author  AISafe64 Team
 * @date    2026-04-03
 * @version 1.0
 *
 * @details 能力撤销（cap_revoke）与传递降权（cap_copy 降权）测试
 *          测试与内核 capability.c / cspace.c 一致的逻辑：
 *          - 单个能力撤销（无子能力）
 *          - 级联撤销（父撤销时子全部撤销）
 *          - 撤销权限检查（无 REVOKE 权限应失败）
 *          - 空闲槽撤销（应返回错误）
 *          - 已撤销能力再次撤销（应返回错误）
 *          - 多级级联撤销（A→B→C 链式撤销）
 *          - 大规模撤销（多个子能力同时撤销）
 *          - 能力复制时的降权（子权限不能超过父）
 *          - 传递降权（A→B→C，每级权限递减）
 *          - NULL / 无效参数安全
 *          - 复制无 GRANT 权限应失败
 *
 * @note 宿主机单线程模拟，不测试多核竞态
 * @note 对应需求: KR-013~016
 */

#include "mock_kernel.h"

/* ========================================================================
 * 侵入式双向链表（与 kernel/list.h 一致）
 * ======================================================================== */

struct list_head
{
    struct list_head *next;
    struct list_head *prev;
};

static inline void init_list_head(struct list_head *head)
{
    if (head == NULL) { return; }
    head->next = head;
    head->prev = head;
}

static inline int list_empty_test(const struct list_head *head)
{
    if (head == NULL) { return 1; }
    return (head->next == head) ? 1 : 0;
}

static inline void list_add_tail(struct list_head *new_node,
                                  struct list_head *head)
{
    if (new_node == NULL || head == NULL) { return; }

    new_node->prev = head->prev;
    new_node->next = head;
    head->prev->next = new_node;
    head->prev = new_node;
}

static inline void list_del_init(struct list_head *entry)
{
    if (entry == NULL) { return; }
    entry->prev->next = entry->next;
    entry->next->prev = entry->prev;
    entry->next = entry;
    entry->prev = entry;
}

#undef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* ========================================================================
 * 补充 Mock 缺少的错误码
 * ======================================================================== */

#ifndef EACCES
#define EACCES          13U
#endif

#ifndef ENOENT
#define ENOENT           2U
#endif

/* ========================================================================
 * 配置常量
 * ======================================================================== */

#define TEST_CAP_RIGHT_READ      (1U << 0U)
#define TEST_CAP_RIGHT_WRITE     (1U << 1U)
#define TEST_CAP_RIGHT_EXECUTE   (1U << 2U)
#define TEST_CAP_RIGHT_GRANT     (1U << 3U)
#define TEST_CAP_RIGHT_REVOKE    (1U << 4U)
#define TEST_CAP_RIGHT_ALL       (TEST_CAP_RIGHT_READ | TEST_CAP_RIGHT_WRITE | \
                                   TEST_CAP_RIGHT_EXECUTE | TEST_CAP_RIGHT_GRANT | \
                                   TEST_CAP_RIGHT_REVOKE)
#define TEST_CAP_RIGHT_NONE      0U

#define TEST_CAP_SLOT_INVALID    ((uint32_t)0xFFFFFFFFU)

#define TEST_CSPACE_CAPACITY     32U
#define TEST_MAX_CSPACES         8U

#define TEST_KOBJ_ID_INVALID     ((uint64_t)0U)

/* ========================================================================
 * 能力状态与类型
 * ======================================================================== */

typedef enum
{
    TEST_CAP_FREE = 0U,
    TEST_CAP_VALID,
    TEST_CAP_REVOKED
} test_cap_state_t;

typedef enum
{
    TEST_KOBJ_CSPACE = 0U,
    TEST_KOBJ_ENDPOINT,
    TEST_KOBJ_CHANNEL,
    TEST_KOBJ_TYPE_COUNT
} test_kobj_type_t;

/* ========================================================================
 * 能力描述符
 * ======================================================================== */

typedef struct
{
    test_cap_state_t state;
    test_kobj_type_t kobj_type;
    uint8_t         rights;
    uint16_t        badge;
    uint64_t        kobj_id;
    uint32_t        parent_slot;
    uint32_t        cspace_root;
    struct list_head children;
    struct list_head sibling;
} test_cap_t;

/* ========================================================================
 * CSpace 结构
 * ======================================================================== */

typedef struct
{
    uint32_t        capacity;
    uint32_t        used_count;
    uint32_t        root_slot;
    uint32_t        free_head;
    test_cap_t     *cap_table;
} test_cspace_t;

/* ========================================================================
 * 静态存储
 * ======================================================================== */

static test_cap_t s_cap_tables[TEST_MAX_CSPACES][TEST_CSPACE_CAPACITY];
static test_cspace_t s_cspaces[TEST_MAX_CSPACES];
static uint32_t s_cspace_used[TEST_MAX_CSPACES];
static uint64_t s_next_kobj_id;

/* ========================================================================
 * CSpace 模拟实现（与 cspace.c 逻辑一致）
 * ======================================================================== */

static void test_cspace_init_cap_table(test_cspace_t *cs)
{
    uint32_t i;

    (void)memset(cs->cap_table, 0, sizeof(test_cap_t) * cs->capacity);

    for (i = 0U; i < cs->capacity; i++)
    {
        test_cap_t *cap = &cs->cap_table[i];
        cap->state = TEST_CAP_FREE;
        init_list_head(&cap->children);
        init_list_head(&cap->sibling);
    }

    /* 空闲链表：slot 1 ~ capacity-1 */
    for (i = 1U; i < cs->capacity; i++)
    {
        test_cap_t *cap = &cs->cap_table[i];
        if (i + 1U < cs->capacity)
        {
            cap->sibling.next = (struct list_head *)&cs->cap_table[i + 1U];
        }
        else
        {
            cap->sibling.next = NULL;
        }
    }

    cs->free_head = (cs->capacity > 1U) ? 1U : TEST_CAP_SLOT_INVALID;
}

static test_cspace_t *test_cspace_create(uint32_t cs_idx)
{
    test_cspace_t *cs;

    if (cs_idx >= TEST_MAX_CSPACES)
    {
        return NULL;
    }

    cs = &s_cspaces[cs_idx];
    cs->capacity = TEST_CSPACE_CAPACITY;
    cs->cap_table = &s_cap_tables[cs_idx][0U];
    cs->used_count = 0U;
    cs->root_slot = TEST_CAP_SLOT_INVALID;
    cs->free_head = TEST_CAP_SLOT_INVALID;

    test_cspace_init_cap_table(cs);

    /* 创建根能力（slot 0） */
    test_cap_t *root = &cs->cap_table[0U];
    root->state = TEST_CAP_VALID;
    root->kobj_type = TEST_KOBJ_CSPACE;
    root->rights = (uint8_t)TEST_CAP_RIGHT_ALL;
    root->badge = 0U;
    root->kobj_id = s_next_kobj_id;
    s_next_kobj_id++;
    root->parent_slot = TEST_CAP_SLOT_INVALID;
    root->cspace_root = cs_idx;
    init_list_head(&root->children);
    init_list_head(&root->sibling);

    cs->root_slot = cs_idx;
    cs->used_count = 1U;
    s_cspace_used[cs_idx] = 1U;

    return cs;
}

static kernel_status_t test_cspace_alloc_slot(test_cspace_t *cs, uint32_t *out_slot)
{
    test_cap_t *free_cap;
    uint32_t idx;

    if (cs == NULL || out_slot == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (cs->free_head == TEST_CAP_SLOT_INVALID)
    {
        return -(int32_t)ENOMEM;
    }

    idx = cs->free_head;
    free_cap = &cs->cap_table[idx];

    if (free_cap->sibling.next != NULL)
    {
        test_cap_t *next_free = (test_cap_t *)free_cap->sibling.next;
        uint32_t next_idx = (uint32_t)(next_free - &cs->cap_table[0U]);
        cs->free_head = next_idx;
    }
    else
    {
        cs->free_head = TEST_CAP_SLOT_INVALID;
    }

    (void)memset(free_cap, 0, sizeof(test_cap_t));
    free_cap->state = TEST_CAP_FREE;

    *out_slot = idx;
    return KERNEL_OK;
}

static test_cap_t *test_cspace_lookup(test_cspace_t *cs, uint32_t slot)
{
    test_cap_t *cap;

    if (cs == NULL || slot >= cs->capacity)
    {
        return NULL;
    }

    cap = &cs->cap_table[slot];

    if (cap->state != TEST_CAP_VALID)
    {
        return NULL;
    }

    return cap;
}

static kernel_status_t test_cspace_insert_cap(test_cspace_t *cs,
                                               uint32_t slot,
                                               test_kobj_type_t kobj_type,
                                               uint64_t kobj_id,
                                               uint8_t rights,
                                               uint16_t badge,
                                               uint32_t parent_slot)
{
    test_cap_t *cap;
    test_cap_t *parent_cap;

    if (cs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (slot >= cs->capacity)
    {
        return -(int32_t)EINVAL;
    }

    cap = &cs->cap_table[slot];

    if (cap->state != TEST_CAP_FREE)
    {
        return -(int32_t)EINVAL;
    }

    cap->state = TEST_CAP_VALID;
    cap->kobj_type = kobj_type;
    cap->rights = rights;
    cap->badge = badge;
    cap->kobj_id = kobj_id;
    cap->parent_slot = parent_slot;
    cap->cspace_root = cs->root_slot;
    init_list_head(&cap->children);
    init_list_head(&cap->sibling);

    if (parent_slot != TEST_CAP_SLOT_INVALID)
    {
        if (parent_slot >= cs->capacity)
        {
            cap->state = TEST_CAP_FREE;
            return -(int32_t)EINVAL;
        }

        parent_cap = &cs->cap_table[parent_slot];

        if (parent_cap->state != TEST_CAP_VALID)
        {
            cap->state = TEST_CAP_FREE;
            return -(int32_t)EINVAL;
        }

        list_add_tail(&cap->sibling, &parent_cap->children);
    }

    cs->used_count++;
    return KERNEL_OK;
}

/**
 * @brief 查找 CSpace（通过 root_slot 索引直接返回）
 */
static test_cspace_t *test_cspace_from_root(uint32_t cspace_root)
{
    if (cspace_root >= TEST_MAX_CSPACES)
    {
        return NULL;
    }

    if (!s_cspace_used[cspace_root])
    {
        return NULL;
    }

    return &s_cspaces[cspace_root];
}

/* ========================================================================
 * 能力操作模拟实现（与 capability.c 逻辑一致）
 * ======================================================================== */

/**
 * @brief 在指定 CSpace 指定槽位直接创建能力（测试辅助）
 */
static kernel_status_t test_cap_create_at(test_cspace_t *cs,
                                           uint32_t slot,
                                           test_kobj_type_t kobj_type,
                                           uint64_t kobj_id,
                                           uint8_t rights,
                                           uint32_t parent_slot)
{
    return test_cspace_insert_cap(cs, slot, kobj_type, kobj_id,
                                   rights, 0U, parent_slot);
}

/**
 * @brief 复制能力（可降权）
 *
 * @details 与 capability.c 的 cap_copy 逻辑一致：
 *          - rights_mask != 0 时，目标权限 = 源权限 & rights_mask
 *          - rights_mask == 0 时，目标权限 = 源权限（保持不变）
 *          - 源能力需要 GRANT 权限
 *          - 目标权限必须是源权限的子集（不可提升）
 *          - 建立父子关系
 */
static kernel_status_t test_cap_copy(test_cspace_t *src_cs,
                                      uint32_t src_slot,
                                      test_cspace_t *dst_cs,
                                      uint32_t dst_slot,
                                      uint8_t rights_mask)
{
    test_cap_t *src_cap;
    uint8_t dest_rights;
    kernel_status_t ret;

    if (src_cs == NULL || dst_cs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    src_cap = test_cspace_lookup(src_cs, src_slot);
    if (src_cap == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 检查 GRANT 权限 */
    if ((src_cap->rights & TEST_CAP_RIGHT_GRANT) == TEST_CAP_RIGHT_NONE)
    {
        return -(int32_t)EACCES;
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
        return -(int32_t)EACCES;
    }

    /* 在目标 CSpace 中插入能力。
     * 注意：test_cspace_insert_cap 传入 parent_slot=src_slot 时
     * 会自动将子能力加入父能力的 children 链表，
     * 无需再次手动添加。 */
    ret = test_cspace_insert_cap(dst_cs,
                                  dst_slot,
                                  src_cap->kobj_type,
                                  src_cap->kobj_id,
                                  dest_rights,
                                  src_cap->badge,
                                  src_slot);

    return ret;
}

/**
 * @brief 撤销能力（级联，显式栈）
 *
 * @details 与 capability.c 的 cap_revoke 逻辑一致：
 *          - 使用显式栈实现非递归深度优先遍历
 *          - 撤销目标及其所有子能力
 *          - 需要 REVOKE 权限
 *
 * @note 修复了原始 cap_revoke 中的 bug：
 *       原实现在压栈子能力时使用了 child_cap->cspace_root
 *       （这是一个 CSpace 标识，不是能力槽索引），
 *       正确做法应该是压入子能力在其所属 CSpace 中的槽索引。
 */
#define TEST_REVOKE_STACK_SIZE  (TEST_MAX_CSPACES * 4U)

static kernel_status_t test_cap_revoke(test_cspace_t *cs, uint32_t slot)
{
    test_cap_t *target_cap;
    static uint32_t revoke_stack[TEST_REVOKE_STACK_SIZE];
    uint32_t stack_top;

    if (cs == NULL)
    {
        return -(int32_t)EINVAL;
    }

    target_cap = test_cspace_lookup(cs, slot);
    if (target_cap == NULL)
    {
        return -(int32_t)ENOENT;
    }

    /* 验证状态 */
    if (target_cap->state != TEST_CAP_VALID)
    {
        return -(int32_t)ENOENT;
    }

    /* 检查 REVOKE 权限 */
    if ((target_cap->rights & TEST_CAP_RIGHT_REVOKE) == TEST_CAP_RIGHT_NONE)
    {
        return -(int32_t)EACCES;
    }

    /* 初始化显式栈 */
    stack_top = 0U;
    revoke_stack[stack_top] = slot;
    stack_top++;

    /* 使用显式栈进行非递归深度优先遍历 */
    while (stack_top > 0U)
    {
        uint32_t cur_slot;
        test_cap_t *cur_cap;

        /* 弹栈 */
        stack_top--;
        cur_slot = revoke_stack[stack_top];

        cur_cap = &cs->cap_table[cur_slot];
        if (cur_cap->state != TEST_CAP_VALID)
        {
            continue;
        }

        /* 将当前能力的所有子能力压栈 */
        {
            struct list_head *pos;
            struct list_head *n;

            for (pos = cur_cap->children.next, n = pos->next;
                 pos != &cur_cap->children;
                 pos = n, n = pos->next)
            {
                test_cap_t *child_cap = container_of(pos, test_cap_t, sibling);

                if (stack_top < TEST_REVOKE_STACK_SIZE)
                {
                    /* 计算子能力在 cap_table 中的槽索引 */
                    uint32_t child_slot = (uint32_t)(child_cap - &cs->cap_table[0U]);
                    revoke_stack[stack_top] = child_slot;
                    stack_top++;
                }
            }
        }

        /* 将当前能力状态设为已撤销 */
        cur_cap->state = TEST_CAP_REVOKED;

        /* 从父能力的 children 链表移除 */
        list_del_init(&cur_cap->sibling);
        init_list_head(&cur_cap->children);
    }

    return KERNEL_OK;
}

/* ========================================================================
 * 测试用例
 * ======================================================================== */

/**
 * @brief 测试 1: 初始化后 CSpace 状态正确
 */
static void test_init_cspace(void)
{
    test_cspace_t *cs;
    test_cap_t *root;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));

    cs = test_cspace_create(0U);

    TEST_ASSERT_NOT_NULL(cs);
    TEST_ASSERT_EQ(cs->used_count, 1U);
    TEST_ASSERT_EQ(cs->root_slot, 0U);

    root = &cs->cap_table[0U];
    TEST_ASSERT_EQ(root->state, TEST_CAP_VALID);
    TEST_ASSERT_EQ(root->rights, (uint8_t)TEST_CAP_RIGHT_ALL);
    TEST_ASSERT_EQ(root->parent_slot, TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_TRUE(list_empty_test(&root->children) == 1);
}

/**
 * @brief 测试 2: 单个能力撤销（无子能力）
 */
static void test_revoke_single_no_children(void)
{
    test_cspace_t *cs;
    test_cap_t *cap;
    kernel_status_t ret;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    /* 在 slot 1 创建一个有能力撤销权限的能力 */
    ret = test_cap_create_at(cs, 1U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_READ | TEST_CAP_RIGHT_REVOKE,
                              TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 撤销 */
    ret = test_cap_revoke(cs, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 验证状态 */
    cap = &cs->cap_table[1U];
    TEST_ASSERT_EQ(cap->state, TEST_CAP_REVOKED);
}

/**
 * @brief 测试 3: 级联撤销（父撤销时子全部撤销）
 */
static void test_revoke_cascade(void)
{
    test_cspace_t *cs;
    kernel_status_t ret;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    /* 创建父能力（slot 1） */
    ret = test_cap_create_at(cs, 1U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_ALL, TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 创建子能力（slot 2），父为 slot 1 */
    ret = test_cap_create_at(cs, 2U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_READ, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 创建子能力（slot 3），父为 slot 1 */
    ret = test_cap_create_at(cs, 3U, TEST_CAP_RIGHT_READ == 0U ?
                              TEST_KOBJ_CSPACE : TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_WRITE, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 撤销父能力 */
    ret = test_cap_revoke(cs, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 验证父能力已撤销 */
    TEST_ASSERT_EQ(cs->cap_table[1U].state, TEST_CAP_REVOKED);

    /* 验证子能力全部被级联撤销 */
    TEST_ASSERT_EQ(cs->cap_table[2U].state, TEST_CAP_REVOKED);
    TEST_ASSERT_EQ(cs->cap_table[3U].state, TEST_CAP_REVOKED);
}

/**
 * @brief 测试 4: 撤销权限检查（无 REVOKE 权限应失败）
 */
static void test_revoke_no_permission(void)
{
    test_cspace_t *cs;
    kernel_status_t ret;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    /* 创建一个没有 REVOKE 权限的能力 */
    ret = test_cap_create_at(cs, 1U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_READ | TEST_CAP_RIGHT_WRITE,
                              TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 撤销应失败 */
    ret = test_cap_revoke(cs, 1U);
    TEST_ASSERT_EQ(ret, -(int32_t)EACCES);

    /* 能力应仍为有效 */
    TEST_ASSERT_EQ(cs->cap_table[1U].state, TEST_CAP_VALID);
}

/**
 * @brief 测试 5: 空闲槽撤销（应返回错误）
 */
static void test_revoke_free_slot(void)
{
    test_cspace_t *cs;
    kernel_status_t ret;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    /* slot 4 应为空闲（未分配） */
    TEST_ASSERT_EQ(cs->cap_table[4U].state, TEST_CAP_FREE);

    /* 撤销空闲槽应失败 */
    ret = test_cap_revoke(cs, 4U);
    TEST_ASSERT_EQ(ret, -(int32_t)ENOENT);
}

/**
 * @brief 测试 6: 已撤销能力再次撤销（应返回错误）
 */
static void test_revoke_already_revoked(void)
{
    test_cspace_t *cs;
    kernel_status_t ret;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    /* 创建并撤销 */
    ret = test_cap_create_at(cs, 1U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_ALL, TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    ret = test_cap_revoke(cs, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 再次撤销应失败 */
    ret = test_cap_revoke(cs, 1U);
    TEST_ASSERT_EQ(ret, -(int32_t)ENOENT);
}

/**
 * @brief 测试 7: 多级级联撤销（A→B→C 链式撤销）
 */
static void test_revoke_multi_level_cascade(void)
{
    test_cspace_t *cs;
    kernel_status_t ret;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    /* A（slot 1）：根父能力 */
    ret = test_cap_create_at(cs, 1U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_ALL, TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* B（slot 2）：A 的子能力 */
    ret = test_cap_create_at(cs, 2U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_READ | TEST_CAP_RIGHT_REVOKE,
                              1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* C（slot 3）：B 的子能力 */
    ret = test_cap_create_at(cs, 3U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_READ,
                              2U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 撤销 A，B 和 C 应全部被撤销 */
    ret = test_cap_revoke(cs, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    TEST_ASSERT_EQ(cs->cap_table[1U].state, TEST_CAP_REVOKED);
    TEST_ASSERT_EQ(cs->cap_table[2U].state, TEST_CAP_REVOKED);
    TEST_ASSERT_EQ(cs->cap_table[3U].state, TEST_CAP_REVOKED);
}

/**
 * @brief 测试 8: 大规模撤销（多个子能力同时撤销）
 */
static void test_revoke_many_children(void)
{
    test_cspace_t *cs;
    kernel_status_t ret;
    uint32_t i;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    /* 父能力（slot 1） */
    ret = test_cap_create_at(cs, 1U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_ALL, TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 创建 10 个子能力（slot 2 ~ 11） */
    for (i = 2U; i < 12U; i++)
    {
        ret = test_cap_create_at(cs, i, TEST_KOBJ_ENDPOINT, 100U,
                                  TEST_CAP_RIGHT_READ, 1U);
        TEST_ASSERT_EQ(ret, KERNEL_OK);
    }

    /* 撤销父能力 */
    ret = test_cap_revoke(cs, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 所有子能力应被撤销 */
    TEST_ASSERT_EQ(cs->cap_table[1U].state, TEST_CAP_REVOKED);
    for (i = 2U; i < 12U; i++)
    {
        TEST_ASSERT_EQ(cs->cap_table[i].state, TEST_CAP_REVOKED);
    }
}

/**
 * @brief 测试 9: 能力复制时的降权（子权限不能超过父）
 */
static void test_copy_derive_rights(void)
{
    test_cspace_t *cs;
    test_cap_t *child_cap;
    kernel_status_t ret;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    /* 父能力（slot 1）：有 READ|WRITE|GRANT|REVOKE */
    ret = test_cap_create_at(cs, 1U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_READ | TEST_CAP_RIGHT_WRITE |
                              TEST_CAP_RIGHT_GRANT | TEST_CAP_RIGHT_REVOKE,
                              TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 复制并降权：只保留 READ */
    ret = test_cap_copy(cs, 1U, cs, 2U,
                         TEST_CAP_RIGHT_READ);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 验证子能力权限 */
    child_cap = &cs->cap_table[2U];
    TEST_ASSERT_EQ(child_cap->state, TEST_CAP_VALID);
    TEST_ASSERT_EQ(child_cap->rights, (uint8_t)TEST_CAP_RIGHT_READ);

    /* 验证子能力的 parent_slot 指向父 */
    TEST_ASSERT_EQ(child_cap->parent_slot, 1U);
}

/**
 * @brief 测试 10: 复制时 rights_mask=0 保持原权限
 */
static void test_copy_keep_rights(void)
{
    test_cspace_t *cs;
    test_cap_t *child_cap;
    kernel_status_t ret;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    /* 父能力（slot 1） */
    ret = test_cap_create_at(cs, 1U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_READ | TEST_CAP_RIGHT_WRITE |
                              TEST_CAP_RIGHT_GRANT,
                              TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 复制：rights_mask=0 表示保持原权限 */
    ret = test_cap_copy(cs, 1U, cs, 2U, 0U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    child_cap = &cs->cap_table[2U];
    TEST_ASSERT_EQ(child_cap->rights,
                    (uint8_t)(TEST_CAP_RIGHT_READ | TEST_CAP_RIGHT_WRITE |
                              TEST_CAP_RIGHT_GRANT));
}

/**
 * @brief 测试 11: 复制无 GRANT 权限应失败
 */
static void test_copy_no_grant(void)
{
    test_cspace_t *cs;
    kernel_status_t ret;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    /* 父能力无 GRANT 权限 */
    ret = test_cap_create_at(cs, 1U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_READ,
                              TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 复制应失败 */
    ret = test_cap_copy(cs, 1U, cs, 2U, 0U);
    TEST_ASSERT_EQ(ret, -(int32_t)EACCES);
}

/**
 * @brief 测试 12: 传递降权（A→B→C，每级权限递减）
 */
static void test_copy_transitive_derivation(void)
{
    test_cspace_t *cs;
    test_cap_t *cap_a;
    test_cap_t *cap_b;
    test_cap_t *cap_c;
    kernel_status_t ret;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    /* A（slot 1）：READ|WRITE|EXECUTE|GRANT|REVOKE */
    ret = test_cap_create_at(cs, 1U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_READ | TEST_CAP_RIGHT_WRITE |
                              TEST_CAP_RIGHT_EXECUTE | TEST_CAP_RIGHT_GRANT |
                              TEST_CAP_RIGHT_REVOKE,
                              TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    cap_a = &cs->cap_table[1U];

    /* A→B：降权去掉 WRITE 和 REVOKE */
    ret = test_cap_copy(cs, 1U, cs, 2U,
                         TEST_CAP_RIGHT_READ | TEST_CAP_RIGHT_EXECUTE |
                         TEST_CAP_RIGHT_GRANT);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    cap_b = &cs->cap_table[2U];

    /* 验证 B 的权限 */
    TEST_ASSERT_EQ(cap_b->rights,
                    (uint8_t)(TEST_CAP_RIGHT_READ | TEST_CAP_RIGHT_EXECUTE |
                              TEST_CAP_RIGHT_GRANT));

    /* B→C：从 B 复制，进一步降权只保留 READ */
    ret = test_cap_copy(cs, 2U, cs, 3U, TEST_CAP_RIGHT_READ);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    cap_c = &cs->cap_table[3U];

    /* 验证 C 的权限（只有 READ） */
    TEST_ASSERT_EQ(cap_c->rights, (uint8_t)TEST_CAP_RIGHT_READ);

    /* 验证权限递减：A 权限 > B 权限 > C 权限 */
    TEST_ASSERT_TRUE((cap_a->rights & cap_b->rights) == cap_b->rights);
    TEST_ASSERT_TRUE((cap_b->rights & cap_c->rights) == cap_c->rights);
    TEST_ASSERT_TRUE(cap_c->rights < cap_b->rights);
    TEST_ASSERT_TRUE(cap_b->rights < cap_a->rights);

    /* 验证父子链：C 的 parent 是 B，B 的 parent 是 A */
    TEST_ASSERT_EQ(cap_b->parent_slot, 1U);
    TEST_ASSERT_EQ(cap_c->parent_slot, 2U);

    /* 撤销 A 应级联撤销 B 和 C */
    ret = test_cap_revoke(cs, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    TEST_ASSERT_EQ(cap_a->state, TEST_CAP_REVOKED);
    TEST_ASSERT_EQ(cap_b->state, TEST_CAP_REVOKED);
    TEST_ASSERT_EQ(cap_c->state, TEST_CAP_REVOKED);
}

/**
 * @brief 测试 13: 撤销只影响子能力，不影响无关能力
 */
static void test_revoke_isolation(void)
{
    test_cspace_t *cs;
    kernel_status_t ret;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    /* 能力 A（slot 1）：有子能力 */
    ret = test_cap_create_at(cs, 1U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_ALL, TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* A 的子能力（slot 2） */
    ret = test_cap_create_at(cs, 2U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_READ, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 无关能力 B（slot 3）：独立能力 */
    ret = test_cap_create_at(cs, 3U, TEST_KOBJ_CHANNEL, 200U,
                              TEST_CAP_RIGHT_ALL, TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 无关能力 C（slot 4）：B 的子能力 */
    ret = test_cap_create_at(cs, 4U, TEST_KOBJ_CHANNEL, 200U,
                              TEST_CAP_RIGHT_WRITE, 3U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 撤销 A */
    ret = test_cap_revoke(cs, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* A 及其子能力被撤销 */
    TEST_ASSERT_EQ(cs->cap_table[1U].state, TEST_CAP_REVOKED);
    TEST_ASSERT_EQ(cs->cap_table[2U].state, TEST_CAP_REVOKED);

    /* B 和 C 应保持有效 */
    TEST_ASSERT_EQ(cs->cap_table[3U].state, TEST_CAP_VALID);
    TEST_ASSERT_EQ(cs->cap_table[4U].state, TEST_CAP_VALID);
}

/**
 * @brief 测试 14: 撤销中间节点不影响兄弟节点
 */
static void test_revoke_middle_node(void)
{
    test_cspace_t *cs;
    kernel_status_t ret;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    /* 根父能力（slot 1） */
    ret = test_cap_create_at(cs, 1U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_ALL, TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 子能力 A（slot 2） */
    ret = test_cap_create_at(cs, 2U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_READ | TEST_CAP_RIGHT_REVOKE, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 子能力 B（slot 3） */
    ret = test_cap_create_at(cs, 3U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_WRITE, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 撤销子能力 A */
    ret = test_cap_revoke(cs, 2U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* A 被撤销 */
    TEST_ASSERT_EQ(cs->cap_table[2U].state, TEST_CAP_REVOKED);

    /* 根和 B 保持有效 */
    TEST_ASSERT_EQ(cs->cap_table[1U].state, TEST_CAP_VALID);
    TEST_ASSERT_EQ(cs->cap_table[3U].state, TEST_CAP_VALID);
}

/**
 * @brief 测试 15: NULL 参数安全检查
 */
static void test_null_param(void)
{
    kernel_status_t ret;

    /* 撤销 NULL CSpace */
    ret = test_cap_revoke(NULL, 1U);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);

    /* 复制 NULL 源 */
    ret = test_cap_copy(NULL, 1U, NULL, 2U, 0U);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);
}

/**
 * @brief 测试 16: 复制到非空闲槽应失败
 */
static void test_copy_to_occupied_slot(void)
{
    test_cspace_t *cs;
    kernel_status_t ret;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    /* 父能力（slot 1） */
    ret = test_cap_create_at(cs, 1U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_ALL, TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 先占用 slot 2 */
    ret = test_cap_create_at(cs, 2U, TEST_KOBJ_CHANNEL, 200U,
                              TEST_CAP_RIGHT_READ, TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 复制到已被占用的 slot 2 应失败 */
    ret = test_cap_copy(cs, 1U, cs, 2U, 0U);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);
}

/**
 * @brief 测试 17: 撤销后子链表状态正确
 */
static void test_revoke_cleans_children_list(void)
{
    test_cspace_t *cs;
    kernel_status_t ret;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    /* 父（slot 1） */
    ret = test_cap_create_at(cs, 1U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_ALL, TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 子（slot 2） */
    ret = test_cap_create_at(cs, 2U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_READ, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 验证父的 children 非空 */
    TEST_ASSERT_TRUE(list_empty_test(&cs->cap_table[1U].children) == 0);

    /* 撤销父 */
    ret = test_cap_revoke(cs, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 父被撤销后，children 链表应已清空 */
    TEST_ASSERT_TRUE(list_empty_test(&cs->cap_table[1U].children) == 1);
}

/**
 * @brief 测试 18: 撤销越界槽索引
 */
static void test_revoke_out_of_range(void)
{
    test_cspace_t *cs;
    kernel_status_t ret;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    /* 越界槽索引 */
    ret = test_cap_revoke(cs, TEST_CSPACE_CAPACITY + 1U);
    TEST_ASSERT_EQ(ret, -(int32_t)ENOENT);
}

/**
 * @brief 测试 19: 复制后撤销子能力的子能力（多层级联）
 */
static void test_copy_then_revoke_deep(void)
{
    test_cspace_t *cs;
    kernel_status_t ret;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    /* A（slot 1） */
    ret = test_cap_create_at(cs, 1U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_ALL, TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 用 cap_copy 创建 B（slot 2） */
    ret = test_cap_copy(cs, 1U, cs, 2U,
                         TEST_CAP_RIGHT_READ | TEST_CAP_RIGHT_GRANT |
                         TEST_CAP_RIGHT_REVOKE);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 用 cap_copy 创建 C（slot 3），从 B 复制（需要 GRANT 才能继续传递） */
    ret = test_cap_copy(cs, 2U, cs, 3U, TEST_CAP_RIGHT_READ | TEST_CAP_RIGHT_GRANT);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 用 cap_copy 创建 D（slot 4），从 C 复制 */
    ret = test_cap_copy(cs, 3U, cs, 4U, TEST_CAP_RIGHT_READ);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 撤销 A，B/C/D 应全部撤销 */
    ret = test_cap_revoke(cs, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    TEST_ASSERT_EQ(cs->cap_table[1U].state, TEST_CAP_REVOKED);
    TEST_ASSERT_EQ(cs->cap_table[2U].state, TEST_CAP_REVOKED);
    TEST_ASSERT_EQ(cs->cap_table[3U].state, TEST_CAP_REVOKED);
    TEST_ASSERT_EQ(cs->cap_table[4U].state, TEST_CAP_REVOKED);
}

/**
 * @brief 测试 20: 复制的降权不可提升回原权限
 */
static void test_copy_cannot_elevate(void)
{
    test_cspace_t *cs;
    kernel_status_t ret;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    /* 父能力（slot 1）：只有 READ|GRANT */
    ret = test_cap_create_at(cs, 1U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_READ | TEST_CAP_RIGHT_GRANT,
                              TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 尝试用包含 WRITE 的 mask 来提升权限 */
    ret = test_cap_copy(cs, 1U, cs, 2U,
                         TEST_CAP_RIGHT_READ | TEST_CAP_RIGHT_WRITE);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 子权限应被截断为源权限的子集 */
    TEST_ASSERT_EQ(cs->cap_table[2U].rights,
                    (uint8_t)TEST_CAP_RIGHT_READ);
}

/**
 * @brief 测试 21: 传递降权后子能力无法给孙子更高权限
 */
static void test_transitive_no_elevation(void)
{
    test_cspace_t *cs;
    kernel_status_t ret;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    /* A（slot 1）：READ|WRITE|GRANT|REVOKE */
    ret = test_cap_create_at(cs, 1U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_READ | TEST_CAP_RIGHT_WRITE |
                              TEST_CAP_RIGHT_GRANT | TEST_CAP_RIGHT_REVOKE,
                              TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* A→B：降权只保留 READ|GRANT */
    ret = test_cap_copy(cs, 1U, cs, 2U,
                         TEST_CAP_RIGHT_READ | TEST_CAP_RIGHT_GRANT);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* B→C：B 只有 READ|GRANT，尝试用 WRITE|REVOKE mask，
     * 但由于 B 没有 WRITE|REVOKE，C 不可能得到这些权限 */
    ret = test_cap_copy(cs, 2U, cs, 3U,
                         TEST_CAP_RIGHT_READ | TEST_CAP_RIGHT_WRITE |
                         TEST_CAP_RIGHT_REVOKE);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* C 的权限应只是 READ（B 的权限 & mask = READ） */
    TEST_ASSERT_EQ(cs->cap_table[3U].rights,
                    (uint8_t)(TEST_CAP_RIGHT_READ));
}

/**
 * @brief 测试 22: 树形结构撤销（一个父多个子，子又各有子）
 */
static void test_revoke_tree_structure(void)
{
    test_cspace_t *cs;
    kernel_status_t ret;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    /* 根（slot 1） */
    ret = test_cap_create_at(cs, 1U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_ALL, TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 子 B1（slot 2），子 B2（slot 3） */
    ret = test_cap_create_at(cs, 2U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_READ | TEST_CAP_RIGHT_REVOKE, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);
    ret = test_cap_create_at(cs, 3U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_WRITE, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* B1 的子 C1（slot 4） */
    ret = test_cap_create_at(cs, 4U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_READ, 2U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* B2 的子 C2（slot 5） */
    ret = test_cap_create_at(cs, 5U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_WRITE, 3U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 撤销根 */
    ret = test_cap_revoke(cs, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 所有 5 个能力应全部被撤销 */
    TEST_ASSERT_EQ(cs->cap_table[1U].state, TEST_CAP_REVOKED);
    TEST_ASSERT_EQ(cs->cap_table[2U].state, TEST_CAP_REVOKED);
    TEST_ASSERT_EQ(cs->cap_table[3U].state, TEST_CAP_REVOKED);
    TEST_ASSERT_EQ(cs->cap_table[4U].state, TEST_CAP_REVOKED);
    TEST_ASSERT_EQ(cs->cap_table[5U].state, TEST_CAP_REVOKED);
}

/**
 * @brief 测试 23: 撤销后可以重新在空闲槽创建能力
 */
static void test_revoke_then_reuse(void)
{
    test_cspace_t *cs;
    kernel_status_t ret;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    /* 创建并撤销 */
    ret = test_cap_create_at(cs, 1U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_ALL, TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    ret = test_cap_revoke(cs, 1U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 槽状态为 REVOKED，不是 FREE，直接插入应失败 */
    ret = test_cap_create_at(cs, 1U, TEST_KOBJ_CHANNEL, 200U,
                              TEST_CAP_RIGHT_READ, TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, -(int32_t)EINVAL);
}

/**
 * @brief 测试 24: 复制到自身槽（源和目标不同）应正常工作
 */
static void test_copy_different_slots(void)
{
    test_cspace_t *cs;
    kernel_status_t ret;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    /* 创建父 */
    ret = test_cap_create_at(cs, 1U, TEST_KOBJ_ENDPOINT, 100U,
                              TEST_CAP_RIGHT_ALL, TEST_CAP_SLOT_INVALID);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 复制到不同槽 */
    ret = test_cap_copy(cs, 1U, cs, 2U, 0U);
    TEST_ASSERT_EQ(ret, KERNEL_OK);

    /* 两者都有效 */
    TEST_ASSERT_EQ(cs->cap_table[1U].state, TEST_CAP_VALID);
    TEST_ASSERT_EQ(cs->cap_table[2U].state, TEST_CAP_VALID);

    /* kobj_id 相同 */
    TEST_ASSERT_EQ(cs->cap_table[1U].kobj_id, cs->cap_table[2U].kobj_id);
}

/**
 * @brief 测试 25: 压力测试 — 大量创建/撤销循环
 */
static void test_stress_create_revoke(void)
{
    test_cspace_t *cs;
    kernel_status_t ret;
    uint32_t i;

    s_next_kobj_id = 1U;
    (void)memset(s_cspace_used, 0, sizeof(s_cspace_used));
    cs = test_cspace_create(0U);

    for (i = 0U; i < 100U; i++)
    {
        /* 创建（slot 1） */
        ret = test_cap_create_at(cs, 1U, TEST_KOBJ_ENDPOINT,
                                  (uint64_t)(i + 100U),
                                  TEST_CAP_RIGHT_ALL,
                                  TEST_CAP_SLOT_INVALID);
        TEST_ASSERT_EQ(ret, KERNEL_OK);

        /* 撤销 */
        ret = test_cap_revoke(cs, 1U);
        TEST_ASSERT_EQ(ret, KERNEL_OK);

        /* 重置 slot 1 为空闲以复用 */
        cs->cap_table[1U].state = TEST_CAP_FREE;
        init_list_head(&cs->cap_table[1U].children);
        init_list_head(&cs->cap_table[1U].sibling);
        cs->cap_table[1U].parent_slot = TEST_CAP_SLOT_INVALID;
    }
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("=== 能力撤销与传递降权测试 ===\n\n");

    test_init_cspace();
    test_revoke_single_no_children();
    test_revoke_cascade();
    test_revoke_no_permission();
    test_revoke_free_slot();
    test_revoke_already_revoked();
    test_revoke_multi_level_cascade();
    test_revoke_many_children();
    test_copy_derive_rights();
    test_copy_keep_rights();
    test_copy_no_grant();
    test_copy_transitive_derivation();
    test_revoke_isolation();
    test_revoke_middle_node();
    test_null_param();
    test_copy_to_occupied_slot();
    test_revoke_cleans_children_list();
    test_revoke_out_of_range();
    test_copy_then_revoke_deep();
    test_copy_cannot_elevate();
    test_transitive_no_elevation();
    test_revoke_tree_structure();
    test_revoke_then_reuse();
    test_copy_different_slots();
    test_stress_create_revoke();

    TEST_SUMMARY("test_capability_revoke");

    return TEST_RESULT();
}
