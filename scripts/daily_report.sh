#!/bin/bash
# AISafeOS64 每日进度报告脚本
# 每日 20:00 执行

set -e

WORKDIR="/home/kerfs/AISafeOS64/AISafeOS64"
LOG_DIR="$WORKDIR/logs"
REPORT_DIR="$WORKDIR/reports"
DATE=$(date '+%Y%m%d')
TODAY=$(date '+%Y-%m-%d')
TIME=$(date '+%H:%M:%S')

# 创建目录
mkdir -p "$LOG_DIR"
mkdir -p "$REPORT_DIR"

cd "$WORKDIR"

echo "📊 生成每日进度报告 - $TODAY $TIME" > "$LOG_DIR/daily_report_$DATE.log"

# 1. 获取最新提交
echo "## 🔄 最新提交（最近3条）" >> "$LOG_DIR/daily_report_$DATE.log"
echo '```' >> "$LOG_DIR/daily_report_$DATE.log"
git log --oneline -3 >> "$LOG_DIR/daily_report_$DATE.log" 2>&1 || echo "无法获取提交历史" >> "$LOG_DIR/daily_report_$DATE.log"
echo '```' >> "$LOG_DIR/daily_report_$DATE.log"
echo "" >> "$LOG_DIR/daily_report_$DATE.log"

# 2. 代码统计
echo "## 📈 代码统计" >> "$LOG_DIR/daily_report_$DATE.log"
echo "" >> "$LOG_DIR/daily_report_$DATE.log"
echo "| 模块 | 代码行数 |" >> "$LOG_DIR/daily_report_$DATE.log"
echo "|------|----------|" >> "$LOG_DIR/daily_report_$DATE.log"

# 统计各模块代码行数
for dir in kernel services tests drivers; do
    if [ -d "$dir" ]; then
        LINES=$(find "$dir" -name "*.c" -o -name "*.h" | xargs wc -l 2>/dev/null | tail -1 | awk '{print $1}' || echo "0")
        echo "| $dir | $LINES |" >> "$LOG_DIR/daily_report_$DATE.log"
    fi
done

# 总代码行数
TOTAL_LINES=$(find . -name "*.c" -o -name "*.h" | xargs wc -l 2>/dev/null | tail -1 | awk '{print $1}' || echo "0")
echo "| **总计** | **$TOTAL_LINES** |" >> "$LOG_DIR/daily_report_$DATE.log"
echo "" >> "$LOG_DIR/daily_report_$DATE.log"

# 3. 编译状态
echo "## 🔧 编译状态" >> "$LOG_DIR/daily_report_$DATE.log"
echo "" >> "$LOG_DIR/daily_report_$DATE.log"

mkdir -p build && cd build
if cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm64.cmake > /dev/null 2>&1; then
    if make -j$(nproc) > /dev/null 2>&1; then
        echo "- 编译状态：✅ 成功" >> "$LOG_DIR/daily_report_$DATE.log"
        # 获取内核大小
        if [ -f "build/aisafe64.elf" ]; then
            SIZE=$(aarch64-linux-gnu-size build/aisafe64.elf | grep -E "^\s+(text|data|bss)\s+" | awk '{sum+=$2} END {print sum}' || echo "未知")
            echo "- 内核大小：$SIZE 字节" >> "$LOG_DIR/daily_report_$DATE.log"
        fi
    else
        ERROR_COUNT=$(make -j$(nproc) 2>&1 | grep -c "error:" || echo "0")
        echo "- 编译状态：❌ 失败（$ERROR_COUNT 个错误）" >> "$LOG_DIR/daily_report_$DATE.log"
    fi
else
    echo "- 编译状态：⚠️ 配置失败" >> "$LOG_DIR/daily_report_$DATE.log"
fi
cd "$WORKDIR"

echo "" >> "$LOG_DIR/daily_report_$DATE.log"

# 4. 测试状态
echo "## 🧪 测试状态" >> "$LOG_DIR/daily_report_$DATE.log"
echo "" >> "$LOG_DIR/daily_report_$DATE.log"

cd build
if ctest --output-on-failure > /tmp/test_output.log 2>&1; then
    TEST_COUNT=$(grep -c "Test.*Passed" /tmp/test_output.log || echo "0")
    echo "- 测试状态：✅ 全部通过（$TEST_COUNT 个测试）" >> "$LOG_DIR/daily_report_$DATE.log"
