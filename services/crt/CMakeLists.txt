# services/crt/CMakeLists.txt
# C 运行时构建配置（启动代码和初始化）
#
# 提供用户态程序启动所需的 C 运行时：
# - _start.s: 汇编启动代码（入口点）
# - crt0.c: C 运行时初始化

if(CMAKE_CROSSCOMPILING)
    # ================================================================
    # 编译启动代码和 C 运行时
    # ================================================================

    # crt0 库（包含启动代码和运行时初始化）
    add_library(crt0 STATIC
        _start.s
        crt0.c
    )

    # 汇编编译选项
    set_source_files_properties(_start.s PROPERTIES
        COMPILE_FLAGS "-x assembler-with-cpp"
    )

    # C 编译选项
    target_compile_options(crt0 PRIVATE
        -std=c11
        -Wall
        -Wextra
        -ffreestanding
        -fno-builtin
        -fno-common
        -fno-stack-protector
        -nostdlib
        -O2
    )

    # 包含路径
    target_include_directories(crt0 PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/lib/musl_aisafe/include
    )

    # 链接 musl_aisafe 库（用于 __errno_location）
    target_link_libraries(crt0 PUBLIC musl_aisafe)

endif()
