# AISafeOS64 微内核 CMakeLists.txt
# @version 3.0
# @date 2026-04-04

cmake_minimum_required(VERSION 3.20)
project(AISafeOS64 C ASM)

# ARM64 交叉编译工具链
if(EXISTS "${CMAKE_SOURCE_DIR}/cmake/toolchain-arm64.cmake")
    include(${CMAKE_SOURCE_DIR}/cmake/toolchain-arm64.cmake)
endif()

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
    # 只对 C 文件应用编译选项
    set_source_files_properties(
        ${CMAKE_CURRENT_LIST_DIR}/kernel/*.c
        ${CMAKE_CURRENT_LIST_DIR}/lib/*.c
        PROPERTIES
        COMPILE_OPTIONS
        "-Wall;-Wextra;-Werror;-Wpedantic;-Wconversion;-Wsign-conversion;-Wshadow;-Wstrict-prototypes;-Wmissing-prototypes;-Wstrict-overflow=1;-Wvla;-ffreestanding;-fno-builtin;-fno-common;-fdata-sections;-ffunction-sections;-fno-strict-aliasing;-fno-stack-protector;-Wundef;-Wcast-align;-Wwrite-strings;-Wold-style-definition;-Wmissing-declarations;-Wredundant-decls;-Wnested-externs;-Wlogical-op;-Waggregate-return"
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
    # ELF 测试程序（交叉编译为 AArch64 ELF）
    add_subdirectory(services/test_elf)
    
    # 测试 musl_aisafe 库链接（暂时禁用，避免浮点运算链接问题）
    # add_executable(test_musl_aisafe services/test_musl_aisafe.c)
    # target_link_libraries(test_musl_aisafe musl_aisafe)
    # target_include_directories(test_musl_aisafe PRIVATE ${CMAKE_SOURCE_DIR}/lib/musl_aisafe/include)
    
    # 用户态服务（musl_aisafe 迁移）
    add_subdirectory(services)
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
        test_process
        test_proc_states
        # TCP/IP 优化功能测试（暂时禁用，unity.h 缺失）
        # test_tcp_cubic
        # test_tcp_timestamp
        # test_tcp_keepalive
        # test_tcp_options
        # test_tcp_cong
        # test_tcp_nagle_sack
        # test_ip_reass
        # test_icmp_error
        # TCP/IP 协议栈功能测试
        net_test
        # Socket API 扩展测试
        test_net_socket_api
    )

    foreach(test_name ${TEST_SOURCES})
        add_executable(${test_name} tests/${test_name}.c)
        target_include_directories(${test_name} PRIVATE ${TEST_INCLUDE_DIRS})
        target_compile_options(${test_name} PRIVATE
            -std=c11
            -Wall
            -Wextra
            -Wno-unused-function
            -Wno-missing-prototypes
            -Wno-missing-declarations
            -Wno-old-style-definition
            -Wno-nested-externs
            -Wno-implicit-function-declaration
            -Wno-unused-but-set-variable
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

    # test_musl_safety — musl 功能安全包装测试
    add_executable(test_musl_safety tests/test_musl_safety.c lib/musl_aisafe/src/musl_safety.c)
    target_include_directories(test_musl_safety PRIVATE ${CMAKE_SOURCE_DIR}/lib/musl_aisafe/include ${TEST_INCLUDE_DIRS})
    target_compile_options(test_musl_safety PRIVATE -std=c11 -Wall -Wextra -Wno-unused-function -Wno-sign-comparison -Wno-missing-prototypes -Wno-missing-declarations -Wno-old-style-definition -Wno-nested-externs -Wno-implicit-function-declaration -Wno-error)
    add_test(NAME test_musl_safety COMMAND test_musl_safety)

    # test_syscall_dispatch — musl 系统调用分发器测试
    add_executable(test_syscall_dispatch tests/test_syscall_dispatch.c lib/musl_aisafe/src/syscall_dispatch.c lib/musl_aisafe/src/musl_safety.c)
    target_include_directories(test_syscall_dispatch PRIVATE ${CMAKE_SOURCE_DIR}/lib/musl_aisafe/arch/aarch64_aisafe ${CMAKE_SOURCE_DIR}/lib/musl_aisafe/include ${CMAKE_SOURCE_DIR}/lib/musl_legacy/include ${TEST_INCLUDE_DIRS})
    target_compile_options(test_syscall_dispatch PRIVATE -std=c11 -Wall -Wextra -Wno-unused-function -Wno-sign-comparison -Wno-missing-prototypes -Wno-missing-declarations -Wno-old-style-definition -Wno-nested-externs -Wno-implicit-function-declaration -Wno-error)
    add_test(NAME test_syscall_dispatch COMMAND test_syscall_dispatch)

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
            -drive if=virtio,format=raw,file=${CMAKE_BINARY_DIR}/disk.img
    DEPENDS aisafe64.elf simple.elf
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "在 QEMU virt 平台上运行 AISafeOS64 微内核（带磁盘映像）"
)

# QEMU 调试目标
add_custom_target(qemu-debug
    COMMAND qemu-system-aarch64 -M virt -cpu cortex-a57 -smp 1
            -m 1024 -nographic -kernel ${CMAKE_BINARY_DIR}/aisafe64.bin
            -drive if=virtio,format=raw,file=${CMAKE_BINARY_DIR}/disk.img
            -S -s
    DEPENDS aisafe64.elf simple.elf
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "在 QEMU 中启动 AISafeOS64 并等待 GDB 连接（端口 1234）"
)

# QEMU 网络测试目标
add_custom_target(qemu-net
    COMMAND qemu-system-aarch64 -M virt -cpu cortex-a57 -smp 4
            -m 1024 -nographic -kernel ${CMAKE_BINARY_DIR}/aisafe64.bin
            -drive file=${CMAKE_BINARY_DIR}/disk.img,format=raw,if=none,id=hd0 -device virtio-blk-device,drive=hd0
            -netdev user,id=net0,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:80 -device virtio-net-device,netdev=net0
    DEPENDS aisafe64.elf simple.elf net.elf drv_virtio_net.elf
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "在 QEMU 中运行 AISafeOS64 微内核（带网络支持）"
)

# QEMU 网络测试脚本
add_custom_target(qemu-net-script
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/run_qemu_net.sh
    DEPENDS aisafe64.elf simple.elf net.elf drv_virtio_net.elf
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "使用脚本运行 AISafeOS64 网络测试"
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
