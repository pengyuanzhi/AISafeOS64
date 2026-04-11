# AISafeOS64 微内核 CMakeLists.txt
# @version 3.0
# @date 2026-04-04

cmake_minimum_required(VERSION 3.20)
project(AISafeOS64 C ASM)

# C 标准设置
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# 项目版本
set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 1)
set(PROJECT_VERSION_PATCH 0)
set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

# 微内核公共头文件搜索路径（不包含 include/kernel 避免污染测试）
include_directories(
    ${CMAKE_SOURCE_DIR}/include
)

# ==============================================================================
# MISRA-C:2012 合规编译选项（GNU/Clang）
# ==============================================================================
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall                   # 所有警告
        -Wextra                 # 额外警告
        -Werror                 # 警告视为错误
        -Wpedantic              # 严格标准
        -Wconversion            # 隐式转换警告
        -Wsign-conversion       # 符号转换警告
        -Wshadow                # 变量遮蔽警告
        -Wstrict-prototypes     # 严格原型检查
        -Wmissing-prototypes    # 缺失原型警告
        -Wstrict-overflow=1     # 严格溢出检查
        -Wvla                   # 禁止变长数组警告
        -ffreestanding          # 自由standing环境
        -fno-builtin            # 禁用内置函数
        -fno-common             # 禁止common符号
        -fdata-sections         # 分离数据段
        -ffunction-sections     # 分离代码段
        -fno-strict-aliasing    # 禁止严格别名
        -fno-stack-protector    # 裸机环境无栈保护器运行时
        # MISRA C 额外警告选项
        -Wundef                 # 未定义宏警告
        -Wcast-align            # 对齐转换警告
        -Wwrite-strings         # 字符串字面量 const 限定
        -Wold-style-definition  # 旧式函数定义警告
        -Wmissing-declarations  # 缺少声明警告
        -Wredundant-decls       # 冗余声明警告
        -Wnested-externs        # 嵌套 extern 警告
        -Wlogical-op            # 逻辑操作符可疑使用
        -Waggregate-return      # 返回结构体/联合体警告
    )
endif()

# ==============================================================================
# 构建类型默认值
# ==============================================================================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "构建类型" FORCE)
endif()

# ==============================================================================
# 子目录
# ==============================================================================

# 内核（ARM64 交叉编译）
add_subdirectory(kernel)

# musl libc 适配层（交叉编译时构建）
if(CMAKE_CROSSCOMPILING)
    add_subdirectory(lib/musl_aisafe)
endif()

