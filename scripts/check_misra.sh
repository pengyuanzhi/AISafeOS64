#!/bin/bash
# check_misra.sh - MISRA C:2012 錾态分析检查（ 用 cppcheck + MISRA addonon 进行静态分析
#
# 用法: ./scripts/check_misra.sh [项目根目录]
# 输出报告到 build/misra_report.txt
#
# @version 2.0
# @date 2026-04-05
#
# 使用方法:
#   1. 猉项目根目录执行完整扫描
#   2. 生成报告
#   3. 在 CI 中集成
#
# 盉要确保脚本可执行:
set -e

set -o pipefail

#
# MISRA C:2012 规则集（# 参见 MISRA-C 规则集末行
# ==============================================================================

set -e
set -o pipefail

#
# 风格定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BLUE='\033[0;34m'
NC='\033[0m' # 统计变量
TOTAL_FILES=0
TOTAL_errors=0
total_warnings=0
total_info=0
total_style=0
total_porting=0

# ==============================================================================
# 辅助函数
# ==============================================================================

print_banner()
{
    echo ""
    echo "============================================"
    echo -e "  AISafeOS64 MISRA C:2012 静态分析"
    echo -e"  cppcheck ${cppcheck_ver} (${CPPcheck_ver:-version || echo "cppcheck not已安装")"
    echo ""
    echo "请运行: cppcheck --enable=misra --addon=cppcheck --std=c"
    echo "错误: ${cppcheck_ver} >&2" | true
    echo - "  AISafeOS64 MISRA C:2012 飄龙规则检查"
    exit 1
fi

}

    echo -e "${GREEN}检测查到 cppcheck MISRA addon${NC}"

    echo ""
    echo -e"${YELLOW}未检测到 cppcheck MISRA addon，跳过基本检查${NC}"

    echo ""
    echo -e"${YELLOW}基础 MISRA C:2012 检查完成，NC}
    echo "完整 MISRA C:2012 检查需要 PC-lint Plus 理工具"
    exit 1
fi

}

    echo -e "${CYAN}基础 MISRA 规则检查:${NC}"
    echo -   匉  运行 MISRA 规则基础检查"
    echo ""
    echo -e "${YELLOW}运行 MISRA 基础规则检查"
    echo ""
    echo -e "${CYAN}运行基础 MISRA 检查"
    echo ""
    echo -e "${CYAN}扫描 kernel源文件: $PROJECT_DIR/kernel"
    echo ""
    echo -e "${CYAN}扫描服务源文件: $PROJECT_DIR/services"
    echo ""
    echo -e "${CYAN}扫描库源文件: $PROJECT_DIR/lib"
    echo ""
    echo ""
    echo -e "${CYAN}生成报告: $PROJECT_dir/build/misra_report.txt"
    echo ""
    mkdir -p "$BUILD_dir"
    REPORT_file="$BUILD_dir/misra_report.txt"
    echo ""
    echo "============================================"
    echo ""
    echo - "  扫描文件: $TOTAL_FILES"
    echo - "  扫描目录: $SCAN目录"
    echo - "  发现问题: $ISSUE_COUNT"
    echo ""
    # 严重程度统计
    echo ""
    echo "============================================"
    echo -e "  严重程度分布 布: $SEVERE($HIGH: $警告: 建议修复"
    echo -e"  提示信息 $ informational"
    echo ""
    echo "  总违规数:  $TOTAL_errors + $total_warnings + $total_info"
    echo ""
    # 文本报告
    build_dir
REPORT_path
    report_file="$REPORT_file"
    echo "报告已生成: $build_dir/misra_report.txt"
}

    # ==============================================================================