else
    FAILED_COUNT=$(grep -c "Test.*Failed" /tmp/test_output.log || echo "0")
    TOTAL_COUNT=$(grep -c "Test #" /tmp/test_output.log || echo "0")
    PASSED_COUNT=$((TOTAL_COUNT - FAILED_COUNT))
    echo "- 测试状态：⚠️ 部分失败（$PASSED_COUNT/$TOTAL_COUNT 通过，$FAILED_COUNT 失败）" >> "$LOG_DIR/daily_report_$DATE.log"
fi
cd "$WORKDIR"

echo "" >> "$LOG_DIR/daily_report_$DATE.log"

# 5. 当前任务状态
echo "## 🔄 当前任务状态" >> "$LOG_DIR/daily_report_$DATE.log"
echo "" >> "$LOG_DIR/daily_report_$DATE.log"

# 从开发计划读取当前任务
PLAN_FILE="$WORKDIR/docs/COMMERCIAL_DEVELOPMENT_PLAN.md"
if [ -f "$PLAN_FILE" ]; then
    echo "### 近期任务" >> "$LOG_DIR/daily_report_$DATE.log"
    grep -A 15 "### 近期任务" "$PLAN_FILE" | grep "^-\s*\[ \]" | head -5 | sed 's/^- \[ \]/- [ ]/' >> "$LOG_DIR/daily_report_$DATE.log"
    echo "" >> "$LOG_DIR/daily_report_$DATE.log"
    
    echo "### 今日完成" >> "$LOG_DIR/daily_report_$DATE.log"
    grep -A 15 "### 近期任务" "$PLAN_FILE" | grep "^-\s*\[x\]" | tail -3 | sed 's/^- \[x\]/- [x]/' >> "$LOG_DIR/daily_report_$DATE.log"
    echo "" >> "$LOG_DIR/daily_report_$DATE.log"
fi

# 6. 技术债务
echo "## ⚠️ 技术债务" >> "$LOG_DIR/daily_report_$DATE.log"
echo "" >> "$LOG_DIR/daily_report_$DATE.log"

# 统计 TODO/FIXME
TODO_COUNT=$(grep -r "TODO" kernel services --include="*.c" --include="*.h" 2>/dev/null | wc -l || echo "0")
FIXME_COUNT=$(grep -r "FIXME" kernel services --include="*.c" --include="*.h" 2>/dev/null | wc -l || echo "0")
echo "- TODO/FIXME: $((TODO_COUNT + FIXME_COUNT)) 个" >> "$LOG_DIR/daily_report_$DATE.log"
echo "" >> "$LOG_DIR/daily_report_$DATE.log"

# 7. 项目进度估算
echo "## 📊 项目进度" >> "$LOG_DIR/daily_report_$DATE.log"
echo "" >> "$LOG_DIR/daily_report_$DATE.log"

# 简单估算：基于完成的功能模块
COMPLETED_MODULES=6  # 核心微内核、调度器、IPC、内存管理、网络、文件系统
TOTAL_MODULES=12     # 包括虚拟化、设备驱动、用户态服务、安全等
PROGRESS=$((COMPLETED_MODULES * 100 / TOTAL_MODULES))

echo "- 当前进度：约 $PROGRESS% " >> "$LOG_DIR/daily_report_$DATE.log"
echo "- 预计完成时间：2027-08（按计划）" >> "$LOG_DIR/daily_report_$DATE.log"
echo "" >> "$LOG_DIR/daily_report_$DATE.log"

# 8. 汇报时间
echo "---" >> "$LOG_DIR/daily_report_$DATE.log"
echo "" >> "$LOG_DIR/daily_report_$DATE.log"
echo "**报告时间**：$TODAY $TIME (GMT+8)" >> "$LOG_DIR/daily_report_$DATE.log"

# 输出报告
cat "$LOG_DIR/daily_report_$DATE.log"

# 发送报告到飞书（通过 OpenClaw）
echo "📤 发送报告到飞书..." >> "$LOG_DIR/daily_report_$DATE.log"
echo "$(cat "$LOG_DIR/daily_report_$DATE.log")" | openclaw exec --agent aisafeos "转发到飞书：每日进度报告" || echo "⚠️ 发送失败" >> "$LOG_DIR/daily_report_$DATE.log"

echo "✅ 每日进度报告生成完成" >> "$LOG_DIR/daily_report_$DATE.log"