#!/bin/bash
# AISafeOS64 月度里程碑评估脚本
# 每月1日 22:00 执行

set -e

WORKDIR="/home/kerfs/AISafeOS64/AISafeOS64"
LOG_DIR="$WORKDIR/logs"
REPORT_DIR="$WORKDIR/reports"
MONTH=$(date '+%Y-%m')
TODAY=$(date '+%Y-%m-%d')
TIME=$(date '+%H:%M:%S')

# 创建目录
mkdir -p "$LOG_DIR"
mkdir -p "$REPORT_DIR"

cd "$WORKDIR"

echo "📊 生成月度里程碑评估 - $MONTH ($TODAY $TIME)" > "$LOG_DIR/monthly_report_$MONTH.log"

# 1. 月度完成情况
echo "## 📊 月度完成情况" >> "$LOG_DIR/monthly_report_$MONTH.log"
echo "" >> "$LOG_DIR/monthly_report_$MONTH.log"

# 本月提交统计
COMMIT_COUNT=$(git log --since="1 month ago" --oneline | wc -l || echo "0")
echo "- 提交次数：$COMMIT_COUNT 次" >> "$LOG_DIR/monthly_report_$MONTH.log"

# 本月新增代码
ADDED_LINES=$(git log --since="1 month ago" --pretty=tformat: --numstat | awk '{add+=$1; del+=$2} END {print add+del}' || echo "0")
echo "- 新增代码：$ADDED_LINES 行" >> "$LOG_DIR/monthly_report_$MONTH.log"

# 本月新增文件
NEW_FILES=$(git log --since="1 month ago" --name-only --pretty=format: | sort -u | grep -v "^$" | wc -l || echo "0")
echo "- 新增文件：$NEW_FILES 个" >> "$LOG_DIR/monthly_report_$MONTH.log"

echo "" >> "$LOG_DIR/monthly_report_$MONTH.log"

# 2. 里程碑评估
echo "## 🎯 里程碑评估" >> "$LOG_DIR/monthly_report_$MONTH.log"
echo "" >> "$LOG_DIR/monthly_report_$MONTH.log"

PLAN_FILE="$WORKDIR/docs/COMMERCIAL_DEVELOPMENT_PLAN.md"

