#!/bin/bash
# QEMU 网络协议栈集成测试启动脚本
# @version 1.0
# @date 2026-04-20

set -e

# 配置
QEMU="qemu-system-aarch64"
QEMU_ARGS=(
    -M virt
    -cpu cortex-a57
    -smp 4
    -m 1G
    -nographic
    -serial mon:stdio
    -kernel build/aisafe64.elf
    -drive file=build/disk.img,format=raw,if=none,id=hd0
    -device virtio-blk-device,drive=hd0
    -netdev user,id=net0,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:80
    -device virtio-net-device,netdev=net0
)

# 打印配置
echo "=========================================="
echo "QEMU 网络协议栈集成测试"
echo "=========================================="
echo "CPU: cortex-a57"
echo "SMP: 4 核"
echo "内存: 1GB"
echo "网络: 用户模式网络（User Networking）"
echo "端口转发: 2222->SSH, 8080->HTTP"
echo "=========================================="
echo ""

# 启动 QEMU
echo "[INFO] 启动 QEMU..."
echo "[INFO] 按 Ctrl+A 然后 X 退出"
echo ""

${QEMU} "${QEMU_ARGS[@]}"
