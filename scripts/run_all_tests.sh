#!/bin/bash
# run_all_tests.sh - 编译并运行所有宿主机单元测试
# 用法: ./scripts/run_all_tests.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
TEST_DIR="$PROJECT_DIR/tests"
BUILD_DIR="$PROJECT_DIR/build/tests"

PASS=0
FAIL=0
TOTAL=0

mkdir -p "$BUILD_DIR"

run_test() {
    local name="$1"
    local src="$TEST_DIR/$name"
    local bin="$BUILD_DIR/${name%.c}"

    printf "  编译 %-30s ... " "$name"
    if gcc -std=c11 -Wall -Wextra -o "$bin" "$src" 2>/dev/null; then
        printf "OK  运行 ... "
        if "$bin" > /tmp/test_output.txt 2>&1; then
            echo "PASS"
            PASS=$((PASS + 1))
        else
            echo "FAIL (运行时错误)"
            cat /tmp/test_output.txt
            FAIL=$((FAIL + 1))
        fi
    else
        echo "FAIL (编译错误)"
        gcc -std=c11 -Wall -Wextra -o "$bin" "$src" 2>&1 || true
        FAIL=$((FAIL + 1))
    fi
    TOTAL=$((TOTAL + 1))
}

echo ""
echo "============================================"
echo "  AISafeOS64 宿主机单元测试套件"
echo "============================================"
echo ""

echo "--- IPC 通道 / 安全 / 驱动 / 验证测试 ---"
run_test "test_ipc.c"
run_test "test_vfs.c"
run_test "test_security.c"
run_test "test_certification.c"
run_test "test_twin.c"
run_test "test_formal_verify.c"
run_test "test_evidence.c"

echo ""
echo "--- 内核模块测试（mock_kernel.h）---"
run_test "test_spinlock.c"
run_test "test_object_pool.c"
run_test "test_scheduler.c"
run_test "test_mutex.c"
run_test "test_timer.c"
run_test "test_phys_mem.c"
run_test "test_endpoint.c"
run_test "test_notification.c"
run_test "test_channel.c"
run_test "test_capability.c"
run_test "test_capability_revoke.c"
run_test "test_smp.c"

echo ""
echo "============================================"
echo "  测试总结: $TOTAL 个测试集"
echo "  通过: $PASS"
echo "  失败: $FAIL"
echo "============================================"
echo ""

exit $FAIL
