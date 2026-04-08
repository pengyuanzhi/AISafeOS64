/**
 * @file    driver.h
 * @brief   驱动框架内核接口
 * @author  AISafe64 Team
 * @date    2026-04-08
 * @version 1.0
 *
 * @details 本文件定义了 AISafeOS64 驱动动态加载框架的核心接口：
 *          - 驱动/设备描述符数据结构
 *          - 驱动注册/注销/匹配 API
 *          - 设备注册/注销/probe API
 *          - 设备文件操作（open/close/read/write/ioctl）
 *          - 模块加载/卸载接口
 *
 * @note MISRA-C:2012 合规
 * @warning kernel/ 非 arch/ 目录禁止使用 __asm__/msr/mrs/isb
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_DRIVER_H
#define KERNEL_DRIVER_H

#include <kernel/types.h>
#include <kernel/config.h>
#include <kernel/list.h>
#include <kernel/spinlock.h>
#include <stdint.h>
#include <stddef.h>

/* ========== 驱动设备类型定义 ========== */

/** @brief UART 串口设备 */
#define DRIVER_TYPE_UART        0U

/** @brief 块设备 */
#define DRIVER_TYPE_BLOCK       1U

/** @brief 网络设备 */
#define DRIVER_TYPE_NET         2U

/** @brief GPU 显示设备 */
#define DRIVER_TYPE_GPU         3U

/** @brief 输入设备 */
#define DRIVER_TYPE_INPUT       4U

/** @brief 杂项设备 */
#define DRIVER_TYPE_MISC        5U

/** @brief 设备类型总数 */
#define DRIVER_TYPE_COUNT       6U

/* ========== 驱动状态枚举 ========== */

/**
 * @brief 驱动/设备状态枚举
 *
 * @details 定义驱动和设备的生命周期状态
 */
typedef enum
{
    DRIVER_STATE_UNINITIALIZED = 0U,  /**< @brief 未初始化 */
    DRIVER_STATE_INITIALIZED,         /**< @brief 已初始化 */
    DRIVER_STATE_RUNNING,             /**< @brief 运行中 */
    DRIVER_STATE_SUSPENDED,           /**< @brief 已挂起 */
    DRIVER_STATE_FAILED,              /**< @brief 故障 */
    DRIVER_STATE_REMOVED              /**< @brief 已移除 */
} driver_state_t;

/* ========== 设备标识结构 ========== */

/**
 * @brief PCI 设备 ID（bus:device:function）
 *
 * @details 标识一个 PCI 总线上的设备位置
 */
typedef struct
{
    uint8_t  bus;       /**< @brief 总线号 */
    uint8_t  device;    /**< @brief 设备号 */
    uint8_t  function;  /**< @brief 功能号 */
    uint8_t  reserved;  /**< @brief 保留对齐 */
} device_id_t;

/**
 * @brief 设备匹配条件
 *
 * @details 用于驱动与设备之间的匹配。
 *          支持 compatible 字符串匹配（设备树）或
 *          vendor_id/device_id 匹配（PCI）。
 */
typedef struct
{
    char     compatible[32U];   /**< @brief 设备树 compatible 字符串 */
    uint32_t vendor_id;         /**< @brief PCI 厂商 ID（0 = 忽略） */
    uint32_t device_id;         /**< @brief PCI 设备 ID（0 = 忽略） */
    uint32_t class_code;        /**< @brief PCI class code（0 = 忽略） */
} driver_match_t;

/* ========== 驱动操作函数表 ========== */

/**
 * @brief 驱动操作函数表
 *
 * @details 定义驱动必须实现的所有操作函数指针。
 *          驱动通过实现这些函数来控制设备。
 */
typedef struct driver_ops
{
    kernel_status_t (*probe)(void *dev_data);       /**< @brief 探测并初始化设备 */
    kernel_status_t (*remove)(void *dev_data);      /**< @brief 移除设备 */
    kernel_status_t (*suspend)(void *dev_data);     /**< @brief 挂起设备 */
    kernel_status_t (*resume)(void *dev_data);      /**< @brief 恢复设备 */
    int64_t         (*read)(void *dev_data, void *buf,
                           uint64_t size, uint64_t offset);  /**< @brief 读取设备数据 */
    int64_t         (*write)(void *dev_data, const void *buf,
                            uint64_t size, uint64_t offset); /**< @brief 写入设备数据 */
    kernel_status_t (*ioctl)(void *dev_data, uint32_t cmd,
                             void *arg);            /**< @brief 设备控制命令 */
    void            (*irq_handler)(uint32_t irq,
                                   void *dev_data); /**< @brief 中断处理函数 */
} driver_ops_t;