# ==============================================================================
# 宿主机单元测试（仅非交叉编译时）
# ==============================================================================
if(NOT CMAKE_CROSSCOMPILING)
    enable_testing()

    # 测试公共头文件搜索路径（不包含 include/kernel，避免内核 string.h 宏污染）
    set(TEST_INCLUDE_DIRS
        ${CMAKE_SOURCE_DIR}/tests
        ${CMAKE_SOURCE_DIR}/include
    )

    # 测试源文件列表
    set(TEST_SOURCES
        test_ipc
        test_vfs
        test_security
        test_certification
        test_twin
        test_formal_verify
        test_evidence
        test_spinlock
        test_object_pool
        test_scheduler
        test_mutex
        test_timer
        test_phys_mem
        test_endpoint
        test_notification
        test_channel
        test_capability
        test_capability_revoke
        test_smp
    )

    foreach(test_name ${TEST_SOURCES})
        add_executable(${test_name} tests/${test_name}.c)
        target_include_directories(${test_name} PRIVATE ${TEST_INCLUDE_DIRS})
        target_compile_options(${test_name} PRIVATE
            -std=c11
            -Wall
            -Wextra
            -Wno-unused-function
            -Wno-sign-comparison
            -Wno-missing-prototypes
            -Wno-missing-declarations
            -Wno-old-style-definition
            -Wno-nested-externs
            -Wno-implicit-function-declaration
        )
        # 部分测试需要链接数学库
        target_link_libraries(${test_name} PRIVATE m)
        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()

    # ==============================================================================
    # musl libc 旧版测试（使用 lib/musl_legacy/，保留向后兼容）
    # ==============================================================================

    # 旧版 musl 头文件目录（已废弃，迁移到标准 musl 后删除）
    set(MUSL_LEGACY_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/lib/musl_legacy/include)

    # 旧版 musl 源文件
    set(MUSL_LEGACY_STRING_SOURCES
        lib/musl_legacy/src/string/memcpy.c
        lib/musl_legacy/src/string/memset.c
        lib/musl_legacy/src/string/memmove.c
        lib/musl_legacy/src/string/memcmp.c
        lib/musl_legacy/src/string/memchr.c
        lib/musl_legacy/src/string/strlen.c
        lib/musl_legacy/src/string/strcmp.c
        lib/musl_legacy/src/string/strncmp.c
        lib/musl_legacy/src/string/strcpy.c
        lib/musl_legacy/src/string/strncpy.c
        lib/musl_legacy/src/string/strcat.c
        lib/musl_legacy/src/string/strncat.c
        lib/musl_legacy/src/string/strchr.c
        lib/musl_legacy/src/string/strrchr.c
        lib/musl_legacy/src/string/strstr.c
    )

    set(MUSL_LEGACY_STDIO_SOURCES
        lib/musl_legacy/src/stdio/vsnprintf.c
        lib/musl_legacy/src/stdio/sprintf.c
        lib/musl_legacy/src/stdio/snprintf.c
    )

    set(MUSL_LEGACY_STDLIB_SOURCES
        lib/musl_legacy/src/stdlib/atoi.c
        lib/musl_legacy/src/stdlib/strtol.c
        lib/musl_legacy/src/stdlib/strtoul.c
        lib/musl_legacy/src/stdlib/malloc.c
        lib/musl_legacy/src/stdlib/calloc.c
        lib/musl_legacy/src/stdlib/realloc.c
        lib/musl_legacy/src/stdlib/exit.c
        lib/musl_legacy/src/stdlib/abort.c
        lib/musl_legacy/src/stdlib/atexit.c
    )

    set(MUSL_LEGACY_ERRNO_SOURCES
        lib/musl_legacy/src/errno/errno.c
    )

    # test_musl_string（旧版）
    add_executable(test_musl_string tests/test_musl_string.c ${MUSL_LEGACY_STRING_SOURCES} ${MUSL_LEGACY_ERRNO_SOURCES})
    target_include_directories(test_musl_string PRIVATE ${MUSL_LEGACY_INCLUDE_DIR} ${TEST_INCLUDE_DIRS})
    target_compile_options(test_musl_string PRIVATE -std=c11 -Wall -Wextra -Wno-unused-function -Wno-sign-comparison -Wno-missing-prototypes -Wno-missing-declarations -Wno-old-style-definition -Wno-nested-externs -Wno-implicit-function-declaration -Wno-error)
    add_test(NAME test_musl_string COMMAND test_musl_string)

    # test_musl_stdio（旧版）
    add_executable(test_musl_stdio tests/test_musl_stdio.c ${MUSL_LEGACY_STRING_SOURCES} ${MUSL_LEGACY_STDIO_SOURCES} ${MUSL_LEGACY_ERRNO_SOURCES})
    target_include_directories(test_musl_stdio PRIVATE ${MUSL_LEGACY_INCLUDE_DIR} ${TEST_INCLUDE_DIRS})
    target_compile_options(test_musl_stdio PRIVATE -std=c11 -Wall -Wextra -Wno-unused-function -Wno-sign-comparison -Wno-missing-prototypes -Wno-missing-declarations -Wno-old-style-definition -Wno-nested-externs -Wno-implicit-function-declaration -Wno-unused-but-set-variable -Wno-error)
    add_test(NAME test_musl_stdio COMMAND test_musl_stdio)

    # test_musl_stdlib（旧版）
    add_executable(test_musl_stdlib tests/test_musl_stdlib.c ${MUSL_LEGACY_STRING_SOURCES} ${MUSL_LEGACY_STDLIB_SOURCES} ${MUSL_LEGACY_ERRNO_SOURCES})
    target_include_directories(test_musl_stdlib PRIVATE ${MUSL_LEGACY_INCLUDE_DIR} ${TEST_INCLUDE_DIRS})
    target_compile_options(test_musl_stdlib PRIVATE -std=c11 -Wall -Wextra -Wno-unused-function -Wno-sign-comparison -Wno-missing-prototypes -Wno-missing-declarations -Wno-old-style-definition -Wno-nested-externs -Wno-implicit-function-declaration -Wno-error)
    add_test(NAME test_musl_stdlib COMMAND test_musl_stdlib)

    # test_musl_posix — 仅测试头文件常量和类型定义
    add_executable(test_musl_posix tests/test_musl_posix.c)
    target_include_directories(test_musl_posix PRIVATE ${MUSL_LEGACY_INCLUDE_DIR} ${TEST_INCLUDE_DIRS})
    target_compile_options(test_musl_posix PRIVATE -std=c11 -Wall -Wextra -Wno-unused-function -Wno-sign-comparison -Wno-missing-prototypes -Wno-missing-declarations -Wno-old-style-definition -Wno-nested-externs -Wno-implicit-function-declaration -Wno-error)
    add_test(NAME test_musl_posix COMMAND test_musl_posix)
endif()

# ==============================================================================
# QEMU 运行目标
# ==============================================================================
add_custom_target(qemu
    COMMAND qemu-system-aarch64 -M virt -cpu cortex-a57 -smp 1
            -m 1024 -nographic -kernel ${CMAKE_BINARY_DIR}/aisafe64.bin
    DEPENDS aisafe64.elf
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "在 QEMU virt 平台上运行 AISafeOS64 微内核"
)

# QEMU 调试目标
add_custom_target(qemu-debug
    COMMAND qemu-system-aarch64 -M virt -cpu cortex-a57 -smp 1
            -m 1024 -nographic -kernel ${CMAKE_BINARY_DIR}/aisafe64.bin
            -S -s
    DEPENDS aisafe64.elf
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "在 QEMU 中启动 AISafeOS64 并等待 GDB 连接（端口 1234）"
)

# ==============================================================================
# 代码统计目标
# ==============================================================================
add_custom_target(stats
    COMMAND ${CMAKE_COMMAND} -E echo "========== AISafeOS64 代码统计 =========="
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/code_stats.sh ${CMAKE_SOURCE_DIR}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "统计代码行数、函数数量、圈复杂度"
)

# ==============================================================================
# 静态分析目标
# ==============================================================================
add_custom_target(misra-check
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_misra.sh ${CMAKE_SOURCE_DIR}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "运行 MISRA C:2012 静态分析检查"
)

add_custom_target(format-check
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_format.sh ${CMAKE_SOURCE_DIR}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "检查代码格式规范"
)
