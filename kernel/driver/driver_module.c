/**
 * @file    driver_module.c
 * @brief   驱动模块加载器实现
 * @author  AISafe64 Team
 * @date    2026-04-08
 * @version 1.0
 *
 * @details 本文件实现了 AISafeOS64 驱动模块的动态加载和卸载：
 *          - 模块镜像验证（magic、版本检查）
 *          - 内存段加载（text/data/bss）
 *          - 驱动注册与初始化
 *          - 模块卸载与资源释放
 *
 *          模块镜像格式：
 *          | module_header_t | name_str | text段 | data段 |
 *
 *          由于裸机环境无动态链接器，函数偏移基于模块基址计算。
 *
 * @note MISRA-C:2012 合规
 * @note kernel/ 非 arch/ 目录禁止使用 __asm__/msr/mrs/isb
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

/* ========== 头文件包含 ========== */

#include <kernel/driver.h>
#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <stdint.h>
#include <stddef.h>

/* ========== 外部函数声明 ========== */

extern void *kernel_memcpy(void *dest, const void *src, size_t n);
extern void *kernel_memset(void *s, int32_t c, size_t n);
extern void  kernel_memzero(void *s, size_t n);
extern size_t kernel_strlen(const char *s);
extern char *kernel_strcpy(char *dest, const char *src);
extern char *kernel_strncpy(char *dest, const char *src, size_t n);

/* ========== 内部常量 ========== */

/** @brief 模块当前支持的版本号 */
#define MODULE_VERSION_CURRENT  1U

/** @brief 模块头最小大小（至少包含 magic 和基本字段） */
#define MODULE_HEADER_MIN_SIZE  sizeof(module_header_t)

/** @brief 模块内存对齐要求（16 字节） */
#define MODULE_ALIGN            16U

/* ========== 模块运行时信息 ========== */

/**
 * @brief 已加载模块的运行时跟踪信息
 *
 * @details 记录模块加载后的内存布局，用于卸载时释放
 */
typedef struct
{
    uint32_t    drv_id;         /**< @brief 关联的驱动 ID */
    void        *base;          /**< @brief 模块内存基址 */
    uint32_t    total_size;     /**< @brief 总占用内存大小 */
    uint8_t     loaded;         /**< @brief 是否已加载 */
} module_info_t;

/** @brief 最大同时加载的模块数 */
#define MAX_LOADED_MODULES  8U

/** @brief 已加载模块跟踪表 */
static module_info_t s_modules[MAX_LOADED_MODULES];

/** @brief 模块子系统自旋锁 */
static TicketLock_t s_module_lock;

/* ========== 外部声明（driver_core.c 提供） ========== */

/**
 * @brief 内部查找驱动函数（不加锁版本）
 *
 * @note 此函数在 driver_core.c 中定义
 */
extern driver_desc_t *find_driver_by_id_internal(uint32_t drv_id);

/* ========== 内部函数声明 ========== */

static module_info_t *alloc_module_slot(void);
static module_info_t *find_module_by_drv_id(uint32_t drv_id);
static void *module_alloc(uint32_t size);
static void module_free(void *ptr);

/* ========== 模块子系统初始化 ========== */

/**
 * @brief 初始化模块加载器（由 driver_subsys_init 调用）
 *
 * @details 清零模块跟踪表并初始化锁
 *
 * @note 此函数在 driver_subsys_init 中被调用，
 *       不需要单独调用
 */
void driver_module_init(void);

/* ========================================================================
 * 模块加载/卸载 API 实现
 * ======================================================================== */

/**
 * @brief 加载驱动模块镜像
 *
 * @details 验证模块头，分配内存，加载段数据，注册驱动
 *
 * @param image 模块镜像数据指针
 * @param size  镜像大小（字节）
 *
 * @return >=0 驱动 ID，<0 错误码
 */