/* ========== 设备描述符 ========== */

/**
 * @brief 设备描述符
 *
 * @details 描述一个已注册的物理或虚拟设备，
 *          包含设备标识、MMIO 信息、中断号和状态。
 */
typedef struct device_desc
{
    uint32_t        dev_id;         /**< @brief 全局设备 ID */
    char            name[32U];      /**< @brief 设备名称 */
    uint32_t        type;           /**< @brief 设备类型（DRIVER_TYPE_*） */
    device_id_t     pci_id;         /**< @brief PCI 总线标识 */
    paddr_t         mmio_base;      /**< @brief MMIO 基地址 */
    uint64_t        mmio_size;      /**< @brief MMIO 区域大小 */
    uint32_t        irq;            /**< @brief 中断号 */
    void            *priv;          /**< @brief 驱动私有数据 */
    driver_state_t  state;          /**< @brief 设备当前状态 */
    uint32_t        drv_id;         /**< @brief 绑定的驱动 ID（0 = 未绑定） */
    struct list_head node;          /**< @brief 全局设备链表节点 */
    struct list_head drv_node;      /**< @brief 驱动所属设备链表节点 */
} device_desc_t;

/* ========== 驱动描述符 ========== */

/**
 * @brief 驱动描述符
 *
 * @details 描述一个已注册的内核驱动，
 *          包含驱动匹配条件、操作函数表和状态。
 */
typedef struct driver_desc
{
    uint32_t            drv_id;     /**< @brief 驱动 ID */
    char                name[32U];  /**< @brief 驱动名称 */
    uint32_t            type;       /**< @brief 驱动类型（DRIVER_TYPE_*） */
    driver_match_t      match;      /**< @brief 匹配条件 */
    const driver_ops_t  *ops;       /**< @brief 操作函数表 */
    driver_state_t      state;      /**< @brief 驱动状态 */
    uint32_t            ref_count;  /**< @brief 引用计数 */
    uint32_t            device_count; /**< @brief 绑定的设备数 */
    struct list_head    devices;    /**< @brief 绑定设备链表 */
    struct list_head    node;       /**< @brief 全局驱动链表节点 */
    uint8_t             in_use;     /**< @brief 槽位占用标志 */
} driver_desc_t;

/* ========== 驱动子系统统计 ========== */

/**
 * @brief 驱动子系统统计信息
 *
 * @details 用于调试和监控驱动子系统的运行状态
 */
typedef struct
{
    uint32_t total_drivers;     /**< @brief 已注册驱动总数 */
    uint32_t total_devices;     /**< @brief 已注册设备总数 */
    uint32_t probe_count;       /**< @brief 成功 probe 次数 */
    uint32_t probe_fail_count;  /**< @brief 失败 probe 次数 */
    uint32_t irq_count;         /**< @brief 中断处理次数 */
} driver_stats_t;

/* ========== 模块元数据头 ========== */

/** @brief 模块镜像魔数（"MODR"） */
#define MODULE_MAGIC  0x4D4F4452U

/**
 * @brief 模块元数据头
 *
 * @details 编译时生成，嵌入在驱动模块镜像的头部，
 *          包含模块的元信息和各段的偏移量。
 */
typedef struct
{
    uint32_t        magic;          /**< @brief 魔数（MODULE_MAGIC） */
    uint32_t        version;        /**< @brief 模块版本 */
    uint32_t        name_off;       /**< @brief 驱动名在模块中的偏移 */
    uint32_t        type;           /**< @brief DRIVER_TYPE_* */
    driver_match_t  match;          /**< @brief 匹配条件 */
    uint32_t        ops_off;        /**< @brief driver_ops_t 偏移 */
    uint32_t        init_off;       /**< @brief 模块初始化函数偏移 */
    uint32_t        cleanup_off;    /**< @brief 模块清理函数偏移 */
    uint32_t        text_size;      /**< @brief 代码段大小 */
    uint32_t        data_size;      /**< @brief 数据段大小 */
    uint32_t        bss_size;       /**< @brief BSS 段大小 */
} module_header_t;

/* ========== 驱动子系统 API ========== */

/**
 * @brief 初始化驱动子系统
 *
 * @details 初始化驱动/设备静态池、全局链表和自旋锁。
 *          必须在调度器启动前调用。
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t driver_subsys_init(void);

/**
 * @brief 注册驱动到子系统
 *
 * @param name   驱动名称（不能为 NULL）
 * @param type   驱动类型（DRIVER_TYPE_*）
 * @param match  匹配条件（不能为 NULL）
 * @param ops    操作函数表（不能为 NULL）
 *
 * @return KERNEL_OK 成功，负值表示错误
 */
