#!/bin/bash
# check_misra.sh - 简化的 MISRA C:2012 静态分析检查
#
# 用法: ./scripts/check_misra.sh
# 输出报告到 build/misra_report.txt
#
# @version 3.0
# @date 2026-04-22

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
REPORT_FILE="$BUILD_DIR/misra_report.txt"

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# 统计变量
TOTAL_FILES=0
TOTAL_ISSUES=0
HIGH_ISSUES=0
MEDIUM_ISSUES=0
LOW_ISSUES=0

echo "============================================"
echo "  AISafeOS64 MISRA C:2012 基础检查"
echo "============================================"
echo ""

# 检查 cppcheck 是否安装
if ! command -v cppcheck &> /dev/null; then
    echo -e "${RED}错误: cppcheck 未安装${NC}"
    echo "请安装 cppcheck: sudo apt-get install cppcheck"
    exit 1
fi

echo -e "${GREEN}✓ cppcheck 已安装: $(cppcheck --version | head -1)${NC}"
echo ""

# 创建报告文件
echo "MISRA C:2012 检查报告" > "$REPORT_FILE"
echo "生成时间: $(date)" >> "$REPORT_FILE"
echo "============================================" >> "$REPORT_FILE"
echo "" >> "$REPORT_FILE"

# 扫描源文件
SCAN_DIRS=("kernel" "services" "lib" "tests")

for dir in "${SCAN_DIRS[@]}"; do
    if [ -d "$PROJECT_DIR/$dir" ]; then
        echo -e "${YELLOW}扫描目录: $dir${NC}"

        # 使用 cppcheck 进行基础 MISRA 检查
        cppcheck --enable=style,performance,portability --inconclusive \
            --suppress=missingIncludeSystem \
            --suppressions-list="$PROJECT_DIR/scripts/cppcheck_suppressions.txt" \
            --xml="$BUILD_DIR/misra_cppcheck.xml" \
            -I "$PROJECT_DIR/include" \
            -I "$PROJECT_DIR/lib/musl_aisafe/include" \
            "$PROJECT_DIR/$dir" >> "$REPORT_FILE" 2>&1

        # 统计文件数
        FILE_COUNT=$(find "$PROJECT_DIR/$dir" -name "*.c" -o -name "*.h" | wc -l)
        TOTAL_FILES=$((TOTAL_FILES + FILE_COUNT))
        echo "  扫描文件: $FILE_COUNT"
        echo "" >> "$REPORT_FILE"
    fi
done

echo -e "${GREEN}✓ 扫描完成${NC}"
echo ""
echo "============================================"
echo "  检查总结"
echo "============================================"
echo ""
echo "检查文件: $TOTAL_FILES"
echo ""
echo -e "${RED}基础 MISRA C:2012 检查完成${NC}"
echo ""
echo -e "${YELLOW}注意: 完整 MISRA C:2012 规则检查需要 PC-lint Plus 或专业工具${NC}"
echo ""
echo "报告已生成: $REPORT_FILE"
echo ""

exit 0
