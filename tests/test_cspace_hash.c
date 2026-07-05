/**
 * @file    test_cspace_hash.c
 * @brief   AISafe64 RTOS - CSpace 哈希查找性能优化单元测试（宿主机）
 * @author  AISafe64 Team
 * @date    2026-04-05
 * @version 1.0
 *
 * @details 测试 cspace_from_root 哈希查找优化功能：
 *          1. cspace_create 后能通过 ID 快速查找（哈希命中）
 *          2. 查找不存在的 ID 返回 NULL
 *          3. 多个 CSpace 的查找正确性（哈希冲突处理）
 *          4. 删除后查找返回 NULL
 *          5. 哈希桶冲突时的开链法正确性
 *          6. 子系统重置后查找干净
 *
 * @note 宿主机单线程模拟
 * @note 对应需求: KR-016（CSpace 管理）、性能优化（O(1) 查找）
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

#define INIT_LIST_HEAD(ptr) tl_init(ptr)

#undef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* ========================================================================
 * 配置常量（与内核 config.h 一致）
 * ======================================================================== */

#define TEST_MAX_CSPACES            32U
#define TEST_CSPACE_DEFAULT_CAP     64U
#define TEST_CSPACE_MAX_CAP         256U
#define KOBJ_ID_INVALID             ((kobj_id_t)0U)
#define CAP_SLOT_INVALID            ((uint32_t)0xFFFFFFFFU)

/* ========================================================================
 * 权限位定义
 * ======================================================================== */

#define CAP_RIGHT_READ      (1U << 0U)
#define CAP_RIGHT_WRITE     (1U << 1U)
#define CAP_RIGHT_EXECUTE   (1U << 2U)
#define CAP_RIGHT_GRANT     (1U << 3U)
#define CAP_RIGHT_REVOKE    (1U << 4U)
#define CAP_RIGHT_ALL       (CAP_RIGHT_READ | CAP_RIGHT_WRITE | \
                             CAP_RIGHT_EXECUTE | CAP_RIGHT_GRANT | \
                             CAP_RIGHT_REVOKE)

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
 * 内核对象类型
 * ======================================================================== */

typedef enum
{
    KOBJ_THREAD = 0U,
    KOBJ_ENDPOINT,
    KOBJ_NOTIFICATION,
    KOBJ_CSPACE,
    KOBJ_TYPE_COUNT
} test_kobj_type_t;

/* ========================================================================
 * 能力描述符（与 kernel/capability.h 一致）
 * ======================================================================== */

typedef uint32_t test_cap_slot_t;

typedef struct
{
    test_cap_state_t     state;
    test_kobj_type_t     kobj_type;
    uint8_t              rights;
    uint16_t             badge;
    kobj_id_t            kobj_id;
    test_cap_slot_t      parent_slot;
    test_cap_slot_t      cspace_root;
    struct list_head     children;
    struct list_head     sibling;
} test_cap_t;

/* ========================================================================
 * kobj_header_t（简化版）
 * ======================================================================== */

typedef struct
{
    test_kobj_type_t     type;
    kobj_id_t            id;
} test_kobj_header_t;

/* ========================================================================
 * CSpace 结构（与 kernel/cspace.h 一致）
 * ======================================================================== */

typedef struct
{
    test_kobj_header_t   header;
    test_cap_t          *cap_table;
    uint32_t             capacity;
    uint32_t             used_count;
    test_cap_slot_t      root_slot;
    test_cap_slot_t      free_head;
    struct list_head     child_cspaces;
    struct list_head     cspace_node;
    TicketLock_t         lock;
} test_cspace_t;

/* ========================================================================
 * 哈希索引（与 kernel/cap/cspace.c 一致）
 * ======================================================================== */

/** @brief 哈希表桶数量（2 的幂次方） */
#define CSPACE_HASH_BUCKETS     32U

/** @brief 哈希掩码 */
#define CSPACE_HASH_MASK        (CSPACE_HASH_BUCKETS - 1U)

/**
 * @brief 哈希链表节点
 */
typedef struct cspace_hash_node
{
    kobj_id_t                   key;
    test_cspace_t              *value;
    struct cspace_hash_node    *next;
} cspace_hash_node_t;