kernel_status_t driver_register_kern(const char *name, uint32_t type,
                                      const driver_match_t *match,
                                      const driver_ops_t *ops);

/**
 * @brief 注销驱动
 *
 * @param name 驱动名称
 *
 * @return KERNEL_OK 成功，负值表示错误
 */
kernel_status_t driver_unregister_kern(const char *name);

/**
 * @brief 注册设备
 *
 * @param name      设备名称
 * @param type      设备类型
 * @param mmio_base MMIO 基地址
 * @param mmio_size MMIO 区域大小
 * @param irq       中断号
 * @param priv      驱动私有数据
 *
 * @return KERNEL_OK 成功，负值表示错误
 */
kernel_status_t device_register(const char *name, uint32_t type,
                                 paddr_t mmio_base, uint64_t mmio_size,
                                 uint32_t irq, void *priv);

/**
 * @brief 注销设备
 *
 * @param dev_id 设备 ID
 *
 * @return KERNEL_OK 成功，负值表示错误
 */
kernel_status_t device_unregister(uint32_t dev_id);

/**
 * @brief 对所有未绑定设备执行 probe
 *
 * @details 遍历所有设备，尝试匹配已注册的驱动并调用 probe。
 *
 * @return KERNEL_OK 成功
 */
kernel_status_t device_probe_all(void);

/**
 * @brief 通过 compatible 字符串查找驱动
 *
 * @param compatible compatible 字符串
 *
 * @return 驱动描述符指针，未找到返回 NULL
 */
driver_desc_t *driver_find_by_compatible(const char *compatible);

/**
 * @brief 通过名称查找驱动
 *
 * @param name 驱动名称
 *
 * @return 驱动描述符指针，未找到返回 NULL
 */
driver_desc_t *driver_find_by_name(const char *name);

/**
 * @brief通过设备 ID 查找设备
 *
 * @param dev_id 设备 ID
 *
 * @return 设备描述符指针，未找到返回 NULL
 */
device_desc_t *device_find_by_id(uint32_t dev_id);

/**
 * @brief 打开设备
 *
 * @param dev_id 设备 ID
 *
 * @return KERNEL_OK 成功，负值表示错误
 */
kernel_status_t device_open(uint32_t dev_id);

/**
 * @brief 关闭设备
 *
 * @param dev_id 设备 ID
 *
 * @return KERNEL_OK 成功，负值表示错误
 */
kernel_status_t device_close(uint32_t dev_id);

/**
 * @brief 从设备读取数据
 *
 * @param dev_id 设备 ID
 * @param buf    目标缓冲区
 * @param size   读取大小
 * @param offset 偏移量
 *
 * @return >=0 实际读取字节数，<0 错误码
 */
int64_t device_read(uint32_t dev_id, void *buf,
                    uint64_t size, uint64_t offset);

/**
 * @brief 向设备写入数据
 *
 * @param dev_id 设备 ID
 * @param buf    源缓冲区
 * @param size   写入大小
 * @param offset 偏移量
 *
 * @return >=0 实际写入字节数，<0 错误码
 */
int64_t device_write(uint32_t dev_id, const void *buf,
                     uint64_t size, uint64_t offset);

/**
 * @brief 设备控制命令
 *
 * @param dev_id 设备 ID
 * @param cmd    命令号
 * @param arg    命令参数
 *
 * @return KERNEL_OK 成功，负值表示错误
 */
kernel_status_t device_ioctl(uint32_t dev_id, uint32_t cmd, void *arg);

/**
 * @brief 获取驱动子系统统计信息
 *
 * @param stats 输出统计信息（不能为 NULL）
 */
void driver_get_stats(driver_stats_t *stats);

/* ========== 模块加载/卸载 API ========== */

/**
 * @brief 加载驱动模块镜像
 *
 * @param image 模块镜像数据指针
 * @param size  镜像大小
 *
 * @return >=0 驱动 ID，<0 错误码
 */
int64_t driver_module_load(const void *image, uint32_t size);

/**
 * @brief 卸载驱动模块
 *
 * @param drv_id 驱动 ID
 *
 * @return KERNEL_OK 成功，负值表示错误
 */
kernel_status_t driver_module_unload(uint32_t drv_id);

#endif /* KERNEL_DRIVER_H */
