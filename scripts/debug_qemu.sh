#!/bin/bash
# scripts/debug_qemu.sh
# AISafeOS64 QEMU GDB 调试脚本
# @version 1.0
# @date 2026-04-04
#
# 用法:
#   ./scripts/debug_qemu.sh              # 启动 QEMU 等待 GDB 连接
#   ./scripts/debug_qemu.sh -g           # 启动 QEMU + GDB 自动连接
#   ./scripts/debug_qemu.sh -smp 4       # 4 核调试
#
# GDB 连接:
#   (gdb) target remote :1234
#   (gdb) file build/aisafe64.elf
#   (gdb) break kernel_main
#   (gdb) continue

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_DIR}/build"

# 默认参数
SMP=1
MEMORY=1024
CPU="cortex-a57"
MACHINE="virt"
GDB_PORT=1234
START_GDB=false

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -g|--gdb)
            START_GDB=true
            shift
            ;;
        -smp)
            SMP="$2"
            shift 2
            ;;
        -m)
            MEMORY="$2"
            shift 2
            ;;
        -p|--port)
            GDB_PORT="$2"
            shift 2
            ;;
        *)
            echo "未知参数: $1"
            echo "用法: $0 [-g] [-smp <核数>] [-m <内存MB>] [-p <GDB端口>]"
            exit 1
            ;;
    esac
done

# 检查内核二进制文件
if [ ! -f "${BUILD_DIR}/aisafe64.elf" ]; then
    echo "[debug] 内核 ELF 文件不存在，正在构建..."
    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"
    cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm64.cmake
    make -j"$(nproc)"
fi

# 检查 GDB
if [ "${START_GDB}" = true ]; then
    if ! command -v aarch64-none-elf-gdb &> /dev/null && \
       ! command -v aarch64-linux-gnu-gdb &> /dev/null && \
       ! command -v gdb-multiarch &> /dev/null; then
        echo "[debug] 错误: 未找到 GDB 调试器"
        echo "  请安装以下任一工具:"
        echo "    - aarch64-none-elf-gdb"
        echo "    - aarch64-linux-gnu-gdb"
        echo "    - gdb-multiarch"
        exit 1
    fi
fi

echo "========================================"
echo "  AISafeOS64 QEMU 调试模式"
echo "  CPU: ${CPU} x${SMP}"
echo "  内存: ${MEMORY}MB"
echo "  GDB 端口: ${GDB_PORT}"
echo "========================================"

# 启动 QEMU（后台运行，等待 GDB 连接）
QEMU_PID=""
cleanup() {
    if [ -n "${QEMU_PID}" ]; then
        kill "${QEMU_PID}" 2>/dev/null || true
    fi
    echo ""
    echo "[debug] QEMU 已停止"
}
trap cleanup EXIT INT TERM

qemu-system-aarch64 \
    -M "${MACHINE}" \
    -cpu "${CPU}" \
    -smp "${SMP}" \
    -m "${MEMORY}" \
    -nographic \
    -kernel "${BUILD_DIR}/aisafe64.elf" \
    -S -s -p "${GDB_PORT}" &
QEMU_PID=$!

echo "[debug] QEMU 已启动（PID: ${QEMU_PID}），等待 GDB 连接..."

if [ "${START_GDB}" = true ]; then
    echo "[debug] 启动 GDB..."
    sleep 1

    # 选择可用的 GDB
    if command -v aarch64-none-elf-gdb &> /dev/null; then
        GDB_CMD=aarch64-none-elf-gdb
    elif command -v aarch64-linux-gnu-gdb &> /dev/null; then
        GDB_CMD=aarch64-linux-gnu-gdb
    else
        GDB_CMD=gdb-multiarch
    fi

    echo "[debug] 使用 ${GDB_CMD}"

    ${GDB_CMD} \
        -ex "file ${BUILD_DIR}/aisafe64.elf" \
        -ex "target remote :${GDB_PORT}" \
        -ex "break kernel_main" \
        -ex "echo ======== AISafeOS64 GDB 调试会话 ========\\n"
else
    echo "[debug] 使用以下命令连接 GDB:"
    echo ""
    echo "  gdb-multiarch ${BUILD_DIR}/aisafe64.elf"
    echo "  (gdb) target remote :${GDB_PORT}"
    echo ""
    echo "  常用断点:"
    echo "    break kernel_main"
    echo "    break exception_sync_handler"
    echo "    break irq_handler"
    echo "    break scheduler_start"
    echo ""

    # 等待 QEMU 进程
    wait "${QEMU_PID}" 2>/dev/null || true
fi
