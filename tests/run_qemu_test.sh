#!/bin/bash
# QEMU 文件锁分片锁性能测试脚本
# @author  AISafe64 Team
# @date    2026-05-09

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 脚本目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_DIR"

echo "=========================================="
echo "QEMU 文件锁分片锁性能测试"
echo "=========================================="
echo "项目目录: $PROJECT_DIR"
echo ""

# 检查 QEMU
if ! command -v qemu-system-aarch64 &> /dev/null; then
    echo -e "${RED}错误: 未找到 qemu-system-aarch64${NC}"
    echo "请安装 QEMU: sudo apt-get install qemu-system-arm"
    exit 1
fi

# 检查内核文件
if [ ! -f "build/aisafe64.elf" ]; then
    echo -e "${RED}错误: 未找到内核文件 build/aisafe64.elf${NC}"
    echo "请先编译内核: cd build && make aisafe64.elf"
    exit 1
fi

echo -e "${GREEN}[1/2] 检查 QEMU 环境...${NC}"
echo "QEMU 版本: $(qemu-system-aarch64 --version | head -1)"
echo "内核文件: build/aisafe64.elf"
echo "内核大小: $(du -h build/aisafe64.elf | cut -f1)"
echo -e "${GREEN}✓ QEMU 环境检查完成${NC}"

echo ""
echo -e "${GREEN}[2/2] 运行 QEMU 测试...${NC}"
echo ""
echo -e "${YELLOW}提示: 按 Ctrl+A 然后按 X 退出 QEMU${NC}"
echo ""

# 运行 QEMU
qemu-system-aarch64 \
  -M virt \
  -cpu cortex-a57 \
  -smp 4 \
  -m 1G \
  -kernel build/aisafe64.elf \
  -nographic \
  -serial mon:stdio \
  -d guest_errors \
  -d unimp \
  -d int \
  2>&1 | tee qemu_output.log

echo ""
echo -e "${GREEN}✓ QEMU 测试完成${NC}"
echo ""
echo -e "${YELLOW}QEMU 输出已保存到: qemu_output.log${NC}"
echo ""

# 分析输出
echo "=========================================="
echo "测试结果分析"
echo "=========================================="

if [ -f "qemu_output.log" ]; then
    echo "文件锁分片锁测试输出:"
    grep -i "file\|lock\|flock" qemu_output.log || echo "未找到文件锁相关输出"
    echo ""
    echo "性能指标:"
    grep -i "throughput\|ops/sec\|performance" qemu_output.log || echo "未找到性能相关输出"
    echo ""
    echo "错误信息:"
    grep -i "error\|fail\|segmentation\|panic" qemu_output.log || echo "未找到错误信息"
else
    echo "未找到 QEMU 输出文件"
fi

echo ""
echo -e "${YELLOW}提示: 查看 qemu_output.log 获取完整输出${NC}"
