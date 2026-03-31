/**
 * @file    compiler.h
 * @brief   编译器辅助宏定义
 * @author  AISafe64 Team
 * @date    2026-03-31
 * @version 2.0
 *
 * @details 本文件定义了 AISafeOS64 微内核所使用的编译器辅助宏，
 *          包括：
 *          - 分支预测提示（likely / unlikely）
 *          - 对齐属性宏（ALIGNED）
 *          - 打包属性宏（PACKED）
 *          - 段属性宏（SECTION）
 *          - 函数属性宏（NORETURN、WEAK、UNUSED、USED）
 *          - 内联控制宏（ALWAYS_INLINE、NOINLINE）
 *          - 指针限定宏（RESTRICT）
 *          - 偏移量宏（offsetof）
 *          - 编译时断言宏（BUILD_BUG_ON）
 *          - 类型安全的最值宏（MIN / MAX / CLAMP）
 *
 * @note MISRA-C:2012 合规
 * @note 使用 GCC / Clang 通用语法，兼容主流 ARM64 工具链
 * @note 所有宏定义均为编译器特性的标准封装，不引入非标准行为
 *
 * @copyright Copyright (c) 2026 AISafe64 Team
 */

#ifndef KERNEL_COMPILER_H
#define KERNEL_COMPILER_H

/* ========================================================================
 * 分支预测提示宏
 * ======================================================================== */

/**
 * @def likely
 * @brief 提示编译器条件表达式很可能为真
 *
 * @details 使用 __builtin_expect 将分支预测信息提供给编译器，
 *          使编译器将"为真"的代码路径放在顺序执行位置，
 *          优化指令缓存命中率。
 *
 * @param x 布尔表达式
 *
 * @return 表达式 x 的原始值
 *
 * @note 仅作为性能优化提示，不改变程序语义
 *
 * @par 示例
 * @code
 * if (likely(ptr != NULL))
 * {
 *     // 快速路径：ptr 通常不为空
 * }
 * @endcode
 */
#define likely(x)   __builtin_expect(!!(x), 1)

/**
 * @def unlikely
 * @brief 提示编译器条件表达式很可能为假
 *
 * @details 使用 __builtin_expect 将分支预测信息提供给编译器，
 *          使编译器将"为假"的代码路径放在顺序执行位置，
 *          优化错误处理等罕见路径的性能。
 *
 * @param x 布尔表达式
 *
 * @return 表达式 x 的原始值
 *
 * @note 适用于错误检查、边界条件等罕见分支
 *
 * @par 示例
 * @code
 * if (unlikely(ret != KERNEL_OK))
 * {
 *     // 错误路径：通常不会执行
 * }
 * @endcode
 */
#define unlikely(x) __builtin_expect(!!(x), 0)

/* ========================================================================
 * 对齐属性宏
 * ======================================================================== */

/**
 * @def ALIGNED
 * @brief 指定变量或类型的对齐字节数
 *
 * @details 使用编译器属性强制指定对齐边界。
 *          常见用途：
 *          - 16 字节对齐：SIMD 操作、栈对齐
 *          - 64 字节对齐：缓存行对齐，避免伪共享
 *          - 4096 字节对齐：页对齐
 *
 * @param n 对齐字节数，必须为 2 的幂次方
 *
 * @par 示例
 * @code
 * ALIGNED(64) typedef struct
 * {
 *     atomic_uint lock;
 *     uint8_t padding[60];
 * } CacheLine_t;
 * @endcode
 */
#define ALIGNED(n) __attribute__((aligned(n)))

/* ========================================================================
 * 打包属性宏
 * ======================================================================== */

/**
 * @def PACKED
 * @brief 取消结构体填充，紧凑排列成员
 *
 * @details 使用编译器属性移除结构体成员间的填充字节，
 *          使结构体大小等于所有成员大小之和。
 *
 * @warning 可能导致非对齐访问，影响性能
 * @note 仅用于硬件寄存器映射、网络协议头等需要精确布局的场景
 *
 * @par 示例
 * @code
 * typedef struct PACKED
 * {
 *     uint16_t vendor_id;
 *     uint16_t device_id;
 *     uint32_t class_code;
 * } PciHeader_t;
 * @endcode
 */
#define PACKED __attribute__((packed))

/* ========================================================================
 * 段属性宏
 * ======================================================================== */

