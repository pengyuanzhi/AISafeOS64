#!/bin/bash
# QEMU 网络启动脚本
# 用于测试 AISafeOS64 网络协议栈

set -e

# 项目根目录
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

# 检查编译产物
if [ ! -f "${BUILD_DIR}/aisafe64.bin" ]; then
    echo "错误：找不到 aisafe64.bin"
    echo "请先运行：cd ${BUILD_DIR} && make aisafe64.elf"
    exit 1
fi

if [ ! -f "${BUILD_DIR}/services/net.elf.elf" ]; then
    echo "错误：找不到 net.elf.elf"
    echo "请先运行：cd ${BUILD_DIR} && make net.elf"
    exit 1
fi

if [ ! -f "${BUILD_DIR}/services/drv_virtio_net.elf.elf" ]; then
    echo "错误：找不到 drv_virtio_net.elf.elf"
    echo "请先运行：cd ${BUILD_DIR} && make drv_virtio_net.elf"
    exit 1
fi

# QEMU 参数
QEMU="qemu-system-aarch64"
MACHINE="-M virt"
CPU="-cpu cortex-a57"
SMP="-smp 4"
MEMORY="-m 1G"
KERNEL="-kernel ${BUILD_DIR}/aisafe64.bin"
DISK="-drive file=${BUILD_DIR}/disk.img,format=raw,if=none,id=hd0 -device virtio-blk-device,drive=hd0"
NET_DEVICE="-netdev user,id=net0,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:80 -device virtio-net-device,netdev=net0"
NOGRAPHIC="-nographic"
SERIAL="-serial mon:stdio"

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

# 启动 QEMU
cd "${BUILD_DIR}"
exec ${QEMU} ${MACHINE} ${CPU} ${SMP} ${MEMORY} ${KERNEL} ${DISK} ${NET_DEVICE} ${NOGRAPHIC} ${SERIAL}
