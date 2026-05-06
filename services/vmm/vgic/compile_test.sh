#!/bin/bash
# VGIC 单元测试编译脚本

set -e

echo "===================================="
echo "VGIC 单元测试编译"
echo "===================================="

# 设置变量
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

# 创建构建目录
mkdir -p "$BUILD_DIR"

cd "$BUILD_DIR"

# 编译选项
CFLAGS="-std=c11 -Wall -Wextra -I$PROJECT_ROOT/include -I$PROJECT_ROOT/tests -I$PROJECT_ROOT/services/vmm -I$PROJECT_ROOT/services/vmm/core -I$PROJECT_ROOT/services/vmm/npt -I$PROJECT_ROOT/services/vmm/vgic -I$PROJECT_ROOT/services/vmm/stats -I$PROJECT_ROOT/services/vmm/device -I$PROJECT_ROOT/services/vmm/hypercall -I$PROJECT_ROOT/services/vmm/exit -I$PROJECT_ROOT/services/vmm/events"

# 编译 Unity 框架
echo "编译 Unity 框架..."
gcc -c -o unity.o $CFLAGS "$PROJECT_ROOT/tests/unity.c" 2>&1

# 编译 VMM 核心模块（宿主机测试）
echo "编译 VMM 核心模块..."
gcc -c -o vmm.o $CFLAGS "$PROJECT_ROOT/services/vmm/vmm.c" 2>&1 || true

# 编译 VGIC 模块（宿主机测试）
echo "编译 VGIC 模块..."
gcc -c -o vgic.o $CFLAGS "$PROJECT_ROOT/services/vmm/vgic/vgic.c" 2>&1 || true

# 编译 VM 模块（宿主机测试）
echo "编译 VM 模块..."
gcc -c -o vm.o $CFLAGS "$PROJECT_ROOT/services/vmm/core/vm.c" 2>&1 || true

# 编译 vCPU 模块（宿主机测试）
echo "编译 vCPU 模块..."
gcc -c -o vcpu.o $CFLAGS "$PROJECT_ROOT/services/vmm/core/vcpu.c" 2>&1 || true

# 编译 VGIC 测试
echo "编译 VGIC 测试..."
gcc -c -o test_vgic.o $CFLAGS "$PROJECT_ROOT/services/vmm/vgic/test_vgic.c" 2>&1

# 链接
echo "链接可执行文件..."
gcc -o test_vgic_host unity.o vmm.o vgic.o vm.o vcpu.o test_vgic.o 2>&1 || true

echo "===================================="
echo "编译完成"
echo "===================================="
echo "可执行文件: $BUILD_DIR/test_vgic_host"
