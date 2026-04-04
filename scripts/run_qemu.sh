#!/bin/bash
# scripts/run_qemu.sh
# AISafeOS64 QEMU 快速运行脚本
# @version 1.0
# @date 2026-04-04
#
# 用法:
#   ./scripts/run_qemu.sh          # 默认运行（单核，1GB RAM）
#   ./scripts/run_qemu.sh -smp 4   # 4 核运行
#   ./scripts/run_qemu.sh -m 2048  # 2GB RAM

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_DIR}/build"

# 默认参数
SMP=1
MEMORY=1024
CPU="cortex-a57"
MACHINE="virt"

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -smp)
            SMP="$2"
            shift 2
            ;;
        -m)
            MEMORY="$2"
            shift 2
            ;;
        *)
            echo "未知参数: $1"
            echo "用法: $0 [-smp <核数>] [-m <内存MB>]"
            exit 1
            ;;
    esac
done

KERNEL_ELF="${BUILD_DIR}/kernel/aisafe64.elf.elf"

# 检查内核二进制文件
if [ ! -f "${KERNEL_ELF}" ]; then
    echo "[run] 内核 ELF 不存在，正在构建..."
    cd "${BUILD_DIR}" 2>/dev/null || mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"
    cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm64.cmake
    make -j"$(nproc)"
fi

echo "========================================"
echo "  AISafeOS64 QEMU 启动"
echo "  CPU: ${CPU} x${SMP}"
echo "  内存: ${MEMORY}MB"
echo "========================================"

# 运行 QEMU（使用 ELF 格式以便 QEMU 正确加载到入口地址）
qemu-system-aarch64 \
    -M "${MACHINE}" \
    -cpu "${CPU}" \
    -smp "${SMP}" \
    -m "${MEMORY}" \
    -nographic \
    -kernel "${KERNEL_ELF}"

echo ""
echo "[run] QEMU 已退出"
