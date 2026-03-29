# AISafe64 RTOS - Tests CMakeLists.txt

# 测试可执行文件
add_executable(test_atomic
    test_atomic.c
    test_framework.c
)

# 包含目录
target_include_directories(test_atomic PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/include
    ${CMAKE_SOURCE_DIR}/src/arch/arm64/include
)

# 编译定义
target_compile_definitions(test_atomic PRIVATE
    TEST_MODE=1
)

# 测试特定编译选项
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_atomic PRIVATE
        -Wall
        -Wextra
        -Werror
        -O2
        -g
    )
endif()

# 如果是主机测试（非嵌入式）
if(NOT CMAKE_CROSSCOMPILING)
    # 主机测试不需要链接脚本
    target_link_options(test_atomic PRIVATE
        -Wl,--gc-sections
    )

    # 添加测试到CTest
    add_test(NAME atomic_test COMMAND test_atomic)
else()
    # 嵌入式测试需要特殊处理
    message(WARNING "Cross-compilation detected. Tests will be built but not run.")
endif()

# Scheduler tests
add_executable(test_sched
    test_sched.c
    test_framework.c
    # Scheduler sources
    ../src/kernel/sched.c
    ../src/kernel/sched_fifo.c
    ../src/kernel/sched_rr.c
    ../src/kernel/sched_cfs.c
    ../src/kernel/sched_edf.c
    ../src/kernel/sched_idle.c
    ../src/kernel/sched_extra.c
    # Memory management
    ../src/kernel/mm/kheap.c
    # Libraries
    ../src/lib/bitmap.c
    ../src/lib/rbtree.c
    ../src/lib/printk.c
)

target_include_directories(test_sched PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/include
    ${CMAKE_SOURCE_DIR}/src/arch/arm64/include
)

target_compile_definitions(test_sched PRIVATE
    TEST_MODE=1
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_sched PRIVATE
        -Wall
        -Wextra
        -Werror
        -O2
        -g
    )
endif()

if(NOT CMAKE_CROSSCOMPILING)
    target_link_options(test_sched PRIVATE
        -Wl,--gc-sections
    )
    add_test(NAME sched_test COMMAND test_sched)
else()
    message(WARNING "Cross-compilation detected. Scheduler tests will be built but not run.")
endif()

# Task integration tests
add_executable(test_task_integration
    test_task_integration.c
    test_framework.c
    # Scheduler sources
    ../src/kernel/sched.c
    ../src/kernel/sched_fifo.c
    ../src/kernel/sched_rr.c
    ../src/kernel/sched_cfs.c
    ../src/kernel/sched_edf.c
    ../src/kernel/sched_idle.c
    ../src/kernel/sched_extra.c
    # Memory management
    ../src/kernel/mm/kheap.c
    # Libraries
    ../src/lib/bitmap.c
    ../src/lib/rbtree.c
    ../src/lib/printk.c
)

target_include_directories(test_task_integration PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/include
    ${CMAKE_SOURCE_DIR}/src/arch/arm64/include
)

target_compile_definitions(test_task_integration PRIVATE
    TEST_MODE=1
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_task_integration PRIVATE
        -Wall
        -Wextra
        -Werror
        -O2
        -g
    )
endif()

if(NOT CMAKE_CROSSCOMPILING)
    target_link_options(test_task_integration PRIVATE
        -Wl,--gc-sections
    )
    add_test(NAME task_integration_test COMMAND test_task_integration)
else()
    message(WARNING "Cross-compilation detected. Task integration tests will be built but not run.")
endif()