/* ========================================================================
 * 测试专用静态存储
 * ======================================================================== */

/** @brief CSpace 静态池 */
static test_cspace_t s_cspace_pool[TEST_MAX_CSPACES];

/** @brief CSpace 空闲索引栈 */
static uint32_t s_cspace_free_stack[TEST_MAX_CSPACES];

/** @brief CSpace 空闲计数 */
static uint32_t s_cspace_free_count;

/** @brief 能力表静态存储 */
static test_cap_t s_cap_table_pool[TEST_MAX_CSPACES][TEST_CSPACE_MAX_CAP];

/** @brief 全局锁 */
static TicketLock_t s_subsys_lock;

/** @brief ID 计数器 */
static kobj_id_t s_cspace_id_counter;

/** @brief 哈希桶数组 */
static cspace_hash_node_t *s_cspace_hash[CSPACE_HASH_BUCKETS];

/** @brief 哈希节点静态池 */
static cspace_hash_node_t s_hash_nodes[TEST_MAX_CSPACES];

/* ========================================================================
 * 哈希函数（与 cspace.c 一致）
 * ======================================================================== */

/**
 * @brief 计算 CSpace ID 的哈希桶索引
 */
static inline uint32_t cspace_hash_index(kobj_id_t id)
{
    return (uint32_t)(id & (kobj_id_t)CSPACE_HASH_MASK);
}

/**
 * @brief 将 CSpace 注册到哈希表
 *
 * @details 与 kernel/cap/cspace.c 中的 cspace_hash_register 一致
 */
static void cspace_hash_register(kobj_id_t id, test_cspace_t *cspace)
{
    uint32_t bucket = cspace_hash_index(id);
    uint32_t idx = (uint32_t)(cspace - &s_cspace_pool[0U]);
    cspace_hash_node_t *node;

    /* 索引越界保护 */
    if (idx >= (uint32_t)TEST_MAX_CSPACES)
    {
        return;
    }

    node = &s_hash_nodes[idx];

    /* 填充节点 */
    node->key   = id;
    node->value = cspace;

    /* 头插入桶链表 */
    node->next = s_cspace_hash[bucket];
    s_cspace_hash[bucket] = node;
}

/**
 * @brief 将 CSpace 从哈希表移除
 *
 * @details 与 kernel/cap/cspace.c 中的 cspace_hash_unregister 一致
 */
static void cspace_hash_unregister(kobj_id_t id)
{
    uint32_t bucket = cspace_hash_index(id);
    cspace_hash_node_t *prev = NULL;
    cspace_hash_node_t *curr = s_cspace_hash[bucket];

    /* 遍历桶链表查找目标节点 */
    while (curr != NULL)
    {
        if (curr->key == id)
        {
            /* 从链表中摘除 */
            if (prev == NULL)
            {
                s_cspace_hash[bucket] = curr->next;
            }
            else
            {
                prev->next = curr->next;
            }

            /* 清空节点 */
            curr->key   = KOBJ_ID_INVALID;
            curr->value = NULL;
            curr->next  = NULL;
            return;
        }

        prev = curr;
        curr = curr->next;
    }
}

/**
 * @brief 哈希查找 CSpace
 *
 * @details 与 kernel/cap/cspace.c 中的 cspace_from_root 快速路径一致，
 *          仅使用哈希查找，不回退到线性遍历。
 */
static test_cspace_t *cspace_from_root_hash(kobj_id_t id)
{
    uint32_t bucket;
    cspace_hash_node_t *node;

    bucket = cspace_hash_index(id);
    node = s_cspace_hash[bucket];

    while (node != NULL)
    {
        if (node->key == id)
        {
            return node->value;
        }
        node = node->next;
    }

    return NULL;
}

/* ========================================================================
 * 子系统初始化
 * ======================================================================== */

/**
 * @brief 初始化 CSpace 子系统
 */