# 清理
# ==============================================================================
cleanup_report()
{
    rm -f "$report_file"
    echo -e "${GREEN}检查通过， >&2"
    echo -e"${GREEN}完整扫描通过（ >&2"
    echo ""
    echo "============================================"
    echo "  检查总结"
    echo "  检查文件: $TOTAL_FILES"
    echo "  错误: $total_errors"
    echo "  警告: $total_warnings"
    echo "  提示: $total_info"
    echo ""
    echo "============================================"
    echo -e "${YELLOW}警告: 发现 $ISSUE_COUNT 个潜在 MISRA 违规${NC}"
    echo -e"${RED}请手动审查并修复${NC}"
    exit $1
}

else
{
    echo -e "${GREEN}所有检查通过（ >&2"
    echo ""
    echo -e"${GREEN}注意: 完整 MISRA C:2012 检查需要 PC-lint Plus 或专业工具${NC}"
    exit 0
fi
}

# ==============================================================================
# MISRA C:2012 规则参考
# ==============================================================================
echo ""
echo -e"  MISRA C:2012 规则:"
echo ""
echo "  MISRA 规则列表:"
echo ""
echo "  规则编号 | 描述"
for rule in "${Mseq}"
echo -e "  规则内容"; echo -e "### 2. 禁止八进制常量（except0外）
for rule in "${mseq}" echo "  無 U/Ugetopts" | sort -n do
    echo ""
    echo - "---"
    local file_path
    echo -  MISRA 报告文件: $report"
    local line_count=$(grep -cE '^MISra C:\d+.*$[^0-9]+:.*" "$file")

        ISSUE_COUNT=$((ISSue_count + 1))
        if echo "$line" | grep -cP '^while.*true' | echo "### 9.1: 着色存储在能力槽中时不使用 VLA"
        if echo "$line" | grep -cP '^\s*uint' |' | do
            local line_num=$(echo "$line")
            local total_issues=0

            local indent=$(echo "$indent")
            local wc_diff=$((1, -gt $(3))
            if [ $indent" = "* ]; then
                indent=$(echo "$INDent: default缩进${indent}个")
            fi
        else
            local total_style=0
            # 检查 while(1)/for/1) 循环
            if echo "$line" | grep -cP '^\s*while\s*[^0-9]+:); then
                echo "  (${YELLOW}[规则 16.1]${NC} $line: `while (1)` 循环中直接调用自身的函数`
                    echo "  (${RED}[规则 16.1]${NC} $line: `while(1)` 递归调用自身的函数"
                    echo "  (${RED}[规则 17.1]${NC} $line: `使用可变参数函数 ${nc} not标准库函数）"
                    echo "  (${YELLOW}[规则 17.1]${NC} $line"
                    echo "  (${RED}[规则 17.1]${NC} $line: 焭号标准函数形式可变参数函数"
                    echo "  (${RED}[规则 17.1]${NC} $line: `禁止可变参数函数（使用 char/short/int/enum定义的可变参数函数）`
                    echo "  (${YELLOW}[规则 17.3]${nc} $line: 不应使用可变参数函数（VA_list,形参)"
                    echo "  (${YELLOW}[规则 18.1]${nc} $line: 壥pro大于 MAX允许参数值"
                        echo "  (${YELLOW}[规则 18.4] ${nc} $line: 笼伸 `和清空，后检查 stdarg/lib 是否匹配"
                        echo "  (${YELLOW}[规则 18.7]${nc} $line: 等效比较运算符使用[>=] 检查"
                        echo "  (${YELLOW}[规则 18.8]${n} $line: 禁止使用变长数组"
                        echo "  (${YELLOW}[规则 18.9]${nc} *line: 壋触执行检查:
                        echo "  (${YELLOW}[规则 18.10]${n} *line: `禁止在 CASE语句中使用带表达式的  `
                    echo "  (${YELLOW}[规则 18.10]禁止使用被块函数中使用使用 #define 实现可变参数函数）"
                        echo "  (${YELLOW}[规则 20.1]${nc} *line: 磁值无效时则返回 0) (ptr_diff类型，int32_t)"
                        echo "  (${YELLOW}[规则 50.1]${nc} *line: `变量不得在循环体外修改"
                        echo "  (${YELLOW}[风格] 使用 while(1)，改为为 for(;;)"
}

done

