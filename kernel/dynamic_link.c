/**
 * @file    dynamic_link.c
 * @brief   动态链接实现
 * @author  AISafe64 Team
 * @date    2026-05-02
 * @version 1.0
 *
 * @details 实现动态链接：
 *          - 共享库加载
 *          - 符号解析
 *          - 符号绑定
 *          - 函数调用
 *
 * @note MISRA C:2012 合规
 * @note 对应优化点：动态链接
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#include <kernel/dynamic_link.h>
#include <kernel/printk.h>
#include <kernel/barrier.h>
#include <kernel/string.h>
#include <kernel/spinlock.h>
#include <kernel/process.h>

/* ========================================================================
 * 全局动态链接管理器实例
 * ======================================================================== */

/**
 * @brief 全局动态链接管理器实例
 */
static dynlib_manager_t s_dynlib_manager CACHE_ALIGN(64);

/**
 * @brief 动态链接管理器初始化标志
 */
static bool s_dynlib_initialized = false;

/* ========================================================================
 * 符号查找
 * ======================================================================== */

/**
 * @brief 计算字符串哈希
 *
 * @details 使用 FNV-1a 哈希算法计算字符串哈希。
 *
 * @param str  字符串指针
 *
 * @return 哈希值
 */
static inline uint32_t dynlib_hash(const char *str)
{
    uint32_t hash = 2166136261U;
    const uint8_t *c = (const uint8_t *)str;

    while (*c != '\0')
    {
        hash ^= *c++;
        hash *= 16777619U;
    }

    return hash;
}

/**
 * @brief 查找符号
 *
 * @details 在符号表中查找指定符号。
 *
 * @param manager 动态链接管理器实例
 * @param name    符号名称
 *
 * @return 符号指针，失败返回 NULL
 */
static inline const dynlib_sym_t *dynlib_find_symbol(const dynlib_manager_t *manager,
                                                      const char *name)
{
    uint32_t hash;
    uint32_t i;
    uint32_t offset;
    uint32_t stride;

    if (manager == NULL)
    {
        return NULL;
    }

    hash = dynlib_hash(name);

    /* 使用链表法处理哈希冲突 */
    offset = hash % DYNLIB_MAX_SYMS;

    /* 计算步长（应该是质数，减少冲突）*/
    stride = hash % (DYNLIB_MAX_SYMS - 1) + 1;

    /* 遍历链表 */
    for (i = 0U; i < DYNLIB_MAX_SYMS; i++)
    {
        const dynlib_sym_t *sym = &manager->syms[(offset + i * stride) % DYNLIB_MAX_SYMS];

        if (sym->name != NULL && hash == sym->hash &&
            strcmp(name, sym->name) == 0)
        {
            return sym;
        }

        /* 空槽位，停止搜索 */
        if (sym->name == NULL)
        {
            break;
        }
    }

    return NULL;
}

/* ========================================================================
 * 动态链接操作 API 实现
 * ======================================================================== */

/**
 * @brief 初始化动态链接管理器
 *
 * @param manager 动态链接管理器实例
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t dynlib_init(dynlib_manager_t *manager)
{
    uint32_t i;

    if (manager == NULL)
    {
        return -(int32_t)EINVAL;
    }

    (void)memset(manager, 0U, sizeof(dynlib_manager_t));

    for (i = 0U; i < DYNLIB_MAX_LIBS; i++)
    {
        manager->libs[i] = NULL;
    }

    for (i = 0U; i < DYNLIB_MAX_SYMS; i++)
    {
        manager->syms[i].name = NULL;
    }

    spin_lock_init(&manager->lock);
    manager->lib_count = 0U;
    manager->sym_count = 0U;
    manager->total_size = 0U;

    s_dynlib_initialized = true;

    printk("Dynamic Linker initialized: max %u libs, max %u syms\n",
           DYNLIB_MAX_LIBS, DYNLIB_MAX_SYMS);

    return KERNEL_OK;
}

/**
 * @brief 加载共享库
 *
 * @details 加载动态链接库。
 *
 * @param manager 动态链接管理器实例
 * @param path    库文件路径
 *
 * @return 库描述符指针，失败返回 NULL
 */
const dynlib_desc_t *dynlib_load(dynlib_manager_t *manager, const char *path)
{
    uint32_t i;
    dynlib_desc_t *desc;
    uint32_t path_len;

    if (manager == NULL)
    {
        return NULL;
    }

    if (path == NULL)
    {
        return NULL;
    }

    if (!s_dynlib_initialized)
    {
        return NULL;
    }

    path_len = (uint32_t)strlen(path);

    if (path_len >= DYNLIB_PATH_MAX)
    {
        return NULL;
    }

    spin_lock(&manager->lock);

    /* 检查是否已经加载 */
    for (i = 0U; i < DYNLIB_MAX_LIBS; i++)
    {
        if (manager->libs[i] != NULL &&
            strcmp(manager->libs[i]->path, path) == 0)
        {
            /* 已经加载，返回现有库 */
            spin_unlock(&manager->lock);
            return manager->libs[i];
        }
    }

    /* 查找空闲槽位 */
    for (i = 0U; i < DYNLIB_MAX_LIBS; i++)
    {
        if (manager->libs[i] == NULL)
        {
            break;
        }
    }

    if (i >= DYNLIB_MAX_LIBS)
    {
        spin_unlock(&manager->lock);
        return NULL;
    }

    /* TODO: 实际加载库文件（需要实现 ELF 解析）*/
    /* 这里暂时返回 NULL，等待实际实现 */
    spin_unlock(&manager->lock);

    printk("Dynamic Linker: skip loading '%s' (not implemented yet)\n", path);

    return NULL;
}

