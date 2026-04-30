#!/bin/bash
# AISafeOS64 网络测试快速启动脚本

echo "=========================================="
echo "AISafeOS64 网络协议栈测试"
echo "=========================================="
echo ""
echo "编译项目..."
cd /home/kerfs/AISafeOS64/AISafeOS64/build
make -j4

echo ""
echo "编译完成！"
echo ""
echo "启动 QEMU（带网络支持）..."
echo "=========================================="
make qemu-net
