#!/bin/bash
# code_stats.sh - 代码统计（行数、函数数量、圈复杂度）
# 用法: ./scripts/code_stats.sh [项目根目录]
# @version 1.0
# @date 2026-04-04

set -e

# ==============================================================================
# 配置
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="${1:-$(dirname "$SCRIPT_DIR")}"

# ==============================================================================
# 函数定义
# ==============================================================================

print_header()
{
    echo ""
    echo "============================================"
    echo "  AISafeOS64 代码统计报告"
    echo "============================================"
    echo ""
}

# 统计目录中的行数
count_lines()
{
    local dir="$1"
    local pattern="$2"

    if [ -d "$dir" ]; then
        find "$dir" -name "$pattern" 2>/dev/null | while read -r f; do
            wc -l < "$f"
        done | awk '{sum+=$1} END {print sum+0}'
    else
        echo "0"
    fi
}

# 统计文件数量
count_files()
{
    local dir="$1"
    local pattern="$2"

    if [ -d "$dir" ]; then
        find "$dir" -name "$pattern" 2>/dev/null | wc -l
    else
        echo "0"
    fi
}

# 统计函数数量（简单匹配）
count_functions()
{
    local dir="$1"

    if [ -d "$dir" ]; then
        # 匹配函数定义模式（Allman风格）
        find "$dir" -name "*.c" 2>/dev/null | while read -r f; do
            # 排除空行、注释行、typedef、#define
            grep -cE '^\s*(static\s+)?(inline\s+)?(void|int|uint|bool|size_t|kernel_status_t|ErrorCode_t|cpu_id_t|thread_id_t)\s+\w+\s*\(' "$f" 2>/dev/null || echo "0"
        done | awk '{sum+=$1} END {print sum+0}'
    else
        echo "0"
    fi
}

# 估算圈复杂度（决策点计数）
estimate_complexity()
{
    local dir="$1"

    if [ -d "$dir" ]; then
        local total_points=0

        # 计数决策点: if, else if, for, while, case, &&, ||, ?:
        for keyword in "if" "for" "while" "case" "&&" "||" "?:"; do
            local count
            count=$(find "$dir" -name "*.c" 2>/dev/null | while read -r f; do
                grep -cE "\b${keyword}\b" "$f" 2>/dev/null || echo "0"
            done | awk '{sum+=$1} END {print sum+0}')
            total_points=$((total_points + count))
        done

        echo "$total_points"
    else
        echo "0"
    fi
}

# ==============================================================================
# 主流程
# ==============================================================================

print_header

# ---- 总体统计 ----
echo "┌─────────────────────────────────────────────┐"
echo "│  项目总体统计                                │"
echo "└─────────────────────────────────────────────┘"

# 统计各目录的代码行数
kernel_c_lines=$(count_lines "$PROJECT_DIR/kernel" "*.c")
kernel_s_lines=$(count_lines "$PROJECT_DIR/kernel" "*.S")
kernel_h_lines=$(count_lines "$PROJECT_DIR/kernel" "*.h" 2>/dev/null || echo "0")

services_c_lines=$(count_lines "$PROJECT_DIR/services" "*.c")
services_h_lines=$(count_lines "$PROJECT_DIR/services" "*.h" 2>/dev/null || echo "0")

include_h_lines=$(count_lines "$PROJECT_DIR/include" "*.h")
tests_c_lines=$(count_lines "$PROJECT_DIR/tests" "*.c")
lib_c_lines=$(count_lines "$PROJECT_DIR/lib" "*.c")

kernel_c_files=$(count_files "$PROJECT_DIR/kernel" "*.c")
kernel_s_files=$(count_files "$PROJECT_DIR/kernel" "*.S")
services_c_files=$(count_files "$PROJECT_DIR/services" "*.c")
include_h_files=$(count_files "$PROJECT_DIR/include" "*.h")
tests_c_files=$(count_files "$PROJECT_DIR/tests" "*.c")
lib_c_files=$(count_files "$PROJECT_DIR/lib" "*.c")

total_c_lines=$((kernel_c_lines + services_c_lines + tests_c_lines + lib_c_lines))
total_h_lines=$((kernel_h_lines + include_h_lines + services_h_lines))
total_asm_lines=$kernel_s_lines
total_lines=$((total_c_lines + total_h_lines + total_asm_lines))

total_c_files=$((kernel_c_files + services_c_files + tests_c_files + lib_c_files))

