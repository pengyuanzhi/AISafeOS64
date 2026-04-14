# services/test_elf/CMakeLists.txt
# @brief 编译 AArch64 ELF 测试程序
# @date 2026-04-14
# @version 1.0

# 编译 simple.c 为 AArch64 ELF 可执行文件
add_executable(simple.elf simple.c)

target_compile_options(simple.elf PRIVATE
    -std=c11
    -Wall
    -Wextra
    -ffreestanding
    -fno-builtin
    -fno-common
    -fno-stack-protector
    -nostdlib
    -nostartfiles
    -mgeneral-regs-only
    -O2
)

target_link_options(simple.elf PRIVATE
    -nostdlib
    -nostartfiles
    -static
    -T ${CMAKE_CURRENT_SOURCE_DIR}/simple.ld
    -Wl,--gc-sections
    -Wl,--no-warn-rwx-segments
)

# 生成二进制文件（用于磁盘映像）
add_custom_command(TARGET simple.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
        $<TARGET_FILE:simple.elf>
        ${CMAKE_BINARY_DIR}/simple.bin
    COMMENT "生成 simple.bin 裸机二进制"
)

# 生成反汇编文件（用于调试）
add_custom_command(TARGET simple.elf POST_BUILD
    COMMAND ${CMAKE_OBJDUMP} -d -S
        $<TARGET_FILE:simple.elf>
        > ${CMAKE_BINARY_DIR}/simple.dis
    COMMENT "生成 simple.dis 反汇编"
)

# 创建磁盘映像：将 simple.elf 嵌入磁盘扇区
# 布局: 扇区 0 = ELF 文件, 以扇区(512B)对齐
add_custom_command(TARGET simple.elf POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "创建磁盘映像..."
    COMMAND dd if=/dev/zero of=${CMAKE_BINARY_DIR}/disk.img bs=512 count=128 2>/dev/null
    COMMAND dd if=$<TARGET_FILE:simple.elf> of=${CMAKE_BINARY_DIR}/disk.img bs=512 conv=notrunc 2>/dev/null
    COMMENT "创建 disk.img (64KB, 包含 simple.elf)"
)