int64_t driver_module_load(const void *image, uint32_t size)
{
    const module_header_t *hdr;
    module_info_t *mod;
    void *base;
    uint32_t total_size;
    const char *name;
    const driver_ops_t *ops;
    kernel_status_t (*init_fn)(void);
    kernel_status_t ret;

    /* 参数检查 */
    if ((image == NULL) || (size < MODULE_HEADER_MIN_SIZE))
    {
        return -22; /* EINVAL */
    }

    /* 验证魔数 */
    hdr = (const module_header_t *)image;
    if (hdr->magic != MODULE_MAGIC)
    {
        return -8; /* ENOEXEC: 非法模块格式 */
    }

    /* 验证版本 */
    if (hdr->version != MODULE_VERSION_CURRENT)
    {
        return -8; /* ENOEXEC */
    }

    /* 检查镜像大小是否足够 */
    total_size = hdr->text_size + hdr->data_size;
    if (size < (sizeof(module_header_t) + total_size))
    {
        return -22; /* EINVAL */
    }

    /* 分配模块跟踪槽位 */
    mod = alloc_module_slot();
    if (mod == NULL)
    {
        return -12; /* ENOMEM */
    }

    /* 计算总内存需求（text + data + bss + 对齐） */
    total_size = hdr->text_size + hdr->data_size + hdr->bss_size;
    total_size = (total_size + MODULE_ALIGN - 1U) & ~(MODULE_ALIGN - 1U);

    /* 分配模块内存 */
    base = module_alloc(total_size);
    if (base == NULL)
    {
        return -12; /* ENOMEM */
    }

    /* 加载代码段 */
    if (hdr->text_size > 0U)
    {
        const uint8_t *src = (const uint8_t *)image + sizeof(module_header_t);
        kernel_memcpy(base, src, hdr->text_size);
    }

    /* 加载数据段 */
    if (hdr->data_size > 0U)
    {
        uint8_t *data_dst = (uint8_t *)base + hdr->text_size;
        const uint8_t *data_src = (const uint8_t *)image +
                                   sizeof(module_header_t) + hdr->text_size;
        kernel_memcpy(data_dst, data_src, hdr->data_size);
    }

    /* 清零 BSS 段 */
    if (hdr->bss_size > 0U)
    {
        uint8_t *bss_dst = (uint8_t *)base + hdr->text_size + hdr->data_size;
        kernel_memzero(bss_dst, hdr->bss_size);
    }

    /* 解析模块中的字符串和偏移 */
    name = (const char *)((const uint8_t *)image + hdr->name_off);

    /* 解析操作函数表指针（基于加载后的基址重定位） */
    ops = (const driver_ops_t *)((uint8_t *)base + hdr->ops_off);

    /* 解析初始化函数指针 */
    init_fn = (kernel_status_t (*)(void))((uint8_t *)base + hdr->init_off);

    /* 注册驱动 */
    ret = driver_register_kern(name, hdr->type, &hdr->match, ops);
    if (ret != KERNEL_OK)
    {
        module_free(base);
        mod->loaded = 0U;
        return (int64_t)ret;
    }

    /* 获取刚注册的驱动 */
    {
        driver_desc_t *drv;

        ticket_lock_acquire(&s_module_lock);
        drv = driver_find_by_name(name);
        if (drv != NULL)
        {
            mod->drv_id = drv->drv_id;
        }
        else
        {
            mod->drv_id = 0U;
        }
        mod->base = base;
        mod->total_size = total_size;
        mod->loaded = 1U;
        ticket_lock_release(&s_module_lock);
    }

    /* 调用模块初始化函数 */
    if (init_fn != NULL)
    {
        ret = init_fn();
        if (ret != KERNEL_OK)
        {
            /* 初始化失败：回滚 */
            (void)driver_unregister_kern(name);
            module_free(base);
            mod->loaded = 0U;
            return (int64_t)ret;
        }
    }

    return (int64_t)mod->drv_id;
}

/**
 * @brief 卸载驱动模块
 *
 * @details 调用清理函数，注销驱动，释放模块内存
 *
 * @param drv_id 驱动 ID
 *
 * @return KERNEL_OK 成功，负值表示错误
 */
