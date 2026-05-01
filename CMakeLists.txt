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
    # musl libc 旧版测试（已删除 - 用户态服务已 100% 迁移到标准 musl）
    # ==============================================================================
    # 注释：旧版测试 (test_musl_string/stdio/stdlib) 已删除
    # 理由：用户态服务已全部迁移到 musl_aisafe (标准 musl + 适配层)
    # 参考：docs/MUSL_MIGRATION_STATUS.md


    # test_musl_safety — musl 功能安全包装测试
    add_executable(test_musl_safety tests/test_musl_safety.c lib/musl_aisafe/src/musl_safety.c)
    target_include_directories(test_musl_safety PRIVATE ${CMAKE_SOURCE_DIR}/lib/musl_aisafe/include ${TEST_INCLUDE_DIRS})
    target_compile_options(test_musl_safety PRIVATE -std=c11 -Wall -Wextra -Wno-unused-function -Wno-sign-comparison -Wno-missing-prototypes -Wno-missing-declarations -Wno-old-style-definition -Wno-nested-externs -Wno-implicit-function-declaration -Wno-error)
    add_test(NAME test_musl_safety COMMAND test_musl_safety)

    # test_syscall_dispatch — musl 系统调用分发器测试
    add_executable(test_syscall_dispatch tests/test_syscall_dispatch.c lib/musl_aisafe/src/syscall_dispatch.c lib/musl_aisafe/src/musl_safety.c lib/musl_aisafe/src/fs_ipc.c lib/musl_aisafe/src/extra_syscalls.c)
    target_include_directories(test_syscall_dispatch PRIVATE ${CMAKE_SOURCE_DIR}/lib/musl_aisafe/arch/aarch64_aisafe ${CMAKE_SOURCE_DIR}/lib/musl_aisafe/include ${TEST_INCLUDE_DIRS})
    target_compile_options(test_syscall_dispatch PRIVATE -std=c11 -Wall -Wextra -Wno-unused-function -Wno-sign-comparison -Wno-missing-prototypes -Wno-missing-declarations -Wno-old-style-definition -Wno-nested-externs -Wno-implicit-function-declaration -Wno-error)
    add_test(NAME test_syscall_dispatch COMMAND test_syscall_dispatch)

    # test_musl_posix — 仅测试头文件常量和类型定义
    add_executable(test_musl_posix tests/test_musl_posix.c)
    target_include_directories(test_musl_posix PRIVATE ${MUSL_LEGACY_INCLUDE_DIR} ${TEST_INCLUDE_DIRS})
    target_compile_options(test_musl_posix PRIVATE -std=c11 -Wall -Wextra -Wno-unused-function -Wno-sign-comparison -Wno-missing-prototypes -Wno-missing-declarations -Wno-old-style-definition -Wno-nested-externs -Wno-implicit-function-declaration -Wno-error)
    add_test(NAME test_musl_posix COMMAND test_musl_posix)

    # test_musl_det_malloc — 确定性内存分配器测试
    add_executable(test_musl_det_malloc tests/test_musl_det_malloc.c lib/musl_aisafe/src/det_malloc.c)
    target_include_directories(test_musl_det_malloc PRIVATE ${TEST_INCLUDE_DIRS})
    target_compile_options(test_musl_det_malloc PRIVATE -std=c11 -Wall -Wextra -Wno-unused-function -Wno-sign-comparison -Wno-missing-prototypes -Wno-missing-declarations -Wno-old-style-definition -Wno-nested-externs -Wno-implicit-function-declaration -Wno-error)
    add_test(NAME test_musl_det_malloc COMMAND test_musl_det_malloc)

    # test_misra_wrapper — MISRA C:2012 合规包装测试
    add_executable(test_misra_wrapper tests/test_misra_wrapper.c lib/musl_aisafe/src/misra_wrapper.c)
    target_include_directories(test_misra_wrapper PRIVATE ${CMAKE_SOURCE_DIR}/lib/musl_aisafe/include ${TEST_INCLUDE_DIRS})
    target_compile_options(test_misra_wrapper PRIVATE -std=c11 -Wall -Wextra -Wno-unused-function -Wno-sign-comparison -Wno-missing-prototypes -Wno-missing-declarations -Wno-old-style-definition -Wno-nested-externs -Wno-implicit-function-declaration -Wno-error)
    add_test(NAME test_misra_wrapper COMMAND test_misra_wrapper)

    # test_userland_services — 用户态服务集成测试框架
    add_executable(test_userland_services tests/test_userland_services.c)
    target_include_directories(test_userland_services PRIVATE ${TEST_INCLUDE_DIRS})
    target_compile_options(test_userland_services PRIVATE -std=c11 -Wall -Wextra -Wno-unused-function -Wno-sign-comparison -Wno-missing-prototypes -Wno-missing-declarations -Wno-old-style-definition -Wno-nested-externs -Wno-implicit-function-declaration -Wno-error)
    add_test(NAME test_userland_services COMMAND test_userland_services)

    # test_userland_services_ipc — 用户态服务 IPC 集成测试
    add_executable(test_userland_services_ipc tests/test_userland_services_ipc.c tests/ipc_stubs.c)
    target_include_directories(test_userland_services_ipc PRIVATE ${TEST_INCLUDE_DIRS})
    target_compile_options(test_userland_services_ipc PRIVATE -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-sign-comparison -Wno-missing-prototypes -Wno-missing-declarations -Wno-old-style-definition -Wno-nested-externs -Wno-implicit-function-declaration -Wno-error)
    add_test(NAME test_userland_services_ipc COMMAND test_userland_services_ipc)

    # test_userland_services_extended — 用户态服务测试扩展（Phase 3）
    add_executable(test_userland_services_extended tests/test_userland_services_extended.c tests/ipc_stubs.c)
    target_include_directories(test_userland_services_extended PRIVATE ${TEST_INCLUDE_DIRS})
    target_compile_options(test_userland_services_extended PRIVATE -std=c11 -Wall -Wextra -Wno-unused-variable -Wno-sign-comparison -Wno-missing-prototypes -Wno-missing-declarations -Wno-old-style-definition -Wno-nested-externs -Wno-implicit-function-declaration -Wno-error)
    add_test(NAME test_userland_services_extended COMMAND test_userland_services_extended)

    # test_fs_server_ipc — FS 服务 IPC 服务器端处理测试
    add_executable(test_fs_server_ipc tests/test_fs_server_ipc.c)
    target_include_directories(test_fs_server_ipc PRIVATE ${TEST_INCLUDE_DIRS})
    target_compile_options(test_fs_server_ipc PRIVATE -std=c11 -Wall -Wextra -Wno-unused-function -Wno-sign-comparison -Wno-missing-prototypes -Wno-missing-declarations -Wno-old-style-definition -Wno-nested-externs -Wno-implicit-function-declaration -Wno-error)
    add_test(NAME test_fs_server_ipc COMMAND test_fs_server_ipc)

    # test_net_service — Net 服务主循环测试
    add_executable(test_net_service tests/test_net_service.c)
    target_include_directories(test_net_service PRIVATE ${TEST_INCLUDE_DIRS})
    target_compile_options(test_net_service PRIVATE -std=c11 -Wall -Wextra -Wno-unused-function -Wno-sign-comparison -Wno-missing-prototypes -Wno-missing-declarations -Wno-old-style-definition -Wno-nested-externs -Wno-implicit-function-declaration -Wno-error)
    add_test(NAME test_net_service COMMAND test_net_service)

    # test_net_integration_full — 网络协议栈与 VirtIO Net 完整集成测试
    add_executable(test_net_integration_full tests/test_net_integration_full.c)
    target_include_directories(test_net_integration_full PRIVATE ${TEST_INCLUDE_DIRS})
    target_compile_options(test_net_integration_full PRIVATE -std=c11 -Wall -Wextra -Wno-unused-function -Wno-sign-comparison -Wno-missing-prototypes -Wno-missing-declarations -Wno-old-style-definition -Wno-nested-externs -Wno-implicit-function-declaration -Wno-error)
    add_test(NAME test_net_integration_full COMMAND test_net_integration_full)

    # test_performance_benchmark — 内核性能基准测试
    add_executable(test_performance_benchmark tests/test_performance_benchmark.c)
    target_include_directories(test_performance_benchmark PRIVATE ${TEST_INCLUDE_DIRS})
    target_compile_options(test_performance_benchmark PRIVATE -std=c11 -Wall -Wextra -Wno-unused-function -Wno-sign-comparison -Wno-missing-prototypes -Wno-missing-declarations -Wno-old-style-definition -Wno-nested-externs -Wno-implicit-function-declaration -Wno-error)
    add_test(NAME test_performance_benchmark COMMAND test_performance_benchmark)

    # test_shm — 共享内存和零拷贝 IPC 测试
    add_executable(test_shm tests/test_shm.c)
    target_include_directories(test_shm PRIVATE ${TEST_INCLUDE_DIRS})
    target_compile_options(test_shm PRIVATE -std=c11 -Wall -Wextra -Wno-unused-function -Wno-sign-comparison -Wno-missing-prototypes -Wno-missing-declarations -Wno-old-style-definition -Wno-nested-externs -Wno-implicit-function-declaration -Wno-error)
    add_test(NAME test_shm COMMAND test_shm)

    # test_ring_buffer — 环形缓冲区性能测试
    add_executable(test_ring_buffer tests/test_ring_buffer.c)
    target_include_directories(test_ring_buffer PRIVATE ${TEST_INCLUDE_DIRS})
    target_compile_options(test_ring_buffer PRIVATE -std=c11 -Wall -Wextra -Wno-unused-function -Wno-sign-comparison -Wno-missing-prototypes -Wno-missing-declarations -Wno-old-style-definition -Wno-nested-externs -Wno-implicit-function-declaration -Wno-error)
    add_test(NAME test_ring_buffer COMMAND test_ring_buffer)

    # test_ipc_batch — IPC 批量处理测试
    add_executable(test_ipc_batch tests/test_ipc_batch.c)
    target_include_directories(test_ipc_batch PRIVATE ${TEST_INCLUDE_DIRS})
    target_compile_options(test_ipc_batch PRIVATE -std=c11 -Wall -Wextra -Wno-unused-function -Wno-sign-comparison -Wno-missing-prototypes -Wno-missing-declarations -Wno-old-style-definition -Wno-nested-externs -Wno-implicit-function-declaration -Wno-error)
    add_test(NAME test_ipc_batch COMMAND test_ipc_batch)
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