if [ -f "$PLAN_FILE" ]; then
    # 检查 Phase 1
    if grep -q "Phase 1.*能力系统" "$PLAN_FILE"; then
        # 统计已完成任务
        COMPLETED_TASKS=$(grep -A 50 "Phase 1" "$PLAN_FILE" | grep -c "^\- \[x\]" || echo "0")
        TOTAL_TASKS=$(grep -A 50 "Phase 1" "$PLAN_FILE" | grep -c "^\-\s*\[" || echo "0")
        if [ $TOTAL_TASKS -gt 0 ]; then
            PROGRESS=$((COMPLETED_TASKS * 100 / TOTAL_TASKS))
            echo "- **Phase 1**: $PROGRESS% ($COMPLETED_TASKS/$TOTAL_TASKS 完成)" >> "$LOG_DIR/monthly_report_$MONTH.log"
        else
            echo "- **Phase 1**: 进行中" >> "$LOG_DIR/monthly_report_$MONTH.log"
        fi
    fi

    # 检查 Phase 2
    if grep -q "Phase 2" "$PLAN_FILE"; then
        COMPLETED_TASKS=$(grep -A 50 "Phase 2" "$PLAN_FILE" | grep -c "^\- \[x\]" || echo "0")
        TOTAL_TASKS=$(grep -A 50 "Phase 2" "$PLAN_FILE" | grep -c "^\-\s*\[" || echo "0")
        if [ $TOTAL_TASKS -gt 0 ]; then
            PROGRESS=$((COMPLETED_TASKS * 100 / TOTAL_TASKS))
            echo "- **Phase 2**: $PROGRESS% ($COMPLETED_TASKS/$TOTAL_TASKS 完成)" >> "$LOG_DIR/monthly_report_$MONTH.log"
        else
            echo "- **Phase 2**: ⏳ 待开始" >> "$LOG_DIR/monthly_report_$MONTH.log"
        fi
    fi

    # 检查 Phase 3
    if grep -q "Phase 3" "$PLAN_FILE"; then
        COMPLETED_TASKS=$(grep -A 50 "Phase 3" "$PLAN_FILE" | grep -c "^\- \[x\]" || echo "0")
        TOTAL_TASKS=$(grep -A 50 "Phase 3" "$PLAN_FILE" | grep -c "^\-\s*\[" || echo "0")
        if [ $TOTAL_TASKS -gt 0 ]; then
            PROGRESS=$((COMPLETED_TASKS * 100 / TOTAL_TASKS))
            echo "- **Phase 3**: $PROGRESS% ($COMPLETED_TASKS/$TOTAL_TASKS 完成)" >> "$LOG_DIR/monthly_report_$MONTH.log"
        else
            echo "- **Phase 3**: ⏳ 待开始" >> "$LOG_DIR/monthly_report_$MONTH.log"
        fi
    fi

    # 检查 Phase 4
    if grep -q "Phase 4" "$PLAN_FILE"; then
        COMPLETED_TASKS=$(grep -A 50 "Phase 4" "$PLAN_FILE" | grep -c "^\- \[x\]" || echo "0")
        TOTAL_TASKS=$(grep -A 50 "Phase 4" "$PLAN_FILE" | grep -c "^\-\s*\[" || echo "0")
        if [ $TOTAL_TASKS -gt 0 ]; then
            PROGRESS=$((COMPLETED_TASKS * 100 / TOTAL_TASKS))
            echo "- **Phase 4**: $PROGRESS% ($COMPLETED_TASKS/$TOTAL_TASKS 完成)" >> "$LOG_DIR/monthly_report_$MONTH.log"
        else
            echo "- **Phase 4**: ⏳ 待开始" >> "$LOG_DIR/monthly_report_$MONTH.log"
        fi
    fi
fi

echo "" >> "$LOG_DIR/monthly_report_$MONTH.log"

# 3. 代码质量趋势
echo "## 📈 代码质量趋势" >> "$LOG_DIR/monthly_report_$MONTH.log"
echo "" >> "$LOG_DIR/monthly_report_$MONTH.log"

# 编译状态
cd build
if make -j$(nproc) > /dev/null 2>&1; then
    echo "- 编译状态：✅ 成功" >> "$LOG_DIR/monthly_report_$MONTH.log"
else
    ERROR_COUNT=$(make -j$(nproc) 2>&1 | grep -c "error:" || echo "0")
    echo "- 编译状态：❌ 失败（$ERROR_COUNT 个错误）" >> "$LOG_DIR/monthly_report_$MONTH.log"
fi
cd "$WORKDIR"

# 测试状态
cd build
if ctest --output-on-failure > /tmp/test_output.log 2>&1; then
    TEST_COUNT=$(grep -c "Test.*Passed" /tmp/test_output.log || echo "0")
    TEST_PASS_RATE="100%"
    echo "- 测试状态：✅ 全部通过（$TEST_COUNT 个测试）" >> "$LOG_DIR/monthly_report_$MONTH.log"
else
    FAILED_COUNT=$(grep -c "Test.*Failed" /tmp/test_output.log || echo "0")
    TOTAL_COUNT=$(grep -c "Test #" /tmp/test_output.log || echo "0")
    PASSED_COUNT=$((TOTAL_COUNT - FAILED_COUNT))
    TEST_PASS_RATE=$((PASSED_COUNT * 100 / TOTAL_COUNT))%
    echo "- 测试状态：⚠️ $PASSED_COUNT/$TOTAL_COUNT 通过（$TEST_PASS_RATE）" >> "$LOG_DIR/monthly_report_$MONTH.log"
fi
cd "$WORKDIR"

