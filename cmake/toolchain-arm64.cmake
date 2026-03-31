# cmake/toolchain-arm64.cmake
# AISafeOS64 微内核 ARM64 交叉编译工具链配置
# @version 2.0
# @date 2026-03-31

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SYSTEM_VERSION 1)

# 交叉编译工具链前缀（自动检测可用编译器）
find_program(AARCH64_GCC aarch64-none-elf-gcc)
if(AARCH64_GCC)
    set(TOOLCHAIN_PREFIX aarch64-none-elf-)
else()
    find_program(AARCH64_LINUX_GCC aarch64-linux-gnu-gcc)
    if(AARCH64_LINUX_GCC)
        set(TOOLCHAIN_PREFIX aarch64-linux-gnu-)
    else()
        message(WARNING "未找到 ARM64 交叉编译器，请安装 gcc-aarch64-linux-gnu 或 aarch64-none-elf-gcc")
        set(TOOLCHAIN_PREFIX aarch64-none-elf-)
    endif()
endif()

# 交叉编译工具
set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_AR ${TOOLCHAIN_PREFIX}ar)
set(CMAKE_RANLIB ${TOOLCHAIN_PREFIX}ranlib)
set(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_OBJDUMP ${TOOLCHAIN_PREFIX}objdump)
set(CMAKE_SIZE ${TOOLCHAIN_PREFIX}size)
set(CMAKE_STRIP ${TOOLCHAIN_PREFIX}strip)

# 可执行文件后缀
set(CMAKE_EXECUTABLE_SUFFIX_ASM .elf)
set(CMAKE_EXECUTABLE_SUFFIX_C .elf)

# 查找路径设置
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 禁用编译器检查
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_C_COMPILER_ID_RUN TRUE)
