#!/bin/bash
# QEMU VirtIO Block 测试脚本

cd /home/kerfs/AISafeOS64/AISafeOS64

echo "========================================"  
echo "🚀 启动 QEMU + VirtIO Block 设备"
echo "========================================"  
echo ""

# 创建临时磁盘镜像
if [ ! -f /tmp/test_disk.img ]; then
    echo "📝 创建测试磁盘镜像..."
    dd if=/dev/zero of=/tmp/test_disk.img bs=1M count=64
fi

echo "🔧 启动 QEMU（带 VirtIO Block 设备）..."
timeout 30 qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a57 \
    -smp 4 \
    -m 1G \
    -kernel build/aisafe64.bin \
    -nographic \
    -serial mon:stdio \
    -drive file=/tmp/test_disk.img,if=none,id=hd0,format=raw \
    -device virtio-blk-device,drive=hd0 \
    2>&1 | tee logs/qemu_blk_test_$(date +%Y%m%d_%H%M%S).log

echo ""
echo "✅ QEMU 已终止"
