#!/bin/bash
# AISafeOS64 网络协议栈测试启动脚本

set -e

# 项目根目录
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

# 检查编译产物
if [ ! -f "${BUILD_DIR}/aisafe64.bin" ]; then
    echo "错误：找不到 aisafe64.bin"
    echo "请先运行：cd ${BUILD_DIR} && make aisafe64.bin"
    exit 1
fi

echo "=========================================="
echo "AISafeOS64 网络协议栈测试"
echo "=========================================="
echo "QEMU 参数："
echo "  CPU: cortex-a57"
echo "  SMP: 4 核"
echo "  Memory: 1GB"
echo "  Network: 用户模式网络（user networking）"
echo "  Port Forwarding:"
echo "    2222 -> 22 (SSH)"
echo "    8080 -> 80 (HTTP)"
echo "=========================================="
echo ""
echo "启动 QEMU..."
echo "=========================================="
echo ""

# 启动 QEMU（简化版，只启动内核）
cd "${BUILD_DIR}"
exec qemu-system-aarch64 \
  -M virt \
  -cpu cortex-a57 \
  -smp 4 \
  -m 1G \
  -nographic \
  -kernel ${BUILD_DIR}/aisafe64.bin \
  -drive file=${BUILD_DIR}/disk.img,format=raw,if=none,id=hd0 -device virtio-blk-device,drive=hd0 \
  -netdev user,id=net0,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:80 -device virtio-net-device,netdev=net0 \
  -serial mon:stdio
