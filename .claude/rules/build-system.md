## 13. CMake构建系统规范

### 13.1 CMake文件组织

#### 13.1.1 项目结构要求
```cmake
# CMakeLists.txt文件组织原则:
# 1. 根CMakeLists.txt: 项目整体配置
# 2. 子目录CMakeLists.txt: 模块级配置
# 3. cmake/*.cmake: 可重用的CMake模块
# 4. 工具链文件独立: cmake/toolchain-arm64.cmake
```

#### 13.1.2 命名规范
```cmake
# CMake变量命名
set(PROJECT_NAME TinyOS64)           # 项目: 全大写
set(SOURCE_FILES main.c)              # 局部: 大写下划线
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}") # CMake内置: 保留原始

# 目标命名
add_executable(tinyos64_core ${SRC})  # 可执行文件: 小写下划线
add_library(kernel STATIC ${SRC})      # 库: 小写下划线
add_custom_target(misra_check ...)    # 自定义目标: 小写下划线

# 宏和函数命名
macro(add_kernel_module name)          # 宏: 小写下划线
function(compile_config_file)          # 函数: 小写下划线
endfunction()
endmacro()
```

#### 13.1.3 最小CMake版本
```cmake
# 必须声明最小CMake版本
cmake_minimum_required(VERSION 3.20)
project(TinyOS64 C ASM)

# 设置C标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)  # 禁止编译器扩展，确保标准合规
```

### 13.2 编译选项规范

#### 13.2.1 安全相关编译选项
```cmake
# MISRA-C:2012合规的编译选项
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall                   # 启用所有警告
        -Wextra                 # 启用额外警告
        -Werror                 # 将警告视为错误
        -Wpedantic              # 严格遵循标准
        -Wconversion            # 隐式转换警告
        -Wsign-conversion       # 符号转换警告
        -Wshadow                # 变量遮蔽警告
        -Wstrict-prototypes     # 严格原型检查
        -Wmissing-prototypes    # 缺失原型警告
        -Wstrict-overflow=1     # 严格溢出检查
        -Wvla                   # 禁止变长数组警告
        -Wpedantic              # ISO C合规
    )
endif()

# 嵌入式系统特定选项
add_compile_options(
    -ffreestanding          # 自由standing环境（无标准库）
    -fno-builtin            # 禁用内置函数
    -fno-common             # 禁止未初始化全局变量合并
    -fdata-sections         # 分离数据段
    -ffunction-sections     # 分离代码段
    -fno-strict-aliasing    # 禁止严格别名（避免未定义行为）
)
```

#### 13.2.2 调试/发布配置
```cmake
# Debug配置: 无优化，包含调试信息
set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")

# Release配置: 优化，包含调试符号
set(CMAKE_C_FLAGS_RELEASE "-O2 -g1")

# RelWithDebInfo: 优化并保留调试信息
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g3")

# MinSizeRel: 最小体积优化
set(CMAKE_C_FLAGS_MINSIZEREL "-Os -g1")

# 设置默认构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
```

### 13.3 链接选项规范

#### 13.3.1 链接器脚本
```cmake
# 指定链接器脚本
set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/lds/linker.ld)

# 链接选项
set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS}
    -nostartfiles           # 不使用标准启动文件
    -nostdlib               # 不链接标准库
    -T ${LINKER_SCRIPT}     # 使用自定义链接器脚本
    -Wl,--gc-sections       # 删除未使用的段
    -Wl,-Map=$<TARGET>.map # 生成内存映射文件
    "
)
```

#### 13.3.2 链接库顺序
```cmake
# 链接库顺序: 依赖者在前，被依赖者在后
target_link_libraries(tinyos64_kernel
    kernel                      # 内核核心
    hal                         # 硬件抽象层
    lib                         # 工具库
    -lm                         # 数学库（最后）
)

# 不要链接标准C库
# 嵌入式系统通常不使用标准库
```

### 13.4 目标定义规范

#### 13.4.1 静态库目标
```cmake
# 定义静态库
add_library(kernel STATIC
    scheduler.c
    task.c
    smp.c
    mmu.c
    sync.c
    timer.c
)

# 设置库属性
set_target_properties(kernel PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    POSITION_INDEPENDENT_CODE OFF
)

# 添加包含目录
target_include_directories(kernel
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src/kernel
)
```

#### 13.4.2 可执行文件目标
```cmake
# 定义可执行文件
add_executable(tinyos.elf
    startup.S
    main.c
)

# 链接库
target_link_libraries(tinyos.elf
    PRIVATE kernel hal lib
)

# 设置输出属性
set_target_properties(tinyos.elf PROPERTIES
    OUTPUT_NAME "tinyos"
    SUFFIX ".elf"
)

# 生成二进制文件
add_custom_command(TARGET tinyos.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY}
        -O binary
        $<TARGET_FILE:tinyos.elf>
        ${CMAKE_BINARY_DIR}/tinyos.bin
    COMMENT "Generating binary file..."
)

# 生成反汇编文件
add_custom_command(TARGET tinyos.elf POST_BUILD
    COMMAND ${CMAKE_OBJDUMP}
        -d -S
        $<TARGET_FILE:tinyos.elf>
        > ${CMAKE_BINARY_DIR}/tinyos.dis
    COMMENT "Generating disassembly..."
)
```

### 13.5 交叉编译配置

#### 13.5.1 工具链文件
```cmake
# cmake/toolchain-arm64.cmake
# 目标系统
set(CMAKE_SYSTEM_NAME Generic)          # 通用嵌入式系统
set(CMAKE_SYSTEM_PROCESSOR aarch64)     # ARM64架构

# 交叉编译工具链
set(CMAKE_C_COMPILER aarch64-none-elf-gcc)
set(CMAKE_ASM_COMPILER aarch64-none-elf-gcc)
set(CMAKE_AR aarch64-none-elf-ar)
set(CMAKE_RANLIB aarch64-none-elf-ranlib)
set(CMAKE_OBJCOPY aarch64-none-elf-objcopy)
set(CMAKE_OBJDUMP aarch64-none-elf-objdump)
set(CMAKE_SIZE aarch64-none-elf-size)

# 设置查找路径行为
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
```

#### 13.5.2 使用工具链文件
```bash
# 命令行使用
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm64.cmake

# 或设置环境变量
export CMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm64.cmake
cmake ..
```

### 13.6 测试集成

#### 13.1.1 启用测试
```cmake
# 启用测试
enable_testing()

# 添加测试
add_executable(test_scheduler tests/test_scheduler.c)
target_link_libraries(test_scheduler kernel unity)

# 注册测试
add_test(NAME scheduler_test COMMAND test_scheduler)
```

#### 13.6.2 覆盖率收集
```cmake
# 启用覆盖率（仅Debug构建）
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(
        -fprofile-arcs
        -ftest-coverage
    )
    add_link_options(
        -fprofile-arcs
        -ftest-coverage
    )

    # 添加覆盖率目标
    add_custom_target(coverage
        COMMAND lcov --capture --directory . --output-file coverage.info
        COMMAND lcov --remove coverage.info '/usr/*' --output-file coverage.info
        COMMAND genhtml coverage.info --output-directory coverage_html
        COMMENT "Generating code coverage report..."
    )
endif()
```

---