echo ""
printf "  %-30s %8s %8s\n" "模块" "文件数" "代码行数"
echo "  ───────────────────────────────────────────"
printf "  %-30s %8d %8d\n" "kernel/ (C源码)" "$kernel_c_files" "$kernel_c_lines"
printf "  %-30s %8d %8d\n" "kernel/ (汇编)" "$kernel_s_files" "$kernel_s_lines"
printf "  %-30s %8d %8d\n" "services/ (C源码)" "$services_c_files" "$services_c_lines"
printf "  %-30s %8d %8d\n" "include/ (头文件)" "$include_h_files" "$include_h_lines"
printf "  %-30s %8d %8d\n" "tests/ (测试代码)" "$tests_c_files" "$tests_c_lines"
printf "  %-30s %8d %8d\n" "lib/ (库代码)" "$lib_c_files" "$lib_c_lines"
echo "  ───────────────────────────────────────────"
printf "  %-30s %8s %8d\n" "总计" "$total_c_files 文件" "$total_lines"
echo ""

# ---- 函数统计 ----
echo "┌─────────────────────────────────────────────┐"
echo "│  函数统计                                    │"
echo "└─────────────────────────────────────────────┘"

kernel_funcs=$(count_functions "$PROJECT_DIR/kernel")
services_funcs=$(count_functions "$PROJECT_DIR/services")
lib_funcs=$(count_functions "$PROJECT_DIR/lib")
total_funcs=$((kernel_funcs + services_funcs + lib_funcs))

echo ""
printf "  %-30s %8s\n" "模块" "函数数"
echo "  ───────────────────────────────────────────"
printf "  %-30s %8d\n" "kernel/" "$kernel_funcs"
printf "  %-30s %8d\n" "services/" "$services_funcs"
printf "  %-30s %8d\n" "lib/" "$lib_funcs"
echo "  ───────────────────────────────────────────"
printf "  %-30s %8d\n" "总计" "$total_funcs"
echo ""

# ---- 圈复杂度估算 ----
echo "┌─────────────────────────────────────────────┐"
echo "│  圈复杂度估算                                │"
echo "└─────────────────────────────────────────────┘"

kernel_complexity=$(estimate_complexity "$PROJECT_DIR/kernel")
services_complexity=$(estimate_complexity "$PROJECT_DIR/services")
total_complexity=$((kernel_complexity + services_complexity))

# 平均复杂度（决策点 / 函数数）
if [ "$total_funcs" -gt 0 ]; then
    avg_complexity=$((total_complexity / total_funcs))
else
    avg_complexity=0
fi

echo ""
printf "  %-30s %8s\n" "模块" "决策点"
echo "  ───────────────────────────────────────────"
printf "  %-30s %8d\n" "kernel/" "$kernel_complexity"
printf "  %-30s %8d\n" "services/" "$services_complexity"
echo "  ───────────────────────────────────────────"
printf "  %-30s %8d\n" "总计" "$total_complexity"
echo ""
printf "  平均圈复杂度: %d（决策点 / 函数）\n" "$avg_complexity"
echo "  建议阈值: ≤ 10（MISRA-C:2012 推荐）"
echo ""

# ---- 测试覆盖率预估 ----
echo "┌─────────────────────────────────────────────┐"
echo "│  测试覆盖预估                                │"
echo "└─────────────────────────────────────────────┘"

echo ""
printf "  测试代码行数:     %d\n" "$tests_c_lines"
printf "  内核代码行数:     %d\n" "$kernel_c_lines"

if [ "$kernel_c_lines" -gt 0 ]; then
    test_ratio=$((tests_c_lines * 100 / kernel_c_lines))
    printf "  测试/代码比:      %d%%\n" "$test_ratio"
fi

echo ""

# ---- 文件大小 Top 10 ----
echo "┌─────────────────────────────────────────────┐"
echo "│  最大文件 Top 10                              │"
echo "└─────────────────────────────────────────────┘"
echo ""

find "$PROJECT_DIR/kernel" "$PROJECT_DIR/services" "$PROJECT_DIR/lib" \
    \( -name "*.c" -o -name "*.h" \) 2>/dev/null | while read -r f; do
    lines=$(wc -l < "$f")
    echo "$lines $f"
done | sort -rn | head -10 | while read -r lines file; do
    rel_path="${file#$PROJECT_DIR/}"
    printf "  %6d 行  %s\n" "$lines" "$rel_path"
done

echo ""
echo "============================================"
echo "  统计完成"
echo "============================================"
echo ""