/**
 * @def SECTION
 * @brief 将变量或函数放入指定链接器段
 *
 * @details 使用编译器属性将符号放置到指定的链接器段中。
 *          常见用途：
 *          - 将异常向量表放到专门的向量段
 *          - 将初始化代码放到启动段
 *          - 将性能关键代码放到特定内存区域
 *
 * @param s 段名称字符串
 *
 * @note 段名称必须在链接器脚本中有对应定义
 *
 * @par 示例
 * @code
 * SECTION(".vector_table")
 * ExceptionHandler_t exception_table[16];
 * @endcode
 */
#define SECTION(s) __attribute__((section(s)))

/* ========================================================================
 * 函数属性宏
 * ======================================================================== */

/**
 * @def NORETURN
 * @brief 标记函数不会返回到调用者
 *
 * @details 告知编译器被标记的函数不会正常返回。
 *          典型场景：内核恐慌（panic）、线程退出、
 *          无限循环（空闲任务）等。
 *
 * @note 编译器将优化调用点，不生成返回后的代码
 *
 * @par 示例
 * @code
 * void NORETURN kernel_panic(const char *msg)
 * {
 *     log_emerg("PANIC: %s\n", msg);
 *     for (;;) { }
 * }
 * @endcode
 */
#define NORETURN __attribute__((noreturn))

/**
 * @def WEAK
 * @brief 声明弱符号
 *
 * @details 将函数或变量声明为弱符号，允许其他编译单元
 *          提供强符号来覆盖此定义。常用于默认实现和钩子函数。
 *
 * @note 如果存在同名的强符号，弱符号将被覆盖
 *
 * @par 示例
 * @code
 * void WEAK board_init(void)
 * {
 *     // 默认空实现，板级代码可覆盖
 * }
 * @endcode
 */
#define WEAK __attribute__((weak))

/**
 * @def UNUSED
 * @brief 标记变量或参数可能未使用
 *
 * @details 告知编译器被标记的变量或参数可能不被使用，
 *          避免产生"未使用变量"警告。
 *
 * @note 适用于回调函数中不需要的参数、条件编译中的变量等
 *
 * @par 示例
 * @code
 * void callback(int UNUSED param)
 * {
 *     // 不使用 param，但不产生警告
 * }
 * @endcode
 */
#define UNUSED __attribute__((unused))

/**
 * @def USED
 * @brief 强制编译器保留符号
 *
 * @details 告知编译器即使符号看似未被引用，也必须保留。
 *          防止链接器在优化阶段移除该符号。
 *
 * @note 适用于通过链接器脚本引用的符号、调试信息等
 *
 * @par 示例
 * @code
 * USED const char build_info[] = "Build: " __DATE__;
 * @endcode
 */
#define USED __attribute__((used))

/* ========================================================================
 * 内联控制宏
 * ======================================================================== */

/**
 * @def ALWAYS_INLINE
 * @brief 强制函数内联
 *
 * @details 使用编译器属性强制将函数内联到调用点。
 *          适用于极小的性能关键函数（如原子操作、位运算）。
 *
 * @warning 过度使用会增加代码体积，影响指令缓存命中率
 * @note 必须与 static 一起使用
 *
 * @par 示例
 * @code
 * static inline ALWAYS_INLINE uint32_t get_cpu_id(void)
 * {
 *     uint64_t mpidr;
 *     __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
 *     return (uint32_t)(mpidr & 0xFFU);
 * }
 * @endcode
 */
#define ALWAYS_INLINE __attribute__((always_inline))

/**
 * @def NOINLINE
 * @brief 禁止函数内联
 *
 * @details 使用编译器属性阻止函数被内联。
 *          适用于以下场景：
 *          - 函数体较大，内联反而降低性能
 *          - 需要独立地址用于调试或性能分析
 *          - 防止栈深度膨胀
 *
 * @par 示例
 * @code
 * void NOINLINE kernel_panic(const char *msg)
 * {
 *     // 不希望被内联的严重错误处理
 * }
 * @endcode
 */
#define NOINLINE __attribute__((noinline))

/* ========================================================================
 * 指针限定宏
 * ======================================================================== */

/**
 * @def RESTRICT
 * @brief 指针别名限定符
 *
 * @details C99 restrict 限定符的封装。
 *          告知编译器通过该指针访问的内存不会通过其他指针访问，
 *          允许编译器进行更激进的优化。
 *
 * @warning 如果违反别名约定，行为未定义
 * @note MISRA-C:2012 下需由评审确认安全性后方可使用
 *
 * @par 示例
 * @code
 * void memcpy_custom(void *RESTRICT dst, const void *RESTRICT src, size_t n);
 * @endcode
 */
#define RESTRICT restrict

