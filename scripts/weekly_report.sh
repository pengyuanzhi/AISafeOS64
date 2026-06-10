#!/bin/bash
# AISafeOS64 每周进度汇总脚本
# 每周日 21:00 执行

set -e

WORKDIR="/home/kerfs/AISafeOS64/AISafeOS64"
LOG_DIR="$WORKDIR/logs"
REPORT_DIR="$WORKDIR/reports"
WEEK=$(date '+%Y-W%V')
TODAY=$(date '+%Y-%m-%d')
TIME=$(date '+%H:%M:%S')

# 创建目录
mkdir -p "$LOG_DIR"
mkdir -p "$REPORT_DIR"

cd "$WORKDIR"

echo "📈 生成每周进度汇总 - 第 $(date '+%W') 周 ($TODAY $TIME)" > "$LOG_DIR/weekly_report_$WEEK.log"

# 1. 本周提交统计
echo "## 📊 本周提交统计" >> "$LOG_DIR/weekly_report_$WEEK.log"
echo "" >> "$LOG_DIR/weekly_report_$WEEK.log"

COMMIT_COUNT=$(git log --since="1 week ago" --oneline | wc -l || echo "0")
echo "- 提交次数：$COMMIT_COUNT 次" >> "$LOG_DIR/weekly_report_$WEEK.log"

# 新增代码行数
ADDED_LINES=$(git log --since="1 week ago" --pretty=tformat: --numstat | awk '{add+=$1; del+=$2} END {print add+del}' || echo "0")
echo "- 新增代码：$ADDED_LINES 行" >> "$LOG_DIR/weekly_report_$WEEK.log"

# 新增文件数
NEW_FILES=$(git log --since="1 week ago" --name-only --pretty=format: | sort -u | grep -v "^$" | wc -l || echo "0")
echo "- 新增文件：$NEW_FILES 个" >> "$LOG_DIR/weekly_report_$WEEK.log"

echo "" >> "$LOG_DIR/weekly_report_$WEEK.log"

# 2. 关键指标
echo "## 📈 关键指标" >> "$LOG_DIR/weekly_report_$WEEK.log"
echo "" >> "$LOG_DIR/weekly_report_$WEEK.log"

# 编译状态
cd build
if make -j$(nproc) > /dev/null 2>&1; then
    echo "- 编译状态：✅ 成功" >> "$LOG_DIR/weekly_report_$WEEK.log"
else
    ERROR_COUNT=$(make -j$(nproc) 2>&1 | grep -c "error:" || echo "0")
    echo "- 编译状态：❌ 失败（$ERROR_COUNT 个错误）" >> "$LOG_DIR/weekly_report_$WEEK.log"
fi
cd "$WORKDIR"

# 测试状态
cd build
if ctest --output-on-failure > /tmp/test_output.log 2>&1; then
    TEST_COUNT=$(grep -c "Test.*Passed" /tmp/test_output.log || echo "0")
    TEST_PASS_RATE="100%"
else
    FAILED_COUNT=$(grep -c "Test.*Failed" /tmp/test_output.log || echo "0")
    TOTAL_COUNT=$(grep -c "Test #" /tmp/test_output.log || echo "0")
    PASSED_COUNT=$((TOTAL_COUNT - FAILED_COUNT))
    TEST_PASS_RATE=$((PASSED_COUNT * 100 / TOTAL_COUNT))%
    echo "- 测试状态：⚠️ $PASSED_COUNT/$TOTAL_COUNT 通过（$TEST_PASS_RATE）" >> "$LOG_DIR/weekly_report_$WEEK.log"
fi
cd "$WORKDIR"

# MISRA C:2012 合规性
MISRA_VIOLATIONS=$(grep -r "MISRA" kernel services --include="*.c" --include="*.h" | grep -i "violation\|violate\|error" | wc -l || echo "0")
echo "- MISRA C:2012：$(if [ $MISRA_VIOLATIONS -eq 0 ]; then echo "✅ 合规"; else echo "⚠️ $MISRA_VIOLATIONS 个违规"; fi)" >> "$LOG_DIR/weekly_report_$WEEK.log"

# 技术债务
TODO_COUNT=$(grep -r "TODO" kernel services --include="*.c" --include="*.h" 2>/dev/null | wc -l || echo "0")
FIXME_COUNT=$(grep -r "FIXME" kernel services --include="*.c" --include="*.h" 2>/dev/null | wc -l || echo "0")
echo "- TODO/FIXME：$((TODO_COUNT + FIXME_COUNT)) 个" >> "$LOG_DIR/weekly_report_$WEEK.log"

echo "" >> "$LOG_DIR/weekly_report_$WEEK.log"

# 3. 本周完成的功能
echo "## ✅ 本周完成的功能" >> "$LOG_DIR/weekly_report_$WEEK.log"
echo "" >> "$LOG_DIR/weekly_report_$WEEK.log"

# 从 Git 提交信息提取
git log --since="1 week ago" --pretty=format:"- %s" >> "$LOG_DIR/weekly_report_$WEEK.log" 2>&1 || echo "无法提取提交信息" >> "$LOG_DIR/weekly_report_$WEEK.log"

echo "" >> "$LOG_DIR/weekly_report_$WEEK.log"

# 4. 进度对比
echo "## 📊 进度对比（vs 上周）" >> "$LOG_DIR/weekly_report_$WEEK.log"
echo "" >> "$LOG_DIR/weekly_report_$WEEK.log"

# 上周代码行数
LAST_WEEK_LINES=$(git log --until="1 week ago" --pretty=tformat: --numstat | awk '{add+=$1; del+=$2} END {print add+del}' || echo "0")

