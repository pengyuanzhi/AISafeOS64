#!/bin/bash
# 手动启动网络协议栈服务的 QEMU 脚本

cd /home/kerfs/AISafeOS64/AISafeOS64

echo "========================================"  
echo "🚀 手动启动网络协议栈服务"
echo "========================================"  
echo ""

# 创建 QEMU monitor 命令文件
cat > /tmp/qemu_commands.txt << 'EOF'
# 暂停 CPU
stop

# 加载 net 服务 ELF 文件
device_add loader,id=net_loader,addr=0x50000000,drive-net=on

# 恢复 CPU
cont
EOF

echo "🔧 启动 QEMU..."
timeout 10 qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a57 \
    -smp 4 \
    -m 1G \
    -kernel build/kernel/aisafe64.elf.elf \
    -nographic \
    -serial mon:stdio \
    -monitor stdio < /tmp/qemu_commands.txt 2>&1 | tee logs/manual_net_test_$(date +%Y%m%d_%H%M%S).log

echo ""
echo "✅ QEMU 已终止"