/* ========================================================================
 * 偏移量宏
 * ======================================================================== */

/**
 * @def offsetof
 * @brief 计算结构体成员在结构体中的偏移量
 *
 * @details 计算指定成员相对于结构体起始地址的字节偏移量。
 *          使用编译器内置函数实现，避免未定义行为。
 *
 * @param type   结构体类型名
 * @param member 成员字段名
 *
 * @return 成员在结构体中的偏移量（size_t 类型）
 *
 * @note 本文件自定义 offsetof 仅在标准库未提供时生效
 *
 * @par 示例
 * @code
 * typedef struct
 * {
 *     uint32_t id;
 *     uint64_t data;
 * } MyStruct_t;
 *
 * size_t off = offsetof(MyStruct_t, data);   // off = 8（含对齐填充）
 * @endcode
 */
#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

/* ========================================================================
 * 编译时断言宏
 * ======================================================================== */

/**
 * @def BUILD_BUG_ON
 * @brief 编译时断言宏
 *
 * @details 如果常量表达式 expr 在编译时求值为非零值（真），
 *          则触发编译错误。
 *          适用于验证编译时常量、模板参数、配置值等。
 *
 * @param expr 编译时常量表达式
 *             - 如果为 0（假）：编译通过
 *             - 如果为非零（真）：编译失败
 *
 * @note 此宏不产生任何运行时代码
 *
 * @par 示例
 * @code
 * BUILD_BUG_ON(sizeof(TCB_t) > 1024U);
 * BUILD_BUG_ON(CONFIG_MAX_CPUS > 256U);
 * @endcode
 */
#define BUILD_BUG_ON(expr) \
    static_assert(!(expr), "BUILD_BUG_ON: assertion failed")

/* ========================================================================
 * 类型安全的最值宏
 * ======================================================================== */

/**
 * @def MIN
 * @brief 类型安全的最小值宏
 *
 * @details 返回两个值中的较小者。
 *          使用 GCC 语句表达式实现类型安全，
 *          避免传统宏的多次求值和类型提升问题。
 *
 * @param a 第一个值
 * @param b 第二个值
 *
 * @return a 和 b 中的较小值
 *
 * @warning a 和 b 必须为相同类型
 * @warning a 和 b 不应包含带副作用的表达式（如 i++）
 *
 * @par 示例
 * @code
 * uint32_t val = MIN(x, y);
 * @endcode
 */
#define MIN(a, b) \
    __extension__({ \
        __typeof__(a) _min_a = (a); \
        __typeof__(b) _min_b = (b); \
        (_min_a < _min_b) ? _min_a : _min_b; \
    })

/**
 * @def MAX
 * @brief 类型安全的最大值宏
 *
 * @details 返回两个值中的较大者。
 *          使用 GCC 语句表达式实现类型安全，
 *          避免传统宏的多次求值和类型提升问题。
 *
 * @param a 第一个值
 * @param b 第二个值
 *
 * @return a 和 b 中的较大值
 *
 * @warning a 和 b 必须为相同类型
 * @warning a 和 b 不应包含带副作用的表达式（如 i++）
 *
 * @par 示例
 * @code
 * uint32_t val = MAX(x, y);
 * @endcode
 */
#define MAX(a, b) \
    __extension__({ \
        __typeof__(a) _max_a = (a); \
        __typeof__(b) _max_b = (b); \
        (_max_a > _max_b) ? _max_a : _max_b; \
    })

/**
 * @def CLAMP
 * @brief 将值限制在指定范围内
 *
 * @details 如果 val 小于 lo，返回 lo；
 *          如果 val 大于 hi，返回 hi；
 *          否则返回 val 本身。
 *
 * @param val 要限制的值
 * @param lo  允许的最小值（下界）
 * @param hi  允许的最大值（上界）
 *
 * @return 限制在 [lo, hi] 范围内的值
 *
 * @warning lo 必须小于或等于 hi
 * @warning val、lo、hi 必须为相同类型
 *
 * @par 示例
 * @code
 * uint32_t clamped = CLAMP(raw_value, 0U, 255U);
 * @endcode
 */
#define CLAMP(val, lo, hi) \
    __extension__({ \
        __typeof__(val) _clamp_val = (val); \
        __typeof__(lo) _clamp_lo  = (lo); \
        __typeof__(hi) _clamp_hi  = (hi); \
        (_clamp_val < _clamp_lo) ? _clamp_lo : \
        ((_clamp_val > _clamp_hi) ? _clamp_hi : _clamp_val); \
    })

#endif /* KERNEL_COMPILER_H */
