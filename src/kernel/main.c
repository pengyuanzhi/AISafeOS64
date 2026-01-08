/**
 * @file main.c
 * @brief AISafe64 RTOS - 内核C入口点
 * @author AISafe64 Team
 * @date 2025-01-08
 * @version 1.0
 *
 * @details 内核主函数
 *          - 初始化硬件
 *          - 初始化内核子系统
 *          - 启动调度器
 *          - 进入空闲循环
 *
 * @note MISRA-C:2012合规
 */

#include "printk.h"
#include "types.h"

/**
 * @brief 外部函数声明
 */
extern int printk_init(void);
extern void printk(const char *fmt, ...);

/* 内存管理 */
extern int page_allocator_init(uint64_t base_addr, uint64_t size);
extern int kheap_init(void *start, uint64_t size);

/**
 * @brief 系统配置
 */
#define KERNEL_VERSION "1.0.0"
#define KERNEL_BUILD_DATE __DATE__
#define KERNEL_BUILD_TIME __TIME__

/**
 * @brief 系统信息结构
 */
typedef struct {
    const char *version;
    const char *build_date;
    const char *build_time;
    uint32_t cpu_count;
    uint32_t mem_size_mb;
} SystemInfo_t;

static const SystemInfo_t g_system_info = {
    .version = KERNEL_VERSION,
    .build_date = KERNEL_BUILD_DATE,
    .build_time = KERNEL_BUILD_TIME,
    .cpu_count = 1U,  /* TODO: 从设备树读取 */
    .mem_size_mb = 1024U  /* TODO: 从设备树读取 */
};

/**
 * @brief 内核主函数
 *
 * @details 由start.S调用
 *          - 初始化串口
 *          - 打印系统信息
 *          - 初始化子系统
 *          - 进入主循环
 *
 * @note 不应返回
 */
void kernel_main(void) {
    /* 初始化串口 */
    if (printk_init() != 0) {
        /* UART初始化失败，停止执行 */
        goto kernel_halt;
    }

    /* 打印启动信息 */
    printk("\n");
    printk("**************************************\n");
    printk("*  AISafe64 RTOS v%s\n", g_system_info.version);
    printk("**************************************\n");
    printk("\n");
    printk("Build Date: %s %s\n", g_system_info.build_date, g_system_info.build_time);
    printk("CPU Count: %u\n", g_system_info.cpu_count);
    printk("Memory Size: %u MB\n", g_system_info.mem_size_mb);
    printk("\n");

    /* TODO: 初始化内存管理 */
    printk("[INIT] Memory management... ");
    printk("NOT IMPLEMENTED\n");

    /* TODO: 初始化调度器 */
    printk("[INIT] Scheduler... ");
    printk("NOT IMPLEMENTED\n");

    /* TODO: 初始化中断管理 */
    printk("[INIT] Interrupt controller... ");
    printk("NOT IMPLEMENTED\n");

    /* TODO: 初始化系统调用 */
    printk("[INIT] System calls... ");
    printk("NOT IMPLEMENTED\n");

    printk("\n");
    printk("[INIT] Memory management... ");

    /* 初始化内核堆（必须先于页分配器，因为页分配器使用堆） */
    /* 使用静态内存区域作为堆（1MB） */
    extern uint8_t __kernel_heap_start[];
    extern uint8_t __kernel_heap_end[];
    uint64_t heap_size = (uint64_t)(__kernel_heap_end - __kernel_heap_start);

    if (kheap_init(__kernel_heap_start, heap_size) != 0) {
        printk("FAILED (kernel heap)\n");
        goto kernel_halt;
    }

    /* 初始化物理页分配器 */
    if (page_allocator_init(0x40000000UL, 1024 * 1024 * 1024UL) != 0) {
        printk("FAILED (page allocator)\n");
        goto kernel_halt;
    }

    printk("OK\n");
    printk("[INIT]   Kernel heap: %lu KB @ %p\n", heap_size / 1024, __kernel_heap_start);
    printk("[INIT]   Page allocator: 262144 pages (1GB)\n");

    /* TODO: 初始化调度器 */
    printk("[INIT] Scheduler... ");
    printk("NOT IMPLEMENTED\n");

    /* TODO: 初始化中断管理 */
    printk("[INIT] Interrupt controller... ");
    printk("NOT IMPLEMENTED\n");

    /* TODO: 初始化系统调用 */
    printk("[INIT] System calls... ");
    printk("NOT IMPLEMENTED\n");

    printk("\n");
    printk("[OK] Kernel initialization complete\n");
    printk("\n");

    /* 进入主循环 */
    while (true) {
        /* TODO: 空闲任务或系统管理 */
        __asm__ volatile("wfe");  /* 等待中断 */
    }

kernel_halt:
    /* 内核停止 */
    printk("\n");
    printk("[FATAL] Kernel halted!\n");
    while (true) {
        __asm__ volatile("wfe");
    }
}

/**
 * @brief 内核panic函数
 *
 * @details 停止系统执行
 */
void kernel_panic(void) {
    printk("\n");
    printk("[FATAL] Kernel panic!\n");
    printk("[FATAL] System halted.\n");
    printk("\n");

    while (true) {
        __asm__ volatile("wfe");
    }
}