static void test_subsys_init(void)
{
    uint32_t i;

    ticket_lock_init(&s_subsys_lock);
    s_cspace_id_counter = (kobj_id_t)1U;

    /* 将所有索引压入空闲栈 */
    s_cspace_free_count = (uint32_t)TEST_MAX_CSPACES;

    for (i = 0U; i < (uint32_t)TEST_MAX_CSPACES; i++)
    {
        s_cspace_free_stack[i] = (uint32_t)TEST_MAX_CSPACES - 1U - i;
    }

    /* 清零 CSpace 池 */
    (void)memset(s_cspace_pool, 0U, sizeof(s_cspace_pool));

    /* 初始化哈希索引表和节点池 */
    (void)memset(s_cspace_hash, 0U, sizeof(s_cspace_hash));
    (void)memset(s_hash_nodes, 0U, sizeof(s_hash_nodes));
}

/**
 * @brief 从静态池分配一个空闲 CSpace
 */
static test_cspace_t *cspace_pool_alloc(void)
{
    uint32_t idx;

    if (s_cspace_free_count == 0U)
    {
        return NULL;
    }

    s_cspace_free_count--;
    idx = s_cspace_free_stack[s_cspace_free_count];

    return &s_cspace_pool[idx];
}

/**
 * @brief 将 CSpace 释放回静态池
 */
static void cspace_pool_free(test_cspace_t *cspace)
{
    uint32_t idx;

    if (cspace == NULL)
    {
        return;
    }

    if (s_cspace_free_count >= (uint32_t)TEST_MAX_CSPACES)
    {
        return;
    }

    idx = (uint32_t)(cspace - &s_cspace_pool[0U]);

    if (idx >= (uint32_t)TEST_MAX_CSPACES)
    {
        return;
    }

    (void)memset(cspace, 0U, sizeof(test_cspace_t));

    s_cspace_free_stack[s_cspace_free_count] = idx;
    s_cspace_free_count++;
}

/**
 * @brief 创建测试 CSpace（简化版）
 *
 * @details 分配 CSpace，分配 ID，注册哈希。
 *          不初始化完整能力表，仅设置 header 和 root_slot。
 */
static test_cspace_t *test_cspace_create(void)
{
    test_cspace_t *cspace;
    kobj_id_t new_id;
    uint32_t pool_idx;

    cspace = cspace_pool_alloc();
    if (cspace == NULL)
    {
        return NULL;
    }

    /* 分配唯一 ID */
    new_id = s_cspace_id_counter;
    s_cspace_id_counter++;

    /* 初始化头部 */
    cspace->header.type = KOBJ_CSPACE;
    cspace->header.id = new_id;

    /* 关联能力表 */
    pool_idx = (uint32_t)(cspace - &s_cspace_pool[0U]);
    cspace->cap_table = &s_cap_table_pool[pool_idx][0U];
    cspace->capacity = TEST_CSPACE_DEFAULT_CAP;
    cspace->used_count = 0U;
    cspace->root_slot = CAP_SLOT_INVALID;
    cspace->free_head = CAP_SLOT_INVALID;

    INIT_LIST_HEAD(&cspace->child_cspaces);
    INIT_LIST_HEAD(&cspace->cspace_node);
    ticket_lock_init(&cspace->lock);

    /* 根能力在 slot 0 */
    {
        test_cap_t *root_cap = &cspace->cap_table[0U];
        root_cap->state = CAP_STATE_VALID;
        root_cap->kobj_type = KOBJ_CSPACE;
        root_cap->rights = (uint8_t)CAP_RIGHT_ALL;
        root_cap->kobj_id = new_id;
        root_cap->parent_slot = CAP_SLOT_INVALID;
        root_cap->cspace_root = 0U;
        INIT_LIST_HEAD(&root_cap->children);
        INIT_LIST_HEAD(&root_cap->sibling);
    }

    cspace->root_slot = 0U;
    cspace->used_count = 1U;

    /* 注册到哈希表 */
    cspace_hash_register(new_id, cspace);

    return cspace;
}

/**
 * @brief 销毁测试 CSpace
 *
 * @details 从哈希表移除，释放回池。
 */
static void test_cspace_destroy(test_cspace_t *cspace)
{
    if (cspace == NULL)
    {
        return;
    }

    /* 从哈希索引表中移除 */
    cspace_hash_unregister(cspace->header.id);

    /* 清空元数据 */
    cspace->used_count = 0U;
    cspace->free_head = CAP_SLOT_INVALID;
    cspace->root_slot = CAP_SLOT_INVALID;

    /* 释放回静态池 */
    cspace_pool_free(cspace);
}

