#!/bin/bash
# check_misra.sh - MISRA C:2012 静态分析检查
# 用法: ./scripts/check_misra.sh [项目根目录]
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
CYAN='\033[0;36m'
NC='\033[0m'

# 统计变量
TOTAL_FILES=0
ISSUE_COUNT=0
WARN_COUNT=0

# ==============================================================================
# 函数定义
# ==============================================================================

print_header()
{
    echo ""
    echo "============================================"
    echo "  AISafeOS64 MISRA C:2012 静态分析"
    echo "============================================"
    echo ""
}

check_file_misra()
{
    local file="$1"
    local basename
    basename="$(basename "$file")"

    # 跳过测试文件（使用 mock_kernel.h，不适用 MISRA 完整检查）
    if echo "$file" | grep -q "/tests/"; then
        return 0
    fi

    # 跳过第三方代码
    if echo "$file" | grep -q "/third_party/"; then
        return 0
    fi

    TOTAL_FILES=$((TOTAL_FILES + 1))
    local file_issues=0

    # ---- 规则 7.1: 禁止八进制常量（除 0 外）----
    if grep -nE '^\s*(uint|int|char|short|long|static|const).*[^xX]0[0-7]+' "$file" | \
       grep -vE '//.*0[0-7]+' | grep -vE '/\*' | grep -vE '0x[0-9a-fA-F]+' > /dev/null 2>&1; then
        echo -e "  ${YELLOW}[规则 7.1]${NC} $file: 疑似使用八进制常量"
        file_issues=$((file_issues + 1))
    fi

    # ---- 规则 7.2: 无符号整数常量必须有 u/U 后缀 ----
    if grep -nE '(uint[0-9_]+_t|size_t)\s+\w+\s*=\s*0x[0-9A-Fa-f]+[^UuLl]' "$file" > /dev/null 2>&1; then
        echo -e "  ${YELLOW}[规则 7.2]${NC} $file: 十六进制无符号常量可能缺少 U 后缀"
        file_issues=$((file_issues + 1))
    fi

    # ---- 规则 10.3: 赋值操作符不应用作真值表达式 ----
    if grep -nE 'if\s*\(\s*\w+\s*=[^=]' "$file" > /dev/null 2>&1; then
        echo -e "  ${RED}[规则 10.3]${NC} $file: 条件中可能存在赋值（应使用 ==）"
        file_issues=$((file_issues + 1))
    fi

    # ---- 规则 15.1: 禁止 goto 语句 ----
    if grep -nE '^\s*goto\s+' "$file" > /dev/null 2>&1; then
        echo -e "  ${RED}[规则 15.1]${NC} $file: 使用了 goto 语句"
        file_issues=$((file_issues + 1))
    fi

    # ---- 规则 16.1: 禁止递归 ----
    # 简单检测：函数名出现在自己体内的直接调用
    local func_name
    func_name="$(basename "$file" .c)"
    if [ "$func_name" != "" ]; then
        if grep -nE "\b${func_name}\s*\(" "$file" | head -2 | tail -1 | grep -q "$func_name"; then
            echo -e "  ${YELLOW}[规则 16.1]${NC} $file: 疑似递归调用"
            file_issues=$((file_issues + 1))
        fi
    fi

    # ---- 规则 9.1: 禁止变长数组 (VLA) ----
    if grep -nE '\w+\s+\w+\s*\[\s*\w+\s*\]' "$file" | \
       grep -vE '(static|const|#define|#if|CONFIG_)' | \
       grep -vE '\[\s*[0-9]+\s*\]' > /dev/null 2>&1; then
        echo -e "  ${YELLOW}[规则 9.1]${NC} $file: 疑似变长数组（VLA）"
        file_issues=$((file_issues + 1))
    fi

    # ---- 规则 17.7: 返回值不得被忽略（非 void 函数调用）----
    # 此项需要更复杂分析，仅做简单提示

    # ---- 无限循环检查 (禁止 while(1)，应使用 for(;;)) ----
    if grep -nE 'while\s*\(\s*1\s*\)' "$file" > /dev/null 2>&1; then
        echo -e "  ${YELLOW}[风格]${NC} $file: 使用 while(1)，应改为 for(;;)"
        file_issues=$((file_issues + 1))
    fi

    # ---- 禁止 while(true) ----
    if grep -nE 'while\s*\(\s*true\s*\)' "$file" > /dev/null 2>&1; then
        echo -e "  ${YELLOW}[风格]${NC} $file: 使用 while(true)，应改为 for(;;)"
        file_issues=$((file_issues + 1))
    fi

    if [ "$file_issues" -eq 0 ]; then
        echo -e "  ${GREEN}[通过]${NC} $file"
    else
        ISSUE_COUNT=$((ISSUE_COUNT + file_issues))
    fi
}

# ==============================================================================
# 主流程
# ==============================================================================

print_header

echo "扫描目录: $PROJECT_DIR"
echo ""

# 查找所有 C 源文件（排除测试和构建目录）
echo "--- 内核源文件 MISRA 检查 ---"
for file in $(find "$PROJECT_DIR/kernel" -name "*.c" 2>/dev/null | sort); do
    check_file_misra "$file"
done

echo ""
echo "--- 服务源文件 MISRA 检查 ---"
for file in $(find "$PROJECT_DIR/services" -name "*.c" 2>/dev/null | sort); do
    check_file_misra "$file"
done

echo ""
echo "--- 库源文件 MISRA 检查 ---"
for file in $(find "$PROJECT_DIR/lib" -name "*.c" 2>/dev/null | sort); do
    check_file_misra "$file"
done

echo ""
echo "============================================"
echo "  MISRA C:2012 检查总结"
echo "  检查文件: $TOTAL_FILES"
echo "  发现问题: $ISSUE_COUNT"
echo "============================================"
echo ""

if [ "$ISSUE_COUNT" -gt 0 ]; then
    echo -e "${YELLOW}警告: 发现 $ISSUE_COUNT 个潜在 MISRA 违规${NC}"
    echo "请手动审查上述问题，建议使用 PC-lint Plus 进行完整分析"
    exit 1
else
    echo -e "${GREEN}所有检查通过（基础静态分析）${NC}"
    echo "注意: 完整 MISRA C:2012 检查需要 PC-lint Plus 或类似工具"
    exit 0
fi
