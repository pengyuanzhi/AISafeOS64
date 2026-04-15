# lib/musl_aisafe/CMakeLists.txt
# AISafeOS64 musl 适配层构建配置
#
# 编译标准 musl 上游源码 + AISafeOS64 适配层，生成 libmusl_aisafe.a 静态库。
# 参考 seL4/musllibc 方案：musl 源码零修改，通过 arch 覆盖适配。

cmake_minimum_required(VERSION 3.20)

# 禁止重复 project() 命令（工具链文件中已设置）
# project(musl_aisafe C ASM)

# ==============================================================================
# 路径定义
# ==============================================================================
set(MUSL_UPSTREAM  ${CMAKE_CURRENT_SOURCE_DIR}/../musl_upstream)
set(MUSL_ASAFE     ${CMAKE_CURRENT_SOURCE_DIR})
set(GEN_DIR        ${CMAKE_CURRENT_BINARY_DIR}/generated)

file(MAKE_DIRECTORY ${GEN_DIR}/bits ${GEN_DIR}/src/internal)

# ==============================================================================
# 编译选项（与 musl Makefile 对齐）
# ==============================================================================
set(MUSL_CFLAGS
    -std=c99 -ffreestanding -nostdinc -fno-builtin
    -fno-strict-aliasing -fno-stack-protector -fno-common
    -Os -pipe -D_XOPEN_SOURCE=700
    -Wno-all -Wno-extra -Wno-error
)

# ==============================================================================
# 头文件搜索路径（优先级从高到低）
# ==============================================================================
set(MUSL_INCS
    ${MUSL_ASAFE}/include
    ${MUSL_ASAFE}/arch/aarch64_aisafe
    ${MUSL_UPSTREAM}/arch/generic
    ${GEN_DIR}/src/internal
    ${MUSL_UPSTREAM}/src/include
    ${MUSL_UPSTREAM}/src/internal
    ${GEN_DIR}
    ${MUSL_UPSTREAM}/include
)

# ==============================================================================
# 生成头文件
# ==============================================================================
add_custom_command(
    OUTPUT ${GEN_DIR}/bits/alltypes.h
    COMMAND sed -f ${MUSL_UPSTREAM}/tools/mkalltypes.sed
            ${MUSL_ASAFE}/arch/aarch64_aisafe/bits/alltypes.h.in
            ${MUSL_UPSTREAM}/include/alltypes.h.in
            > ${GEN_DIR}/bits/alltypes.h
    DEPENDS
        ${MUSL_ASAFE}/arch/aarch64_aisafe/bits/alltypes.h.in
        ${MUSL_UPSTREAM}/include/alltypes.h.in
        ${MUSL_UPSTREAM}/tools/mkalltypes.sed
)
add_custom_target(gen_headers ALL DEPENDS ${GEN_DIR}/bits/alltypes.h)

file(WRITE ${GEN_DIR}/src/internal/version.h "#define VERSION \"1.2.5\"\n")

# ==============================================================================
# 源文件收集
#
# 策略：白名单方式，只收集我们需要的模块目录。
# ==============================================================================

# Phase 1 需要的 musl 模块
set(MUSL_MODULES
    internal
    string
    stdlib
    stdio
    errno
    ctype
    exit
    signal
    thread
    time
    setjmp
    fenv
    process
    unistd
    fcntl
    stat
    mman
    prng
    env
    conf
    multibyte
    search
    sched
    math
    locale
)

