# AISafe64 CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(AISafe64 C ASM)

# 设置C标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# 项目版本
set(PROJECT_VERSION_MAJOR 1)
set(PROJECT_VERSION_MINOR 0)
set(PROJECT_VERSION_PATCH 0)

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/generated
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/hal
)

# 编译选项
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

# 生成配置头文件
if(EXISTS ${CMAKE_SOURCE_DIR}/.config)
    execute_process(
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/parse_config.py
                ${CMAKE_SOURCE_DIR}/.config
                ${CMAKE_BINARY_DIR}/config/autoconf.h
        )
    include_directories(${CMAKE_BINARY_DIR}/config)
endif()

# 子目录
add_subdirectory(src)
add_subdirectory(hal)
add_subdirectory(lib)
add_subdirectory(tests)

# 链接选项
set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS}
    -nostartfiles
    -nostdlib
    -T ${CMAKE_SOURCE_DIR}/lds/linker.ld
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_BINARY_DIR}/aisafe64.map"
)

# 安装规则
install(TARGETS aisafe64
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
)
