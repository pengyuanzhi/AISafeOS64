# AISafeOS64 微内核 CMakeLists.txt
# @version 2.0
# @date 2026-03-31

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

# 微内核公共头文件搜索路径
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/kernel
)

# MISRA-C:2012 合规编译选项
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
    )
endif()

# 微内核子目录
add_subdirectory(kernel)

# QEMU 运行目标
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
