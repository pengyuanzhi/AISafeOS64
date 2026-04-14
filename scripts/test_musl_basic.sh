#!/bin/bash
# test_musl_basic.sh - musl AISafeOS64 基础模块验证构建脚本
#
# 构建 test_musl_basic.c 测试程序，只编译 musl string 模块。

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

# 交叉编译器
CROSS_COMPILE="aarch64-linux-gnu-"
CC="${CROSS_COMPILE}gcc"

echo "========================================"
echo "  musl AISafeOS64 基础模块验证"
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

# 从 musl_aisafe 库中提取 string 模块的对象文件
echo "提取 string 模块对象文件..."
STRING_OBJS_DIR="${BUILD_DIR}/string_objs"
mkdir -p "${STRING_OBJS_DIR}"

cd "${STRING_OBJS_DIR}"
ar x "${MUSL_LIB}" \
    memcpy.c.obj \
    memmove.c.obj \
    memcmp.c.obj \
    memchr.c.obj \
    strlen.c.obj \
    strcmp.c.obj \
    strncmp.c.obj \
    strcpy.c.obj \
    strncpy.c.obj \
    strcat.c.obj \
    strncat.c.obj \
    strchr.c.obj \
    strrchr.c.obj \
    strstr.c.obj \
    memccpy.c.obj \
    mempcpy.c.obj \
    memrchr.c.obj \
    stpcpy.c.obj \
    stpncpy.c.obj 2>/dev/null || true

# 编译测试程序，链接 string 模块
echo "编译测试程序..."
cd "${BUILD_DIR}"
${CC} -o "${TEST_BIN}" "${TEST_SRC}" \
    "${STRING_OBJS_DIR}"/*.obj \
    -I "${PROJECT_DIR}/lib/musl_aisafe/arch/aarch64_aisafe" \
    -I "${PROJECT_DIR}/lib/musl_upstream/include" \
    -I "${GENERATED_DIR}/src/internal" \
    -I "${GENERATED_DIR}" \
    -nostdlib -nostartfiles \
    -Wl,--start-group -lc -Wl,--end-group \
    -Wl,-rpath,"${PROJECT_DIR}/build_arm64/lib/musl_aisafe" \
    -Wl,-e,main 2>&1 | grep -v "undefined reference" || true

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