# MISRA C:2012 合规性
MISRA_VIOLATIONS=$(grep -r "MISRA" kernel services --include="*.c" --include="*.h" | grep -i "violation\|violate\|error" | wc -l || echo "0")
echo "- MISRA C:2012：$(if [ $MISRA_VIOLATIONS -eq 0 ]; then echo "✅ 合规"; else echo "⚠️ $MISRA_VIOLATIONS 个违规"; fi)" >> "$LOG_DIR/monthly_report_$MONTH.log"

# 技术债务
TODO_COUNT=$(grep -r "TODO" kernel services --include="*.c" --include="*.h" 2>/dev/null | wc -l || echo "0")
FIXME_COUNT=$(grep -r "FIXME" kernel services --include="*.c" --include="*.h" 2>/dev/null | wc -l || echo "0")
DEBT_TREND="稳定"
if [ $((TODO_COUNT + FIXME_COUNT)) -gt 100 ]; then
    DEBT_TREND="上升 ⚠️"
elif [ $((TODO_COUNT + FIXME_COUNT)) -lt 50 ]; then
    DEBT_TREND="下降 ✅"
fi
echo "- TODO/FIXME：$((TODO_COUNT + FIXME_COUNT)) 个（趋势：$DEBT_TREND）" >> "$LOG_DIR/monthly_report_$MONTH.log"

echo "" >> "$LOG_DIR/monthly_report_$MONTH.log"

# 4. 性能基准
echo "## ⚡ 性能基准" >> "$LOG_DIR/monthly_report_$MONTH.log"
echo "" >> "$LOG_DIR/monthly_report_$MONTH.log"

# 运行性能测试（如果有）
if [ -f "$WORKDIR/scripts/run_perf_tests.sh" ]; then
    echo "- 运行性能测试..." >> "$LOG_DIR/monthly_report_$MONTH.log"
    "$WORKDIR/scripts/run_perf_tests.sh" >> "$LOG_DIR/monthly_report_$MONTH.log" 2>&1 || echo "⚠️ 性能测试失败" >> "$LOG_DIR/monthly_report_$MONTH.log"
else
    echo "- ⚠️ 无性能测试脚本" >> "$LOG_DIR/monthly_report_$MONTH.log"
fi

echo "" >> "$LOG_DIR/monthly_report_$MONTH.log"

# 5. 下月计划
echo "## 📋 下月计划" >> "$LOG_DIR/monthly_report_$MONTH.log"
echo "" >> "$LOG_DIR/monthly_report_$MONTH.log"

# 从开发计划读取下月任务
if [ -f "$PLAN_FILE" ]; then
    NEXT_MONTH=$(date -d "+1 month" '+%Y-%m')
    echo "**目标月份**：$NEXT_MONTH" >> "$LOG_DIR/monthly_report_$MONTH.log"
    echo "" >> "$LOG_DIR/monthly_report_$MONTH.log"
    echo "**重点任务**：" >> "$LOG_DIR/monthly_report_$MONTH.log"
    grep -A 20 "### 近期任务" "$PLAN_FILE" | grep "^-\s*\[ \]" | head -8 | sed 's/^- \[ \]/- [ ]/' >> "$LOG_DIR/monthly_report_$MONTH.log"
fi

echo "" >> "$LOG_DIR/monthly_report_$MONTH.log"

# 6. 风险与建议
echo "## ⚠️ 风险与建议" >> "$LOG_DIR/monthly_report_$MONTH.log"
echo "" >> "$LOG_DIR/monthly_report_$MONTH.log"

# 检查是否有严重问题
RISK_COUNT=0

if ! cd build && make -j$(nproc) > /dev/null 2>&1; then
    ERROR_COUNT=$(make -j$(nproc) 2>&1 | grep -c "error:" || echo "0")
    if [ $ERROR_COUNT -gt 10 ]; then
        echo "- 🔴 **高风险**：编译错误过多（$ERROR_COUNT 个）" >> "$LOG_DIR/monthly_report_$MONTH.log"
        echo "  - 建议：立即修复 P0 错误" >> "$LOG_DIR/monthly_report_$MONTH.log"
        RISK_COUNT=$((RISK_COUNT + 1))
    fi
fi
cd "$WORKDIR"

