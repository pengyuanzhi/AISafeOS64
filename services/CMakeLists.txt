# services/CMakeLists.txt
# AISafeOS64 用户态服务构建配置（musl_aisafe 迁移版）
#
# 服务迁移策略：
# - Phase 3: 从 lib/musl_legacy/ 迁移到标准 musl
# - 保留内核系统调用（<kernel/syscall.h>）
# - 使用 musl 字符串和内存管理
# - 每个服务编译为独立的 ELF 文件

if(1)  # 临时禁用 CMAKE_CROSSCOMPILING 检查
    # ================================================================
    # C 运行时（启动代码和初始化）
    # ================================================================
    add_subdirectory(crt)

    # ================================================================
    # 用户态服务列表（逐步迁移）
    # ================================================================
    set(USERLAND_SERVICES
        path
        init
        mem
        proc
        fs
        net
        drv_virtio_net
    )
    
    # ================================================================
    # 网络协议栈集成测试
    # ================================================================
    add_executable(test_net_integration.elf
        ${CMAKE_CURRENT_SOURCE_DIR}/test_net_integration.c
        ${CMAKE_CURRENT_SOURCE_DIR}/net/main.c
        ${CMAKE_CURRENT_SOURCE_DIR}/net/net_if/net_if.c
        ${CMAKE_CURRENT_SOURCE_DIR}/net/net_if/net_if_auto.c
        ${CMAKE_CURRENT_SOURCE_DIR}/net/net_if/ethernet.c
        # TCP/IP 优化功能
        ${CMAKE_CURRENT_SOURCE_DIR}/net/tcp_cong.c
        ${CMAKE_CURRENT_SOURCE_DIR}/net/tcp_nagle_sack.c
        ${CMAKE_CURRENT_SOURCE_DIR}/net/ip_reass.c
        ${CMAKE_CURRENT_SOURCE_DIR}/net/icmp_error.c
        ${CMAKE_CURRENT_SOURCE_DIR}/net/tcp_cubic.c
        ${CMAKE_CURRENT_SOURCE_DIR}/net/tcp_timestamp.c
        ${CMAKE_CURRENT_SOURCE_DIR}/net/tcp_keepalive.c
        ${CMAKE_CURRENT_SOURCE_DIR}/net/tcp_options.c
    )
    
    target_link_libraries(test_net_integration.elf musl_aisafe crt0)
    
    target_include_directories(test_net_integration.elf PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/lib/musl_aisafe/include
        ${CMAKE_CURRENT_SOURCE_DIR}/net/net_if
    )
    
    target_compile_options(test_net_integration.elf PRIVATE
        -std=c11
        -Wall
        -Wextra
        -ffreestanding
        -fno-builtin
        -fno-common
        -fno-stack-protector
        -nostdlib
        -nostartfiles
        -U_FORTIFY_SOURCE
        -D_FORTIFY_SOURCE=0
        -O2
        -fno-asynchronous-unwind-tables
    )
    
    target_link_options(test_net_integration.elf PRIVATE
        -nostdlib
        -nostartfiles
        -static
        -Wl,--gc-sections
        -Wl,--no-warn-rwx-segments
        -Wl,--allow-multiple-definition
        -Wl,--start-group
        -lgcc
        -Wl,--end-group
    )
    
    # ================================================================
    # POSIX API 集成测试
    # ================================================================
    add_executable(test_posix_api.elf
        ${CMAKE_CURRENT_SOURCE_DIR}/test_posix_api.c
    )
    
    target_link_libraries(test_posix_api.elf musl_aisafe crt0)
    
    target_include_directories(test_posix_api.elf PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/lib/musl_aisafe/include
    )
    
    target_compile_options(test_posix_api.elf PRIVATE
        -std=c11
        -Wall
        -Wextra
        -ffreestanding
        -fno-builtin
        -fno-common
        -fno-stack-protector
        -nostdlib
        -nostartfiles
        -U_FORTIFY_SOURCE
        -D_FORTIFY_SOURCE=0
        -O2
        -fno-asynchronous-unwind-tables
    )
    
    target_link_options(test_posix_api.elf PRIVATE
        -nostdlib
        -nostartfiles
        -static
        -Wl,--gc-sections
        -Wl,--no-warn-rwx-segments
        -Wl,--start-group
        -lgcc
        -Wl,--end-group
    )
    
    # ================================================================
    # FS IPC 接口测试
    # ================================================================
    add_executable(test_fs_ipc.elf
        ${CMAKE_CURRENT_SOURCE_DIR}/../tests/test_fs_ipc.c
        ${CMAKE_CURRENT_SOURCE_DIR}/fs/fs_ipc/fs_ipc.c
        ${CMAKE_CURRENT_SOURCE_DIR}/fs/fs_ops/fs_ops.c
        ${CMAKE_CURRENT_SOURCE_DIR}/fs/fs_ramfs/ramfs.c
    )
    
    target_link_libraries(test_fs_ipc.elf musl_aisafe crt0)
    
    target_include_directories(test_fs_ipc.elf PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/lib/musl_aisafe/include
        ${CMAKE_CURRENT_SOURCE_DIR}/fs/fs_ipc
        ${CMAKE_CURRENT_SOURCE_DIR}/fs/fs_ops
        ${CMAKE_CURRENT_SOURCE_DIR}/fs/fs_ramfs
        ${CMAKE_CURRENT_SOURCE_DIR}/fs
    )
    
    target_compile_options(test_fs_ipc.elf PRIVATE
        -std=c11
        -Wall
        -Wextra
        -ffreestanding
        -fno-builtin
        -fno-common
        -fno-stack-protector
        -nostdlib
        -nostartfiles
        -U_FORTIFY_SOURCE
        -D_FORTIFY_SOURCE=0
        -O2
        -fno-asynchronous-unwind-tables
    )
    
    target_link_options(test_fs_ipc.elf PRIVATE
        -nostdlib
        -nostartfiles
        -static
        -Wl,--gc-sections
        -Wl,--no-warn-rwx-segments
        -Wl,--allow-multiple-definition
        -Wl,--start-group
        -lgcc
        -Wl,--end-group
    )
    
    # ================================================================
    # 网络协议栈与上层服务集成测试
    # ================================================================
    add_executable(test_net_integration_services.elf
        ${CMAKE_CURRENT_SOURCE_DIR}/test_net_integration_services.c
        ${CMAKE_CURRENT_SOURCE_DIR}/net/main.c
        ${CMAKE_CURRENT_SOURCE_DIR}/net/net_if/net_if.c
        ${CMAKE_CURRENT_SOURCE_DIR}/net/net_if/net_if_auto.c
        ${CMAKE_CURRENT_SOURCE_DIR}/net/net_if/ethernet.c
        # TCP/IP 优化功能
        ${CMAKE_CURRENT_SOURCE_DIR}/net/tcp_cong.c
        ${CMAKE_CURRENT_SOURCE_DIR}/net/tcp_nagle_sack.c
        ${CMAKE_CURRENT_SOURCE_DIR}/net/ip_reass.c
        ${CMAKE_CURRENT_SOURCE_DIR}/net/icmp_error.c
        ${CMAKE_CURRENT_SOURCE_DIR}/net/tcp_cubic.c
        ${CMAKE_CURRENT_SOURCE_DIR}/net/tcp_timestamp.c
        ${CMAKE_CURRENT_SOURCE_DIR}/net/tcp_keepalive.c
        ${CMAKE_CURRENT_SOURCE_DIR}/net/tcp_options.c
    )
    
    target_link_libraries(test_net_integration_services.elf musl_aisafe crt0)
    
    target_include_directories(test_net_integration_services.elf PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/lib/musl_aisafe/include
        ${CMAKE_CURRENT_SOURCE_DIR}/net/net_if
    )
    
    target_compile_options(test_net_integration_services.elf PRIVATE
        -std=c11
        -Wall
        -Wextra
        -ffreestanding
        -fno-builtin
        -fno-common
        -fno-stack-protector
        -nostdlib
        -nostartfiles
        -U_FORTIFY_SOURCE
        -D_FORTIFY_SOURCE=0
        -O2
        -fno-asynchronous-unwind-tables
    )
    
    target_link_options(test_net_integration_services.elf PRIVATE
        -nostdlib
        -nostartfiles
        -static
        -Wl,--gc-sections
        -Wl,--no-warn-rwx-segments
        -Wl,--allow-multiple-definition
        -Wl,--start-group
        -lgcc
        -Wl,--end-group
    )

    # ================================================================
    # 编译用户态服务（musl_aisafe 版本）
    # ================================================================
    foreach(SERVICE_NAME IN LISTS USERLAND_SERVICES)
        if(SERVICE_NAME STREQUAL "net")
            # 网络协议栈：包含网络接口层 + TCP/IP 优化功能
            add_executable(${SERVICE_NAME}.elf
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/main.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/net_if/net_if.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/net_if/net_if_auto.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/net_if/ethernet.c
                # 第一阶段 TCP/IP 优化功能
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/tcp_cong.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/tcp_nagle_sack.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/ip_reass.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/icmp_error.c
                # 第二阶段 P0 优先级：TCP 拥塞控制（CUBIC）+ TCP 时间戳
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/tcp_cubic.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/tcp_timestamp.c
                # 第二阶段 P1 优先级：TCP Keepalive + TCP 选项处理
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/tcp_keepalive.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/tcp_options.c
            )
        elseif(SERVICE_NAME STREQUAL "fs")
            # FS 服务：包含 IPC 客户端 + IPC 服务器端 + 文件系统抽象层
            add_executable(${SERVICE_NAME}.elf
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/main.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/fs_ipc/fs_ipc.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/fs_server_ipc.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/fs_ops/fs_ops.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/fs_ops/fs_inode_cache.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/fs_ops/ticket_lock_user.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/fs_page_cache.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/fs_ramfs/ramfs.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/fs_ramfs/ramfs_hash.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/fs_romfs/romfs.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/fs_fat32/fat32.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/fs_fat32/fat32_bpb.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/fs_fat32/fat32_cache.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/fs_fat32/fat32_dir.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/fs_fat32/fat32_fat.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/fs_fat32/fat32_file.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/fs_fat32/fat32_ops.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/fs_fat32/fat32_path.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/fs_devfs/devfs.c
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/fs_ext4/ext4_ops.c
            )
        elseif(SERVICE_NAME STREQUAL "drv_virtio_net")
            # VirtIO Net 驱动：包含网络接口层
            add_executable(${SERVICE_NAME}.elf
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/main.c
                ${CMAKE_CURRENT_SOURCE_DIR}/net/net_if/net_if.c
                ${CMAKE_CURRENT_SOURCE_DIR}/net/net_if/net_if_auto.c
                ${CMAKE_CURRENT_SOURCE_DIR}/net/net_if/ethernet.c
            )
        else()
            # 普通服务：只有一个 main.c 文件
            add_executable(${SERVICE_NAME}.elf
                ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME}/main.c
            )
        endif()

        target_link_libraries(${SERVICE_NAME}.elf musl_aisafe crt0)

        target_include_directories(${SERVICE_NAME}.elf PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/lib/musl_aisafe/include
            ${CMAKE_CURRENT_SOURCE_DIR}/net/net_if
            ${CMAKE_CURRENT_SOURCE_DIR}/fs/fs_ipc
            ${CMAKE_CURRENT_SOURCE_DIR}/fs/fs_ops
            ${CMAKE_CURRENT_SOURCE_DIR}/fs/fs_ramfs
            ${CMAKE_CURRENT_SOURCE_DIR}/fs/fs_romfs
            ${CMAKE_CURRENT_SOURCE_DIR}/fs/fs_fat32
            ${CMAKE_CURRENT_SOURCE_DIR}/fs/fs_ext4
            ${CMAKE_CURRENT_SOURCE_DIR}/fs/fs_devfs
            ${CMAKE_CURRENT_SOURCE_DIR}/fs
        )

        target_compile_options(${SERVICE_NAME}.elf PRIVATE
            -std=c11
            -Wall
            -Wextra
            -ffreestanding
            -fno-builtin
            -fno-common
            -fno-stack-protector
            -nostdlib
            -nostartfiles
            -U_FORTIFY_SOURCE
            -D_FORTIFY_SOURCE=0
            -O2
            -fno-asynchronous-unwind-tables
            -mno-outline-atomics
        )

        target_link_options(${SERVICE_NAME}.elf PRIVATE
            -nostdlib
            -nostartfiles
            -static
            -Wl,--gc-sections
            -Wl,--no-warn-rwx-segments
            -Wl,--allow-multiple-definition
            -Wl,--start-group
            -lgcc
            -Wl,--end-group
        )
    endforeach()

    # ================================================================
    # 测试程序（验证启动代码和 C 运行时）
    # ================================================================
    add_executable(hello_start.elf hello_start.c)

    target_link_libraries(hello_start.elf musl_aisafe crt0)

    target_include_directories(hello_start.elf PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/lib/musl_aisafe/include
    )

    target_compile_options(hello_start.elf PRIVATE
        -std=c11
        -Wall
        -Wextra
        -ffreestanding
        -fno-builtin
        -fno-common
        -fno-stack-protector
        -nostdlib
        -nostartfiles
        -U_FORTIFY_SOURCE
        -D_FORTIFY_SOURCE=0
        -O2
    )

    target_link_options(hello_start.elf PRIVATE
        -nostdlib
        -nostartfiles
        -static
        -Wl,--gc-sections
        -Wl,--no-warn-rwx-segments
    )

    # 生成磁盘映像：将 hello_start.elf 嵌入磁盘扇区
    # 布局: 扇区 0 = ELF 文件, 以扇区(512B)对齐
    add_custom_command(TARGET hello_start.elf POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "创建磁盘映像..."
        COMMAND dd if=/dev/zero of=${CMAKE_BINARY_DIR}/disk.img bs=512 count=128 2>/dev/null
        COMMAND dd if=$<TARGET_FILE:hello_start.elf> of=${CMAKE_BINARY_DIR}/disk.img bs=512 conv=notrunc 2>/dev/null
        COMMENT "创建 disk.img (64KB, 包含 hello_start.elf)"
    )

    # ================================================================
    # 用户态 virtio-blk 驱动（独立 ELF）
    # ================================================================
    add_subdirectory(dev)
endif()
