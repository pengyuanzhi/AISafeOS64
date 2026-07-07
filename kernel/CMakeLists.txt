# kernel/CMakeLists.txt
# AISafeOS64 微内核核心构建
# @version 3.0
# @date 2026-04-04

set(KERNEL_ARCH_DIR ${CMAKE_CURRENT_SOURCE_DIR}/arch/arm64)

# ARM64 汇编器配置
set(CMAKE_ASM_COMPILER aarch64-linux-gnu-as)
# 不设置 CMAKE_ASM_FLAGS，避免将 C 编译器选项传递给汇编器

# 内核公共头文件
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/kernel
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/sched
    ${CMAKE_CURRENT_SOURCE_DIR}/ipc
    ${CMAKE_CURRENT_SOURCE_DIR}/cap
    ${CMAKE_CURRENT_SOURCE_DIR}/mm
    ${CMAKE_CURRENT_SOURCE_DIR}/irq
    ${CMAKE_CURRENT_SOURCE_DIR}/verify
    ${CMAKE_CURRENT_SOURCE_DIR}/driver
    ${KERNEL_ARCH_DIR}
)

# ==============================================================================
# 内核核心源文件 (kernel/)
# ==============================================================================
set(KERNEL_CORE_SOURCES
    klog.c
    fs/ramfs.c
    net/socket.c
    process/process.c
    process/signal.c
)

# ==============================================================================
# 架构相关源文件 (arch/arm64)
# ==============================================================================
set(KERNEL_ARCH_SOURCES
    arch/arm64/boot.S
    arch/arm64/exception.S
    arch/arm64/context.S
    arch/arm64/init_elf.S
    arch/arm64/hal.c
    arch/arm64/entry.c
    arch/arm64/mmu.c
    arch/arm64/gic.c
    arch/arm64/ipi.c
    arch/arm64/secure_boot.c
    arch/arm64/smp_boot.c
    arch/arm64/icache_optimize.c
)

# ==============================================================================
# 调度器源文件 (sched/)
# ==============================================================================
set(KERNEL_SCHED_SOURCES
    sched/scheduler.c
    sched/sched_class.c
    sched/sched_rr.c
    sched/thread.c
    sched/timer.c
    sched/spinlock.c
    sched/mutex.c
    sched/arinc653.c
    sched/smp.c
    sched/stack_guard.c
    sched/sharded_lock.c
    sched/user_timer.c
)

# ==============================================================================
# IPC 子系统源文件 (ipc/)
# ==============================================================================
set(KERNEL_IPC_SOURCES
    ipc/endpoint.c
    ipc/channel.c
    ipc/notification.c
    ipc/ic2.c
    ipc/ring_buffer.c
    ipc/sharded_message_queue.c
    ipc/zero_copy.c
)

# ==============================================================================
# 能力系统源文件 (cap/)
# ==============================================================================
set(KERNEL_CAP_SOURCES
    cap/capability.c
    cap/cspace.c
)

# ==============================================================================
# 内存管理源文件 (mm/)
# ==============================================================================
set(KERNEL_MM_SOURCES
    mm/page_table.c
    mm/vmspace.c
    mm/phys_mem.c
    mm/security.c
    mm/kobject.c
    mm/object_pool.c
    mm/shm.c
    mm/slab.c
    mm/kmalloc.c
    mm/uaccess.c
    mm/rbtree.c
    mm/hot_data_cache.c
    mm/pcpu_pool.c
)

# ==============================================================================
# 中断路由源文件 (irq/)
# ==============================================================================
set(KERNEL_IRQ_SOURCES
    irq/irq.c
    irq/syscall_dispatch.c
)

# ==============================================================================

# ==============================================================================
# ELF 加载器源文件 (mm/)
# ==============================================================================
set(KERNEL_ELF_LOADER_SOURCES
    mm/elf_loader.c
)

# ==============================================================================
# 合并所有源文件
# ==============================================================================
set(KERNEL_SOURCES
    ${KERNEL_CORE_SOURCES}
    ${KERNEL_ARCH_SOURCES}
    ${KERNEL_SCHED_SOURCES}
    ${KERNEL_IPC_SOURCES}
    ${KERNEL_CAP_SOURCES}
    ${KERNEL_MM_SOURCES}
    ${KERNEL_IRQ_SOURCES}
    ${KERNEL_ELF_LOADER_SOURCES}
    ${CMAKE_SOURCE_DIR}/lib/kernel_string.c
)

# 分离 ASM 文件
set(KERNEL_ASM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/arch/arm64/boot.S
    ${CMAKE_CURRENT_SOURCE_DIR}/arch/arm64/exception.S
    ${CMAKE_CURRENT_SOURCE_DIR}/arch/arm64/context.S
)

# 分离 C 文件
list(REMOVE_ITEM KERNEL_SOURCES ${KERNEL_ASM_SOURCES})

# ==============================================================================
# ARM64 内核编译选项
# ==============================================================================
# 为 C 文件设置编译选项
set(CMAKE_C_FLAGS "-Os -mgeneral-regs-only -ffreestanding -mno-outline-atomics -ffunction-sections -fdata-sections")

# 为 ASM 文件设置编译选项（汇编器不支持 -D 预处理器选项）
set(CMAKE_ASM_FLAGS "")

# 为 Release 模式设置优化标志
set(CMAKE_C_FLAGS_RELEASE "-Os -DNDEBUG")
set(CMAKE_ASM_FLAGS_RELEASE "")

# ==============================================================================
# 构建内核静态库
# ==============================================================================
add_library(kernel_static STATIC ${KERNEL_SOURCES} ${KERNEL_ASM_SOURCES})

# ==============================================================================
# 内核可执行文件
# ==============================================================================
add_executable(aisafe64.elf ${KERNEL_SOURCES} ${KERNEL_ASM_SOURCES})

target_link_options(aisafe64.elf PRIVATE
    -nostartfiles
    -nostdlib
    -static
    -T ${CMAKE_SOURCE_DIR}/lds/kernel.ld
    -Wl,--gc-sections
    -Wl,--version-script=${CMAKE_SOURCE_DIR}/lds/kernel.exports
    -Wl,-Map=${CMAKE_BINARY_DIR}/aisafe64.map
    -Wl,--no-warn-rwx-segments
    -Wl,--no-warn-mismatch
    -lgcc
)

# ==============================================================================
# 生成辅助文件
# ==============================================================================

# 生成二进制文件
if(CMAKE_OBJCOPY)
    add_custom_command(TARGET aisafe64.elf POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:aisafe64.elf>
                ${CMAKE_BINARY_DIR}/aisafe64.bin
        COMMENT "生成裸机二进制文件 aisafe64.bin"
    )
endif()

# 生成反汇编文件
if(CMAKE_OBJDUMP)
    add_custom_command(TARGET aisafe64.elf POST_BUILD
        COMMAND ${CMAKE_OBJDUMP} -d -S $<TARGET_FILE:aisafe64.elf>
                > ${CMAKE_BINARY_DIR}/aisafe64.dis
        COMMENT "生成反汇编文件 aisafe64.dis"
    )
endif()

# 查看大小
if(CMAKE_SIZE)
    add_custom_command(TARGET aisafe64.elf POST_BUILD
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:aisafe64.elf>
        COMMENT "查看内核映像大小"
    )
endif()