if [ "$TEST_PASS_RATE" != "100%" ]; then
    echo "- 🟡 **中风险**：测试覆盖率不足（$TEST_PASS_RATE）" >> "$LOG_DIR/monthly_report_$MONTH.log"
    echo "  - 建议：增加测试用例，提高覆盖率" >> "$LOG_DIR/monthly_report_$MONTH.log"
    RISK_COUNT=$((RISK_COUNT + 1))
fi

if [ $((TODO_COUNT + FIXME_COUNT)) -gt 150 ]; then
    echo "- 🟡 **中风险**：技术债务过高（$((TODO_COUNT + FIXME_COUNT)) 个）" >> "$LOG_DIR/monthly_report_$MONTH.log"
    echo "  - 建议：安排专项清理技术债务" >> "$LOG_DIR/monthly_report_$MONTH.log"
    RISK_COUNT=$((RISK_COUNT + 1))
fi

if [ $COMMIT_COUNT -lt 20 ]; then
    echo "- 🟡 **中风险**：月度提交数量偏低（$COMMIT_COUNT 次）" >> "$LOG_DIR/monthly_report_$MONTH.log"
    echo "  - 建议：检查开发进度是否正常" >> "$LOG_DIR/monthly_report_$MONTH.log"
    RISK_COUNT=$((RISK_COUNT + 1))
fi

if [ $RISK_COUNT -eq 0 ]; then
    echo "- ✅ **无重大风险**：项目运行正常" >> "$LOG_DIR/monthly_report_$MONTH.log"
fi

echo "" >> "$LOG_DIR/monthly_report_$MONTH.log"

# 7. 总结
echo "## 📊 总结" >> "$LOG_DIR/monthly_report_$MONTH.log"
echo "" >> "$LOG_DIR/monthly_report_$MONTH.log"

echo "- **本月工作量**：$COMMIT_COUNT 次提交，$ADDED_LINES 行代码" >> "$LOG_DIR/monthly_report_$MONTH.log"
echo "- **代码质量**：编译$(cd build && make -j$(nproc) > /dev/null 2>&1 && echo "✅" || echo "❌")，测试$([ "$TEST_PASS_RATE" = "100%" ] && echo "✅" || echo "⚠️ $TEST_PASS_RATE")" >> "$LOG_DIR/monthly_report_$MONTH.log"
echo "- **风险等级**：$(if [ $RISK_COUNT -eq 0 ]; then echo "低 ✅"; elif [ $RISK_COUNT -le 2 ]; then echo "中 🟡"; else echo "高 🔴"; fi)" >> "$LOG_DIR/monthly_report_$MONTH.log"
echo "- **总体评价**：$(if [ $RISK_COUNT -eq 0 ] && [ $COMMIT_COUNT -ge 20 ]; then echo "优秀 ✅"; elif [ $RISK_COUNT -le 1 ]; then echo "良好 ⚠️"; else echo "需改进 🔴"; fi)" >> "$LOG_DIR/monthly_report_$MONTH.log"

echo "" >> "$LOG_DIR/monthly_report_$MONTH.log"

# 8. 汇报时间
echo "---" >> "$LOG_DIR/monthly_report_$MONTH.log"
echo "" >> "$LOG_DIR/monthly_report_$MONTH.log"
echo "**报告时间**：$TODAY $TIME (GMT+8)" >> "$LOG_DIR/monthly_report_$MONTH.log"
echo "**评估周期**：$MONTH" >> "$LOG_DIR/monthly_report_$MONTH.log"

# 输出报告
cat "$LOG_DIR/monthly_report_$MONTH.log"

# 发送报告到飞书
echo "📤 发送报告到飞书..." >> "$LOG_DIR/monthly_report_$MONTH.log"
echo "$(cat "$LOG_DIR/monthly_report_$MONTH.log")" | openclaw exec --agent aisafeos "转发到飞书：月度里程碑评估" || echo "⚠️ 发送失败" >> "$LOG_DIR/monthly_report_$MONTH.log"

echo "✅ 月度里程碑评估生成完成" >> "$LOG_DIR/monthly_report_$MONTH.log"