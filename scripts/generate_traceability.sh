#!/bin/bash
# generate_traceability.sh - 生成需求追溯矩阵
#
# 用法: ./scripts/generate_traceability.sh

SCRIPT_DIR="$(dirname "$0")"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
TEST_DIR="${PROJECT_DIR}/tests"
SRC_DIR="${PROJECT_DIR}/kernel"
REQ_FILE="${PROJECT_DIR}/docs/requirements/REQUIREMENTS.md"

echo "=== AISafeOS64 需求追溯矩阵 ==="
echo "生成日期: $(date +%Y-%m-%d)"
echo ""

# 概览统计
echo "## 概览"
echo ""
total_tests=$(find "${TEST_DIR}" -name "test_*.c" 2>/dev/null | wc -l)
total_src=$(find "${SRC_DIR}" -name "*.c" -o -name "*.h" 2>/dev/null | wc -l)
req_count=$(grep -cE 'KR-[0-9]+' "$REQ_FILE" 2>/dev/null || echo 0)
echo "- 测试文件数: ${total_tests}"
echo "- 源文件数: ${total_src}"
echo "- 需求数: ${req_count}"
echo ""

# 模块 -> 需求 -> 测试用例 -> 源文件
echo "## 追溯矩阵"
echo ""
echo "| 模块 | 需求ID | 测试文件 | 源文件 |"
echo "|------|--------|----------|--------|"

# 扫描测试文件中的需求引用
for test_file in "${TEST_DIR}"/test_*.c; do
    if [ ! -f "$test_file" ]; then
        continue
    fi
    test_name=$(basename "$test_file" .c | sed 's/^test_//')
    req_ids=$(grep -oE '(KR|SC|MM|SE|DR|NW|PF|PX|MT|IN|MP|TM|BK|API|FS|SF|CA|VZ|DV)-[0-9]+' "$test_file" 2>/dev/null | sort -u)
    if [ -n "$req_ids" ]; then
        for req_id in $req_ids; do
            echo "| ${test_name} | ${req_id} | $(basename "$test_file") | - |"
        done
    else
        echo "| ${test_name} | - | $(basename "$test_file") | - |"
    fi
done

echo ""
echo "---"
echo "追溯矩阵生成完成。"
