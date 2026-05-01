#!/bin/bash
# AISafe64 - 性能测试运行脚本
# @version 1.0
# @date 2026-05-02

set -e

# 颜色定义
RED='\\033[0;31m'
GREEN='\\033[0;32m'
YELLOW='\\033[1;33m'
BLUE='\\033[0;34m'
NC='\\033[0m' # No Color

# 构建目录
BUILD_DIR="build"
TEST_DIR="${BUILD_DIR}/perf"

# 创建输出目录
mkdir -p "${TEST_DIR}"

# 性能测试函数
run_perf_test() {
    local test_name=$1
    local test_binary="${BUILD_DIR}/tests/${test_name}"
    local output_file="${TEST_DIR}/${test_name}.log"

    echo -e "${BLUE}========== 运行性能测试: ${test_name} ==========${NC}"

    if [ ! -f "${test_binary}" ]; then
        echo -e "${RED}[错误] 测试二进制文件不存在: ${test_binary}${NC}"
        echo -e "${YELLOW}请先运行: cd build && cmake .. && make${NC}"
        return 1
    fi

    # 运行测试并保存输出
    if [ "${test_name}" == "test_perf_sync" ] || \
       [ "${test_name}" == "test_perf_mem" ] || \
       [ "${test_name}" == "test_perf_core" ]; then
        ${test_binary} > "${output_file}" 2>&1
    else
        ${test_binary} > "${output_file}" 2>&1 || true
    fi

    # 检查退出状态
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}[成功] ${test_name} 完成${NC}"
        return 0
    else
        echo -e "${RED}[失败] ${test_name} 失败（退出码: $?）${NC}"
        echo -e "${YELLOW}输出: ${output_file}${NC}"
        return 1
    fi
}

# 主函数
main() {
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}AISafe64 - 性能测试套件${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo ""

    # 检查构建目录
    if [ ! -d "${BUILD_DIR}" ]; then
        echo -e "${RED}[错误] 构建目录不存在: ${BUILD_DIR}${NC}"
        echo -e "${YELLOW}请先运行: mkdir -p ${BUILD_DIR} && cd ${BUILD_DIR} && cmake .. && make${NC}"
        exit 1
    fi

    # 检查测试二进制文件
    if [ ! -d "${BUILD_DIR}/tests" ]; then
        echo -e "${RED}[错误] 测试二进制文件不存在${NC}"
        echo -e "${YELLOW}请先运行: cd ${BUILD_DIR} && cmake .. && make${NC}"
        exit 1
    fi

    echo -e "${YELLOW}警告: 性能测试依赖宿主机 CPU 性能${NC}"
    echo -e "${YELLOW}结果可能因 CPU 不同而有所差异${NC}"
    echo ""

    # 性能测试列表
    PERF_TESTS=(
        "test_perf_sync"
        "test_perf_mem"
        "test_perf_core"
    )

    # 统计
    local total_tests=${#PERF_TESTS[@]}
    local passed_tests=0
    local failed_tests=0

    # 运行所有性能测试
    for test_name in "${PERF_TESTS[@]}"; do
        if run_perf_test "${test_name}"; then
            ((passed_tests++))
        else
            ((failed_tests++))
        fi
        echo ""
    done

    # 生成汇总报告
    local report_file="${TEST_DIR}/perf_report.txt"

    echo -e "${BLUE}========================================${NC}" > "${report_file}"
    echo -e "${BLUE}AISafe64 - 性能测试汇总报告${NC}" >> "${report_file}"
    echo -e "${BLUE}========================================${NC}" >> "${report_file}"
    echo "" >> "${report_file}"
    echo -e "测试时间: $(date '+%Y-%m-%d %H:%M:%S')" >> "${report_file}"
    echo -e "运行环境: $(uname -s) $(uname -r) $(uname -m)" >> "${report_file}"
    echo -e "构建类型: ${CMAKE_BUILD_TYPE:-Debug}" >> "${report_file}"
    echo "" >> "${report_file}"
    echo -e "总测试数: ${total_tests}" >> "${report_file}"
    echo -e "${GREEN}通过: ${passed_tests}${NC}" >> "${report_file}"
    if [ ${failed_tests} -gt 0 ]; then
        echo -e "${RED}失败: ${failed_tests}${NC}" >> "${report_file}"
    else
        echo -e "失败: ${failed_tests}" >> "${report_file}"
    fi
    echo "" >> "${report_file}"
    echo -e "----------------------------------------${NC}" >> "${report_file}"
    echo "" >> "${report_file}"

    for test_name in "${PERF_TESTS[@]}"; do
        local test_log="${TEST_DIR}/${test_name}.log"
        if [ -f "${test_log}" ]; then
            echo -e "测试: ${test_name}" >> "${report_file}"
            echo "" >> "${report_file}"
            tail -50 "${test_log}" >> "${report_file}"
            echo "" >> "${report_file}"
            echo -e "----------------------------------------${NC}" >> "${report_file}"
            echo "" >> "${report_file}"
        fi
    done

    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}测试完成${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo ""
    echo -e "汇总报告: ${report_file}"
    echo -e "详细日志: ${TEST_DIR}/"
    echo ""

    # 生成 HTML 报告（可选）
    if command -v python3 &> /dev/null; then
        echo -e "${YELLOW}生成 HTML 报告...${NC}"
        python3 "${TEST_DIR}/generate_perf_report.py" "${TEST_DIR}" 2>/dev/null || \
        echo -e "${YELLOW}未找到 HTML 报告生成器（可选功能）${NC}"
    fi

    exit ${failed_tests}
}

# 主函数调用
main "$@"