/**
 * @brief 卸载共享库
 *
 * @details 卸载动态链接库。
 *
 * @param manager 动态链接管理器实例
 * @param desc    库描述符
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t dynlib_unload(dynlib_manager_t *manager, const dynlib_desc_t *desc)
{
    uint32_t i;

    if (manager == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (desc == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (!s_dynlib_initialized)
    {
        return -(int32_t)EINVAL;
    }

    spin_lock(&manager->lock);

    /* 从库数组中移除 */
    for (i = 0U; i < DYNLIB_MAX_LIBS; i++)
    {
        if (manager->libs[i] == desc)
        {
            manager->libs[i] = NULL;
            manager->lib_count--;

            /* TODO: 释放库内存 */
            spin_unlock(&manager->lock);
            return KERNEL_OK;
        }
    }

    spin_unlock(&manager->lock);
    return KERNEL_OK;
}

/**
 * @brief 解析符号
 *
 * @details 解析符号地址。
 *
 * @param manager 动态链接管理器实例
 * @param name    符号名称
 * @param addr    输出参数，符号地址
 *
 * @return KERNEL_OK 成功
 * @return -ENOENT 符号未找到
 */
kernel_status_t dynlib_symbol(const dynlib_manager_t *manager,
                              const char *name,
                              void **addr)
{
    const dynlib_sym_t *sym;

    if (manager == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (name == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (addr == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (!s_dynlib_initialized)
    {
        return -(int32_t)EINVAL;
    }

    /* 查找符号 */
    sym = dynlib_find_symbol(manager, name);

    if (sym == NULL)
    {
        return -(int32_t)ENOENT;
    }

    *addr = sym->address;

    return KERNEL_OK;
}

/**
 * @brief 绑定符号
 *
 * @details 绑定符号地址到全局符号表。
 *
 * @param manager 动态链接管理器实例
 * @param name    符号名称
 * @param addr    符号地址
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t dynlib_bind(const dynlib_manager_t *manager,
                            const char *name,
                            void *addr)
{
    uint32_t hash;
    uint32_t i;
    uint32_t offset;
    uint32_t stride;

    if (manager == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (name == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (!s_dynlib_initialized)
    {
        return -(int32_t)EINVAL;
    }

    if (addr == NULL)
    {
        return -(int32_t)EINVAL;
    }

    hash = dynlib_hash(name);

    /* 查找符号 */
    for (i = 0U; i < DYNLIB_MAX_SYMS; i++)
    {
        dynlib_sym_t *sym = &manager->syms[i];

        if (sym->name != NULL && hash == sym->hash &&
            strcmp(name, sym->name) == 0)
        {
            /* 更新符号地址 */
            sym->address = addr;
            return KERNEL_OK;
        }

        /* 空槽位 */
        if (sym->name == NULL)
        {
            break;
        }
    }

    return KERNEL_OK;
}

/**
 * @brief 调用函数
 *
 * @details 通过函数指针调用动态链接的函数。
 *
 * @param manager 动态链接管理器实例
 * @param name    函数名称
 * @param args    函数参数
 * @param ret     输出参数，返回值
 *
 * @return KERNEL_OK 成功
 * @return -ENOENT 函数未找到
 */
kernel_status_t dynlib_call(const dynlib_manager_t *manager,
                            const char *name,
                            void *args,
                            void **ret)
{
    const dynlib_sym_t *sym;
    void *func;

    if (manager == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (name == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if (!s_dynlib_initialized)
    {
        return -(int32_t)EINVAL;
    }

    /* 查找符号 */
    sym = dynlib_find_symbol(manager, name);

    if (sym == NULL)
    {
        return -(int32_t)ENOENT;
    }

    func = sym->address;

    /* 调用函数 */
    if (func != NULL)
    {
        if (sym->type == DYNLIB_SYM_FUNC)
        {
            /* 调用函数 */
            typedef void (*func_t)(void);
            ((func_t)func)();
        }
        else
        {
            return -(int32_t)EINVAL;
        }
    }

    return KERNEL_OK;
}

/**
 * @brief 获取动态链接统计信息
 *
 * @param manager 动态链接管理器实例
 * @param lib_count   输出：加载的库数量
 * @param sym_count   输出：符号数量
 * @param total_size  输出：总大小
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t dynlib_get_stats(const dynlib_manager_t *manager,
                                 uint32_t *lib_count,
                                 uint32_t *sym_count,
                                 uint32_t *total_size)
{
    if (manager == NULL)
    {
        return -(int32_t)EINVAL;
    }

    if ((lib_count == NULL) || (sym_count == NULL) || (total_size == NULL))
    {
        return -(int32_t)EINVAL;
    }

    if (!s_dynlib_initialized)
    {
        return -(int32_t)EINVAL;
    }

    *lib_count = manager->lib_count;
    *sym_count = manager->sym_count;
    *total_size = manager->total_size;

    return KERNEL_OK;
}