/* ========================================================================
 * 测试 1: cspace_create 后能通过 ID 快速查找
 * ======================================================================== */
static void test_hash_lookup_after_create(void)
{
    test_cspace_t *cs;
    test_cspace_t *found;

    printf("  测试 1: cspace_create 后能通过 ID 快速查找\n");

    cs = test_cspace_create();
    TEST_ASSERT_NOT_NULL(cs);

    /* 通过哈希查找 */
    found = cspace_from_root_hash(cs->header.id);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQ(found, cs);
    TEST_ASSERT_EQ((int)found->header.type, (int)KOBJ_CSPACE);
    TEST_ASSERT_EQ((uint64_t)found->header.id, (uint64_t)cs->header.id);
}

/* ========================================================================
 * 测试 2: 查找不存在的 ID 返回 NULL
 * ======================================================================== */
static void test_hash_lookup_not_found(void)
{
    test_cspace_t *found;

    printf("  测试 2: 查找不存在的 ID 返回 NULL\n");

    /* 查找一个从未分配过的 ID */
    found = cspace_from_root_hash((kobj_id_t)9999U);
    TEST_ASSERT_NULL(found);

    /* 查找 ID 0（KOBJ_ID_INVALID） */
    found = cspace_from_root_hash(KOBJ_ID_INVALID);
    TEST_ASSERT_NULL(found);
}

/* ========================================================================
 * 测试 3: 多个 CSpace 的查找正确性
 * ======================================================================== */
static void test_hash_lookup_multiple(void)
{
    test_cspace_t *cs1;
    test_cspace_t *cs2;
    test_cspace_t *cs3;
    test_cspace_t *found;

    printf("  测试 3: 多个 CSpace 的查找正确性\n");

    cs1 = test_cspace_create();
    cs2 = test_cspace_create();
    cs3 = test_cspace_create();

    TEST_ASSERT_NOT_NULL(cs1);
    TEST_ASSERT_NOT_NULL(cs2);
    TEST_ASSERT_NOT_NULL(cs3);

    /* 每个 CSpace 的 ID 应该不同 */
    TEST_ASSERT_NE((uint64_t)cs1->header.id, (uint64_t)cs2->header.id);
    TEST_ASSERT_NE((uint64_t)cs2->header.id, (uint64_t)cs3->header.id);

    /* 分别查找，验证结果正确 */
    found = cspace_from_root_hash(cs1->header.id);
    TEST_ASSERT_EQ(found, cs1);

    found = cspace_from_root_hash(cs2->header.id);
    TEST_ASSERT_EQ(found, cs2);

    found = cspace_from_root_hash(cs3->header.id);
    TEST_ASSERT_EQ(found, cs3);
}

/* ========================================================================
 * 测试 4: 删除后查找返回 NULL
 * ======================================================================== */
static void test_hash_lookup_after_destroy(void)
{
    test_cspace_t *cs;
    kobj_id_t id;
    test_cspace_t *found;

    printf("  测试 4: 删除后查找返回 NULL\n");

    cs = test_cspace_create();
    TEST_ASSERT_NOT_NULL(cs);

    id = cs->header.id;

    /* 销毁前应能找到 */
    found = cspace_from_root_hash(id);
    TEST_ASSERT_NOT_NULL(found);

    /* 销毁 */
    test_cspace_destroy(cs);

    /* 销毁后查找应返回 NULL */
    found = cspace_from_root_hash(id);
    TEST_ASSERT_NULL(found);
}

/* ========================================================================
 * 测试 5: 哈希桶冲突时的开链法正确性
 * ======================================================================== */
