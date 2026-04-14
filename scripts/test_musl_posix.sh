#!/bin/bash
# test_musl_posix.sh - musl AISafeOS64 POSIX 测试构建脚本
#
# 构建 test_musl_posix.c 测试程序，链接 musl_aisafe 静态库，在宿主机上运行测试。

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
TEST_SRC="${PROJECT_DIR}/tests/test_musl_posix.c"
TEST_BIN="${BUILD_DIR}/test_musl_posix"

# 交叉编译器
CROSS_COMPILE="aarch64-linux-gnu-"
CC="${CROSS_COMPILE}gcc"

echo "========================================"
echo "  musl AISafeOS64 POSIX 测试"
echo "========================================"
echo ""

# 检查工具链
if ! command -v ${CC} &> /dev/null; then
    echo -e "${RED}错误: 交叉编译器 ${CC} 未找到${NC}"
    exit 1
fi

# 检查测试源文件
if [ ! -f "${TEST_SRC}" ]; then
    echo -e "${RED}错误: 测试源文件 ${TEST_SRC} 不存在${NC}"
    exit 1
fi

# 检查 musl_aisafe 库
MUSL_LIB="${PROJECT_DIR}/build_arm64/lib/musl_aisafe/libmusl_aisafe.a"
if [ ! -f "${MUSL_LIB}" ]; then
    echo -e "${YELLOW}警告: musl_aisafe 库不存在，尝试编译...${NC}"
    cd "${PROJECT_DIR}/build_arm64"
    make musl_aisafe
    cd - > /dev/null
fi

# 生成 alltypes.h
GENERATED_DIR="${PROJECT_DIR}/build_arm64/lib/musl_aisafe/generated"
if [ ! -f "${GENERATED_DIR}/bits/alltypes.h" ]; then
    echo "生成 alltypes.h..."
    mkdir -p "${GENERATED_DIR}/bits"
    sed -f "${PROJECT_DIR}/lib/musl_upstream/tools/mkalltypes.sed" \
        "${PROJECT_DIR}/lib/musl_aisafe/arch/aarch64_aisafe/bits/alltypes.h.in" \
        "${PROJECT_DIR}/lib/musl_upstream/include/alltypes.h.in" \
        > "${GENERATED_DIR}/bits/alltypes.h"
fi

# 创建构建目录
mkdir -p "${BUILD_DIR}"

# 编译测试程序
echo "编译测试程序..."
${CC} -o "${TEST_BIN}" "${TEST_SRC}" "${MUSL_LIB}" \
    -I "${PROJECT_DIR}/lib/musl_aisafe/arch/aarch64_aisafe" \
    -I "${PROJECT_DIR}/lib/musl_upstream/include" \
    -I "${GENERATED_DIR}/src/internal" \
    -I "${GENERATED_DIR}" \
    -nostdlib -nostartfiles \
    -Wl,--start-group -lc -Wl,--end-group \
    -Wl,-rpath,"${PROJECT_DIR}/build_arm64/lib/musl_aisafe"

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