kernel_status_t driver_module_unload(uint32_t drv_id)
{
    module_info_t *mod;
    driver_desc_t *drv;
    void (*cleanup_fn)(void);
    kernel_status_t ret;

    if (drv_id == 0U)
    {
        return -22; /* EINVAL */
    }

    /* 查找模块信息 */
    ticket_lock_acquire(&s_module_lock);
    mod = find_module_by_drv_id(drv_id);
    ticket_lock_release(&s_module_lock);

    if (mod == NULL)
    {
        return -2; /* ENOENT */
    }

    /* 查找驱动描述符 */
    drv = find_driver_by_id_internal(drv_id);
    if (drv == NULL)
    {
        return -2; /* ENOENT */
    }

    /* 调用清理函数（通过模块基址 + cleanup_off 计算） */
    if (drv->state == DRIVER_STATE_RUNNING)
    {
        cleanup_fn = (void (*)(void))((uint8_t *)mod->base +
                                       sizeof(module_header_t));
        if (cleanup_fn != NULL)
        {
            cleanup_fn();
        }
    }

    /* 注销驱动 */
    ret = driver_unregister_kern(drv->name);
    if (ret != KERNEL_OK)
    {
        return ret;
    }

    /* 释放模块内存 */
    ticket_lock_acquire(&s_module_lock);
    module_free(mod->base);
    mod->base = NULL;
    mod->drv_id = 0U;
    mod->total_size = 0U;
    mod->loaded = 0U;
    ticket_lock_release(&s_module_lock);

    return KERNEL_OK;
}

/* ========================================================================
 * 模块子系统初始化
 * ======================================================================== */

/**
 * @brief 初始化模块加载器
 */
void driver_module_init(void)
{
    kernel_memzero(s_modules, sizeof(s_modules));
    ticket_lock_init(&s_module_lock);
}

/* ========================================================================
 * 内部辅助函数实现
 * ======================================================================== */

/**
 * @brief 分配一个空闲的模块跟踪槽位
 *
 * @return 模块信息指针，无空闲时返回 NULL
 */
static module_info_t *alloc_module_slot(void)
{
    uint32_t i;

    ticket_lock_acquire(&s_module_lock);
    for (i = 0U; i < MAX_LOADED_MODULES; i++)
    {
        if (s_modules[i].loaded == 0U)
        {
            ticket_lock_release(&s_module_lock);
            return &s_modules[i];
        }
    }
    ticket_lock_release(&s_module_lock);

    return NULL;
}

/**
 * @brief 通过驱动 ID 查找已加载模块
 *
 * @param drv_id 驱动 ID
 *
 * @return 模块信息指针，未找到返回 NULL
 */
static module_info_t *find_module_by_drv_id(uint32_t drv_id)
{
    uint32_t i;

    for (i = 0U; i < MAX_LOADED_MODULES; i++)
    {
        if ((s_modules[i].loaded != 0U) && (s_modules[i].drv_id == drv_id))
        {
            return &s_modules[i];
        }
    }

    return NULL;
}

/** @brief 模块内存池大小（64KB） */
#define MODULE_POOL_SIZE    65536U

/** @brief 模块内存池（16 字节对齐） */
static uint8_t s_module_pool[MODULE_POOL_SIZE]
    __attribute__((aligned(MODULE_ALIGN)));

/** @brief 模块内存池当前偏移 */
static uint32_t s_module_pool_offset;

/**
 * @brief 从模块内存池分配内存
 *
 * @param size 所需大小
 *
 * @return 分配的内存指针
 */
static void *module_alloc(uint32_t size)
{
    void *ptr;
    uint32_t aligned_size;

    /* 16 字节对齐 */
    aligned_size = (size + MODULE_ALIGN - 1U) & ~(MODULE_ALIGN - 1U);

    if ((s_module_pool_offset + aligned_size) > MODULE_POOL_SIZE)
    {
        return NULL;
    }

    ptr = &s_module_pool[s_module_pool_offset];
    s_module_pool_offset += aligned_size;

    return ptr;
}

/**
 * @brief 释放模块内存
 *
 * @note 当前实现为线性分配，不支持释放。
 *       未来应替换为内核堆释放。
 *
 * @param ptr 要释放的内存指针
 */
static void module_free(void *ptr)
{
    /* 线性分配器不支持释放，仅做标记 */
    (void)ptr;
}
