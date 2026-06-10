#!/bin/bash
# AISafeOS64 立即开始自动开发

set -e

echo "🚀 AISafeOS64 立即开始自动开发"
echo "================================"
echo ""

# 检查编译状态
echo "📦 检查编译状态..."
cd /home/kerfs/AISafeOS64/AISafeOS64
mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm64.cmake > /dev/null 2>&1 || true

ERROR_COUNT=$(make -j$(nproc) 2>&1 | grep -c "error:" || echo "0")
echo "- 编译错误：$ERROR_COUNT 个"

if [ $ERROR_COUNT -gt 0 ]; then
    echo ""
    echo "⚠️ 当前有编译错误，建议先修复"
    echo ""
    echo "是否继续？(y/n)"
    read -r answer
    if [ "$answer" != "y" ]; then
        echo "❌ 已取消"
        exit 0
    fi
fi

cd /home/kerfs/AISafeOS64/AISafeOS64

# 运行自动开发脚本
echo ""
echo "🤖 启动自动开发..."
echo ""

./scripts/auto_dev.sh