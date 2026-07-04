# services/dev/CMakeLists.txt
# @brief 用户态 virtio-blk 驱动 ELF 构建
# @date 2026-07-04
#
# 编译 virtio_blk_driver.c 为独立 AArch64 ELF，写入磁盘映像。
# 内核通过引导读取器从磁盘加载此 ELF，在用户态执行。

# 编译用户态 virtio-blk 驱动 ELF
add_executable(virtio_blk_driver.elf virtio_blk_driver.c)

target_compile_options(virtio_blk_driver.elf PRIVATE
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

target_link_options(virtio_blk_driver.elf PRIVATE
    -nostdlib
    -nostartfiles
    -static
    -T ${CMAKE_CURRENT_SOURCE_DIR}/virtio_blk_driver.ld
    -Wl,--gc-sections
    -Wl,--no-warn-rwx-segments
)

# 创建磁盘映像：将驱动 ELF 写入扇区 0
# 布局: 扇区 0 = virtio_blk_driver.elf（512B 对齐）
add_custom_command(TARGET virtio_blk_driver.elf POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "创建驱动磁盘映像..."
    COMMAND dd if=/dev/zero of=${CMAKE_BINARY_DIR}/driver_disk.img bs=512 count=256 2>/dev/null
    COMMAND dd if=$<TARGET_FILE:virtio_blk_driver.elf>
            of=${CMAKE_BINARY_DIR}/driver_disk.img
            bs=512 conv=notrunc 2>/dev/null
    COMMENT "创建 driver_disk.img (128KB, 包含 virtio_blk_driver.elf)"
)
