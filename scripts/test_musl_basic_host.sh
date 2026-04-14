#!/bin/bash
# test_musl_basic_host.sh - musl AISafeOS64 基础模块验证（宿主机）
#
# 在宿主机上编译和运行测试，验证测试逻辑正确性。

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 项目目录
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build_host"

# 测试文件
TEST_SRC="${PROJECT_DIR}/tests/test_musl_basic.c"
TEST_BIN="${BUILD_DIR}/test_musl_basic"

echo "========================================"
echo "  musl AISafeOS64 基础模块验证（宿主机）"
echo "========================================"
echo ""

# 检查测试源文件
if [ ! -f "${TEST_SRC}" ]; then
    echo -e "${RED}错误: 测试源文件 ${TEST_SRC} 不存在${NC}"
    exit 1
fi

# 创建构建目录
mkdir -p "${BUILD_DIR}"

# 编译测试程序（使用宿主机的 libc）
echo "编译测试程序（使用 glibc）..."
gcc -o "${TEST_BIN}" "${TEST_SRC}" -std=c99 -Wall -Wextra

echo -e "${GREEN}✓ 编译成功${NC}"
echo ""

# 运行测试
echo "运行测试..."
echo ""
${TEST_BIN}

# 检查退出状态
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ 所有测试通过${NC}"
    exit 0
else
    echo -e "${RED}✗ 部分测试失败${NC}"
    exit 1
fi