# 收集模块基础 .c 文件
set(_BASE_SRCS "")
foreach(mod ${MUSL_MODULES})
    file(GLOB _mod_c ${MUSL_UPSTREAM}/src/${mod}/*.c)
    list(APPEND _BASE_SRCS ${_mod_c})
endforeach()

# 收集 aarch64 架构特定文件（.c .s .S）
set(_ARCH_SRCS "")
foreach(mod ${MUSL_MODULES})
    file(GLOB _mod_arch
        ${MUSL_UPSTREAM}/src/${mod}/aarch64/*.c
        ${MUSL_UPSTREAM}/src/${mod}/aarch64/*.s
        ${MUSL_UPSTREAM}/src/${mod}/aarch64/*.S
    )
    list(APPEND _ARCH_SRCS ${_mod_arch})
endforeach()

# malloc 模块（使用 lite_malloc，不用 mallocng）
file(GLOB _MALLOC_SRCS
    ${MUSL_UPSTREAM}/src/malloc/calloc.c
    ${MUSL_UPSTREAM}/src/malloc/free.c
    ${MUSL_UPSTREAM}/src/malloc/libc_calloc.c
    ${MUSL_UPSTREAM}/src/malloc/lite_malloc.c
    ${MUSL_UPSTREAM}/src/malloc/memalign.c
    ${MUSL_UPSTREAM}/src/malloc/posix_memalign.c
    ${MUSL_UPSTREAM}/src/malloc/realloc.c
    ${MUSL_UPSTREAM}/src/malloc/reallocarray.c
    ${MUSL_UPSTREAM}/src/malloc/replaced.c
)

# ldso aarch64 汇编
file(GLOB _LDSO_ARCH ${MUSL_UPSTREAM}/src/ldso/aarch64/*.s)

# AISafeOS64 适配层
set(_AISAFE_SRCS
    ${MUSL_ASAFE}/src/syscall_dispatch.c
    ${MUSL_ASAFE}/src/musl_safety.c
    ${MUSL_ASAFE}/src/boot.c
    ${MUSL_ASAFE}/src/tls.c
)

# ==============================================================================
# 架构覆盖处理
# 当 aarch64 版本存在时，移除对应的基础版本
# ==============================================================================
set(_ARCH_NAMES "")
foreach(f ${_ARCH_SRCS})
    get_filename_component(_n ${f} NAME_WE)
    list(APPEND _ARCH_NAMES ${_n})
endforeach()

set(_FILTERED_BASE "")
foreach(f ${_BASE_SRCS})
    get_filename_component(_n ${f} NAME_WE)
    list(FIND _ARCH_NAMES ${_n} _idx)
    if(_idx LESS 0)
        list(APPEND _FILTERED_BASE ${f})
    endif()
endforeach()

# ==============================================================================
# 黑名单：排除需要 Linux 内核完整支持的文件
# ==============================================================================

# 需要排除的文件名模式（线程/进程相关，依赖 Linux clone/fork）
set(_EXCLUDE_PATTERNS
    "pthread_create" "pthread_join" "pthread_detach"
    "pthread_cancel" "pthread_kill" "pthread_atfork"
    "pthread_getattr_np" "pthread_setname" "pthread_getname"
    "pthread_getcpuclockid"
    "synccall"
    "__unmapself" "__set_thread_area" "__syscall_cp"
    "syscall_cp" "__timedwait" "__wait"
    "vmlock" "__tls_get_addr" "tls"
    "_Fork" "fork" "vfork" "execve" "fexecve"
    "posix_spawn" "system" "wait" "sigaction" "restore"
    "vdso" "emulate_wait4" "defsysinfo"
)

set(_ALL_SRCS
    ${_FILTERED_BASE}
    ${_ARCH_SRCS}
    ${_LDSO_ARCH}
    ${_MALLOC_SRCS}
    ${_AISAFE_SRCS}
)

# 应用黑名单
set(_FINAL_SRCS "")
foreach(f ${_ALL_SRCS})
    get_filename_component(_fname ${f} NAME_WE)
    set(_skip FALSE)
    foreach(p ${_EXCLUDE_PATTERNS})
        string(FIND ${_fname} ${p} _pos)
        if(NOT _pos LESS 0)
            set(_skip TRUE)
            break()
        endif()
    endforeach()
    if(NOT _skip)
        list(APPEND _FINAL_SRCS ${f})
    endif()
endforeach()

# ==============================================================================
# 构建静态库
# ==============================================================================
add_library(musl_aisafe STATIC ${_FINAL_SRCS})

target_include_directories(musl_aisafe PRIVATE ${MUSL_INCS})
target_compile_options(musl_aisafe PRIVATE ${MUSL_CFLAGS})
add_dependencies(musl_aisafe gen_headers)

list(LENGTH _FINAL_SRCS _cnt)
message(STATUS "musl_aisafe: ${_cnt} 个源文件")
