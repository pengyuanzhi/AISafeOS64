#!/bin/bash
# check_format.sh - 代码格式检查
# 用法: ./scripts/check_format.sh [项目根目录]
# 检查: 4空格缩进、Allman括号、120字符行宽
# @version 1.0
# @date 2026-04-04

set -e

# ==============================================================================
# 配置
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="${1:-$(dirname "$SCRIPT_DIR")}"

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# 统计变量
TOTAL_FILES=0
TOTAL_ISSUES=0

# 最大行宽
MAX_LINE_LENGTH=120

# ==============================================================================
# 函数定义
# ==============================================================================

print_header()
{
    echo ""
    echo "============================================"
    echo "  AISafeOS64 代码格式检查"
    echo "  缩进: 4空格 | 括号: Allman | 行宽: ${MAX_LINE_LENGTH}"
    echo "============================================"
    echo ""
}

check_file_format()
{
    local file="$1"
    local file_issues=0
    local line_num=0

    TOTAL_FILES=$((TOTAL_FILES + 1))

    while IFS= read -r line || [ -n "$line" ]; do
        line_num=$((line_num + 1))

        # ---- 检查 1: Tab 字符（应使用4空格）----
        if echo "$line" | grep -Pq '\t'; then
            if [ "$file_issues" -eq 0 ]; then
                echo -e "  ${YELLOW}[格式]${NC} $file"
            fi
            echo "    行 $line_num: 包含 Tab 字符（应使用4空格缩进）"
            file_issues=$((file_issues + 1))
        fi

        # ---- 检查 2: 行宽超过限制 ----
        local line_len
        line_len=$(echo "$line" | sed 's/\x1b\[[0-9;]*m//g' | wc -L)
        if [ "$line_len" -gt "$MAX_LINE_LENGTH" ]; then
            if [ "$file_issues" -eq 0 ]; then
                echo -e "  ${YELLOW}[格式]${NC} $file"
            fi
            echo "    行 $line_num: 行宽 ${line_len} > ${MAX_LINE_LENGTH} 字符"
            file_issues=$((file_issues + 1))
        fi

        # ---- 检查 3: K&R 风格括号（函数定义）----
        # 匹配 ")\s*{" 不换行的情况（排除 #define 宏、结构体初始化等）
        if echo "$line" | grep -Pq '^\s*(void|int|uint|static|inline).*\)\s*\{'; then
            if [ "$file_issues" -eq 0 ]; then
                echo -e "  ${YELLOW}[格式]${NC} $file"
            fi
            echo "    行 $line_num: K&R 括号风格（应使用 Allman 风格，左大括号换行）"
            file_issues=$((file_issues + 1))
        fi

        # ---- 检查 4: while(1) / while(true)（应使用 for(;;)）----
        if echo "$line" | grep -Pq '\bwhile\s*\(\s*1\s*\)' || \
           echo "$line" | grep -Pq '\bwhile\s*\(\s*true\s*\)'; then
            if [ "$file_issues" -eq 0 ]; then
                echo -e "  ${YELLOW}[格式]${NC} $file"
            fi
            echo "    行 $line_num: 使用 while(1/true)，应改为 for(;;)"
            file_issues=$((file_issues + 1))
        fi

        # ---- 检查 5: 行尾空格 ----
        if echo "$line" | grep -Pq ' +$'; then
            if [ "$file_issues" -eq 0 ]; then
                echo -e "  ${YELLOW}[格式]${NC} $file"
            fi
            echo "    行 $line_num: 行尾有多余空格"
            file_issues=$((file_issues + 1))
        fi

    done < "$file"

    if [ "$file_issues" -eq 0 ]; then
        echo -e "  ${GREEN}[通过]${NC} $file"
    else
        TOTAL_ISSUES=$((TOTAL_ISSUES + file_issues))
    fi
}

# ==============================================================================
# 主流程
# ==============================================================================

print_header

echo "扫描目录: $PROJECT_DIR"
echo ""

# 检查内核源文件
echo "--- 内核源文件 ---"
for file in $(find "$PROJECT_DIR/kernel" \( -name "*.c" -o -name "*.h" \) 2>/dev/null | sort); do
    check_file_format "$file"
done

# 检查服务源文件
echo ""
echo "--- 服务源文件 ---"
for file in $(find "$PROJECT_DIR/services" \( -name "*.c" -o -name "*.h" \) 2>/dev/null | sort); do
    check_file_format "$file"
done

# 检查公共头文件
echo ""
echo "--- 公共头文件 ---"
for file in $(find "$PROJECT_DIR/include" -name "*.h" 2>/dev/null | sort); do
    check_file_format "$file"
done

# 检查库源文件
echo ""
echo "--- 库源文件 ---"
for file in $(find "$PROJECT_DIR/lib" \( -name "*.c" -o -name "*.h" \) 2>/dev/null | sort); do
    check_file_format "$file"
done

echo ""
echo "============================================"
echo "  格式检查总结"
echo "  检查文件: $TOTAL_FILES"
echo "  格式问题: $TOTAL_ISSUES"
echo "============================================"
echo ""

if [ "$TOTAL_ISSUES" -gt 0 ]; then
    echo -e "${YELLOW}发现 $TOTAL_ISSUES 个格式问题${NC}"
    exit 1
else
    echo -e "${GREEN}所有文件格式检查通过${NC}"
    exit 0
fi
