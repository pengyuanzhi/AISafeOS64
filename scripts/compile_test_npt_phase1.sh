#!/bin/bash

# 编译 NPT Phase 1 测试

set -e

echo "=== 编译 NPT Phase 1 单元测试 ==="

# 编译 npt.c（禁用警告）
gcc -std=c11 \
    -Wall -Wno-error=implicit-function-declaration \
    -I./include \
    -I./services/vmm/npt \
    -I./kernel \
    -I./tests \
    -c services/vmm/npt/npt.c \
    -o build/npt_phase1.o \
    2>&1 | tee build/npt_phase1_compile.log

if [ $? -ne 0 ]; then
    echo "❌ npt.c 编译失败"
    exit 1
fi

# 编译 mock_vmm.c
gcc -std=c11 \
    -Wall -Wno-error=implicit-function-declaration \
    -I./include \
    -I./services/vmm \
    -c services/vmm/mock_vmm.c \
    -o build/mock_vmm.o \
    2>&1 | tee build/mock_vmm_compile.log

# 编译测试文件
gcc -std=c11 \
    -Wall -Wno-error=implicit-function-declaration \
    -I./include \
    -I./services/vmm/npt \
    -I./kernel \
    -I./tests \
    -I./lib/unity \
    -I./services/vmm \
    -c tests/test_npt_phase1.c \
    -o build/test_npt_phase1.o \
    2>&1 | tee build/test_npt_phase1_compile.log

if [ $? -ne 0 ]; then
    echo "❌ test_npt_phase1.c 编译失败"
    exit 1
fi

# 链接
gcc -o build/test_npt_phase1 \
    build/npt_phase1.o \
    build/mock_vmm.o \
    build/test_npt_phase1.o \
    2>&1 | tee build/npt_phase1_link.log

if [ $? -ne 0 ]; then
    echo "❌ 链接失败"
    exit 1
fi

echo "✅ 编译成功"

# 运行测试
echo ""
echo "=== 运行 NPT Phase 1 单元测试 ==="
./build/test_npt_phase1

if [ $? -ne 0 ]; then
    echo "❌ 测试失败"
    exit 1
fi

echo "✅ 所有测试通过"

exit 0