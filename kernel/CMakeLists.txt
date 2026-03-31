# kernel/CMakeLists.txt
# AISafeOS64 微内核核心构建

set(KERNEL_ARCH_DIR ${CMAKE_CURRENT_SOURCE_DIR}/arch/arm64)

# 内核公共头文件
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/kernel
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${KERNEL_ARCH_DIR}
)

# ---------- 架构相关源文件 ----------
set(KERNEL_ARCH_SOURCES
    arch/arm64/boot.S
    arch/arm64/exception.S
    arch/arm64/context.S
    arch/arm64/hal.c
    arch/arm64/entry.c
)

# ---------- 调度器源文件 ----------
set(KERNEL_SCHED_SOURCES
    sched/scheduler.c
    sched/thread.c
    sched/timer.c
    sched/spinlock.c
    sched/mutex.c
)

# ---------- 内核对象源文件（后续阶段添加） ----------
# set(KERNEL_KOBJ_SOURCES
#     kobj/kobject.c
#     kobj/object_pool.c
#     kobj/refcount.c
#     kobj/deferred_free.c
# )

# ---------- IPC 源文件（后续阶段添加） ----------
# set(KERNEL_IPC_SOURCES
#     ipc/endpoint.c
#     ipc/notification.c
#     ipc/fast_path.c
#     ipc/pulse.c
#     ipc/channel.c
# )

# ---------- 能力系统（后续阶段添加） ----------
# set(KERNEL_CAP_SOURCES
#     cap/capability.c
#     cap/cspace.c
# )

# ---------- 内存管理（后续阶段添加） ----------
# set(KERNEL_MM_SOURCES
#     mm/page_table.c
#     mm/vmspace.c
#     mm/shmem.c
#     mm/security.c
# )

# ---------- 中断路由（后续阶段添加） ----------
# set(KERNEL_IRQ_SOURCES
#     irq/interrupt.c
# )

# 合并所有源文件
set(KERNEL_SOURCES
    ${KERNEL_ARCH_SOURCES}
    ${KERNEL_SCHED_SOURCES}
)

# 构建内核静态库
add_library(kernel_static STATIC ${KERNEL_SOURCES})

target_compile_options(kernel_static PRIVATE
    -mgeneral-regs-only      # 不使用浮点寄存器
    -mstrict-align           # 严格对齐
    -fno-pic                 # 不使用位置无关代码
    -fno-pie                 # 不使用位置无关可执行
)

# 内核可执行文件
add_executable(aisafe64.elf ${KERNEL_SOURCES})

target_compile_options(aisafe64.elf PRIVATE
    -mgeneral-regs-only
    -mstrict-align
    -fno-pic
    -fno-pie
)

target_link_options(aisafe64.elf PRIVATE
    -nostartfiles
    -nostdlib
    -static
    -T ${CMAKE_SOURCE_DIR}/lds/kernel.ld
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_BINARY_DIR}/aisafe64.map
    -Wl,--no-warn-rwx-segments
)

# 生成二进制文件（仅在工具可用时）
if(CMAKE_OBJCOPY)
    add_custom_command(TARGET aisafe64.elf POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:aisafe64.elf>
                ${CMAKE_BINARY_DIR}/aisafe64.bin
        COMMENT "生成裸机二进制文件 aisafe64.bin"
    )
endif()

# 生成反汇编文件（仅在工具可用时）
if(CMAKE_OBJDUMP)
    add_custom_command(TARGET aisafe64.elf POST_BUILD
        COMMAND ${CMAKE_OBJDUMP} -d -S $<TARGET_FILE:aisafe64.elf>
                > ${CMAKE_BINARY_DIR}/aisafe64.dis
        COMMENT "生成反汇编文件 aisafe64.dis"
    )
endif()

# 查看大小（仅在工具可用时）
if(CMAKE_SIZE)
    add_custom_command(TARGET aisafe64.elf POST_BUILD
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:aisafe64.elf>
        COMMENT "查看内核映像大小"
    )
endif()