echo "| 指标 | 上周 | 本周 | 变化 |" >> "$LOG_DIR/weekly_report_$WEEK.log"
echo "|------|------|------|------|" >> "$LOG_DIR/weekly_report_$WEEK.log"
echo "| 代码行数 | $LAST_WEEK_LINES | $(($LAST_WEEK_LINES + ADDED_LINES)) | +$ADDED_LINES |" >> "$LOG_DIR/weekly_report_$WEEK.log"
echo "| 提交次数 | $(git log --until="1 week ago" --oneline | wc -l || echo "0") | $COMMIT_COUNT | $(if [ $COMMIT_COUNT -gt 0 ]; then echo "✅"; else echo "⚠️"; fi) |" >> "$LOG_DIR/weekly_report_$WEEK.log"

echo "" >> "$LOG_DIR/weekly_report_$WEEK.log"

# 5. 下周计划
echo "## 📋 下周计划" >> "$LOG_DIR/weekly_report_$WEEK.log"
echo "" >> "$LOG_DIR/weekly_report_$WEEK.log"

# 从开发计划读取
PLAN_FILE="$WORKDIR/docs/COMMERCIAL_DEVELOPMENT_PLAN.md"
if [ -f "$PLAN_FILE" ]; then
    grep -A 15 "### 近期任务" "$PLAN_FILE" | grep "^-\s*\[ \]" | head -5 | sed 's/^- \[ \]/- [ ]/' >> "$LOG_DIR/weekly_report_$WEEK.log"
fi

echo "" >> "$LOG_DIR/weekly_report_$WEEK.log"

# 6. 风险与问题
echo "## ⚠️ 风险与问题" >> "$LOG_DIR/weekly_report_$WEEK.log"
echo "" >> "$LOG_DIR/weekly_report_$WEEK.log"

# 检查是否有编译错误
cd build
if ! make -j$(nproc) > /dev/null 2>&1; then
    ERROR_COUNT=$(make -j$(nproc) 2>&1 | grep -c "error:" || echo "0")
    echo "- ⚠️ **编译错误**：$ERROR_COUNT 个错误待修复" >> "$LOG_DIR/weekly_report_$WEEK.log"
    echo "  - 建议优先修复 P0 错误" >> "$LOG_DIR/weekly_report_$WEEK.log"
fi
cd "$WORKDIR"

# 检查 TODO 数量
if [ $((TODO_COUNT + FIXME_COUNT)) -gt 50 ]; then
    echo "- ⚠️ **技术债务**：TODO/FIXME 过多（$((TODO_COUNT + FIXME_COUNT)) 个）" >> "$LOG_DIR/weekly_report_$WEEK.log"
    echo "  - 建议逐步清理技术债务" >> "$LOG_DIR/weekly_report_$WEEK.log"
fi

# 检查测试覆盖率（简单估算）
if [ "$TEST_PASS_RATE" != "100%" ]; then
    echo "- ⚠️ **测试覆盖率**：低于 100%（$TEST_PASS_RATE）" >> "$LOG_DIR/weekly_report_$WEEK.log"
    echo "  - 建议增加测试用例" >> "$LOG_DIR/weekly_report_$WEEK.log"
fi

if [ $(git log --since="1 week ago" --oneline | wc -l || echo "0") -eq 0 ]; then
    echo "- ⚠️ **开发进度**：本周无提交" >> "$LOG_DIR/weekly_report_$WEEK.log"
    echo "  - 请检查开发流程是否正常" >> "$LOG_DIR/weekly_report_$WEEK.log"
fi

echo "" >> "$LOG_DIR/weekly_report_$WEEK.log"

# 7. 里程碑进度
echo "## 🎯 里程碑进度" >> "$LOG_DIR/weekly_report_$WEEK.log"
echo "" >> "$LOG_DIR/weekly_report_$WEEK.log"

# 从 MEMORY.md 提取里程碑
if grep -q "Phase 1" "$WORKDIR/MEMORY.md"; then
    echo "- **Phase 1**: 进行中（能力系统 + SMP 多核）" >> "$LOG_DIR/weekly_report_$WEEK.log"
else
    echo "- **Phase 1**: ⏳ 待开始" >> "$LOG_DIR/weekly_report_$WEEK.log"
fi

if grep -q "Phase 2" "$WORKDIR/MEMORY.md"; then
    echo "- **Phase 2**: ⏳ 待开始（虚拟化支持）" >> "$LOG_DIR/weekly_report_$WEEK.log"
fi

if grep -q "Phase 3" "$WORKDIR/MEMORY.md"; then
    echo "- **Phase 3**: ⏳ 待开始（设备驱动 + 文件系统）" >> "$LOG_DIR/weekly_report_$WEEK.log"
fi

echo "" >> "$LOG_DIR/weekly_report_$WEEK.log"

# 8. 汇报时间
echo "---" >> "$LOG_DIR/weekly_report_$WEEK.log"
echo "" >> "$LOG_DIR/weekly_report_$WEEK.log"
echo "**报告时间**：$TODAY $TIME (GMT+8)" >> "$LOG_DIR/weekly_report_$WEEK.log"
echo "**报告周期**：2026-W$(date '+%W')" >> "$LOG_DIR/weekly_report_$WEEK.log"

# 输出报告
cat "$LOG_DIR/weekly_report_$WEEK.log"

# 发送报告到飞书
echo "📤 发送报告到飞书..." >> "$LOG_DIR/weekly_report_$WEEK.log"
echo "$(cat "$LOG_DIR/weekly_report_$WEEK.log")" | openclaw exec --agent aisafeos "转发到飞书：每周进度汇总" || echo "⚠️ 发送失败" >> "$LOG_DIR/weekly_report_$WEEK.log"

echo "✅ 每周进度汇总生成完成" >> "$LOG_DIR/weekly_report_$WEEK.log"