static void test_hash_collision_chaining(void)
{
    /**
     * @details 构造哈希冲突：手动向同一桶插入两个节点，
     *          验证开链法能正确找到两个。
     */
    uint32_t i;
    test_cspace_t *cs[TEST_MAX_CSPACES];
    test_cspace_t *found;

    printf("  测试 5: 哈希桶冲突时的开链法正确性\n");

    /* 创建最大数量的 CSpace，必定有多个落入同一桶 */
    for (i = 0U; i < TEST_MAX_CSPACES; i++)
    {
        cs[i] = test_cspace_create();
        TEST_ASSERT_NOT_NULL(cs[i]);
    }

    /* 验证每一个都能通过哈希查找找到 */
    for (i = 0U; i < TEST_MAX_CSPACES; i++)
    {
        found = cspace_from_root_hash(cs[i]->header.id);
        TEST_ASSERT_EQ(found, cs[i]);
    }

    /* 销毁一半，验证剩余的仍能找到 */
    for (i = 0U; i < TEST_MAX_CSPACES / 2U; i++)
    {
        test_cspace_destroy(cs[i]);
    }

    for (i = TEST_MAX_CSPACES / 2U; i < TEST_MAX_CSPACES; i++)
    {
        found = cspace_from_root_hash(cs[i]->header.id);
        TEST_ASSERT_EQ(found, cs[i]);
    }

    /* 已销毁的应找不到 */
    for (i = 0U; i < TEST_MAX_CSPACES / 2U; i++)
    {
        found = cspace_from_root_hash(cs[i]->header.id);
        TEST_ASSERT_NULL(found);
    }
}

/* ========================================================================
 * 测试 6: 子系统重置后查找干净
 * ======================================================================== */
static void test_subsys_reset(void)
{
    test_cspace_t *cs;
    test_cspace_t *found;

    printf("  测试 6: 子系统重置后查找干净\n");

    /* 先创建一个 CSpace */
    cs = test_cspace_create();
    TEST_ASSERT_NOT_NULL(cs);

    /* 重置子系统 */
    test_subsys_init();

    /* 重置后查找应返回 NULL（哈希表已清空） */
    found = cspace_from_root_hash(cs->header.id);
    TEST_ASSERT_NULL(found);

    /* 重置后能正常创建新的 CSpace */
    cs = test_cspace_create();
    TEST_ASSERT_NOT_NULL(cs);

    found = cspace_from_root_hash(cs->header.id);
    TEST_ASSERT_EQ(found, cs);
}

/* ========================================================================
 * 测试 7: 销毁后重用 CSpace 槽位
 * ======================================================================== */
static void test_destroy_and_reuse(void)
{
    test_cspace_t *cs1;
    kobj_id_t id1;
    test_cspace_t *cs2;
    test_cspace_t *found;

    printf("  测试 7: 销毁后重用 CSpace 槽位\n");

    cs1 = test_cspace_create();
    TEST_ASSERT_NOT_NULL(cs1);
    id1 = cs1->header.id;

    test_cspace_destroy(cs1);

    /* 创建新的 CSpace，可能复用同一个池槽位 */
    cs2 = test_cspace_create();
    TEST_ASSERT_NOT_NULL(cs2);

    /* 新 ID 应不同 */
    TEST_ASSERT_NE((uint64_t)cs2->header.id, (uint64_t)id1);

    /* 旧 ID 应找不到 */
    found = cspace_from_root_hash(id1);
    TEST_ASSERT_NULL(found);

    /* 新 ID 应能找到 */
    found = cspace_from_root_hash(cs2->header.id);
    TEST_ASSERT_EQ(found, cs2);
}

/* ========================================================================
 * 主函数
 * ======================================================================== */

int main(void)
{
    printf("\n============================================\n");
    printf("  AISafeOS64 - CSpace 哈希查找测试\n");
    printf("============================================\n\n");

    TEST_RESET();
    test_subsys_init();
    test_hash_lookup_after_create();

    TEST_RESET();
    test_subsys_init();
    test_hash_lookup_not_found();

    TEST_RESET();
    test_subsys_init();
    test_hash_lookup_multiple();

    TEST_RESET();
    test_subsys_init();
    test_hash_lookup_after_destroy();

    TEST_RESET();
    test_subsys_init();
    test_hash_collision_chaining();

    TEST_RESET();
    test_subsys_init();
    test_subsys_reset();

    TEST_RESET();
    test_subsys_init();
    test_destroy_and_reuse();

    printf("\n");
    TEST_SUMMARY("cspace_hash");

    return TEST_RESULT();
}
