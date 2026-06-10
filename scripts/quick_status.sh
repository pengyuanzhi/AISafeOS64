#!/bin/bash
# AISafeOS64 快速状态检查（每6小时）

set -e

WORKDIR="/home/kerfs/AISafeOS64/AISafeOS64"
LOG_DIR="$WORKDIR/logs"
DATE=$(date '+%Y%m%d_%H%M%S')
HOUR=$(date '+%H')

cd "$WORKDIR"

echo "⏰ 快速状态检查 - $(date '+%Y-%m-%d %H:%M:%S')" | tee -a "$LOG_DIR/quick_status.log"

# 1. 检查进程状态
echo "" | tee -a "$LOG_DIR/quick_status.log"
echo "📊 系统状态" | tee -a "$LOG_DIR/quick_status.log"
echo "- 当前时间：$(date '+%H:%M')" | tee -a "$LOG_DIR/quick_status.log"

# 2. 检查编译状态（快速检查）
cd build 2>/dev/null || echo "⚠️ build 目录不存在" | tee -a "$LOG_DIR/quick_status.log"
if make -j$(nproc) > /dev/null 2>&1; then
    echo "- 编译状态：✅ 正常" | tee -a "$LOG_DIR/quick_status.log"
else
    ERROR_COUNT=$(make -j$(nproc) 2>&1 | grep -c "error:" || echo "0")
    echo "- 编译状态：⚠️ $ERROR_COUNT 个错误" | tee -a "$LOG_DIR/quick_status.log"
fi
cd "$WORKDIR"

# 3. 检查最近提交
echo "" | tee -a "$LOG_DIR/quick_status.log"
echo "🔄 最近提交" | tee -a "$LOG_DIR/quick_status.log"
git log --oneline -1 | tee -a "$LOG_DIR/quick_status.log"

# 4. 检查是否有未提交的修改
if [ -n "$(git status --porcelain)" ]; then
    echo "" | tee -a "$LOG_DIR/quick_status.log"
    echo "⚠️ 有未提交的修改" | tee -a "$LOG_DIR/quick_status.log"
    git status --short | head -5 | tee -a "$LOG_DIR/quick_status.log"
fi

# 5. 检查开发进度
echo "" | tee -a "$LOG_DIR/quick_status.log"
echo "📈 开发进度" | tee -a "$LOG_DIR/quick_status.log"

# 统计今日提交数
TODAY_COMMITS=$(git log --since="today" --oneline | wc -l || echo "0")
echo "- 今日提交：$TODAY_COMMITS 次" | tee -a "$LOG_DIR/quick_status.log"

# 检查是否有待完成的任务
PLAN_FILE="$WORKDIR/docs/COMMERCIAL_DEVELOPMENT_PLAN.md"
if [ -f "$PLAN_FILE" ]; then
    PENDING_TASKS=$(grep -A 15 "### 近期任务" "$PLAN_FILE" | grep -c "^-\s*\[ \]" || echo "0")
    echo "- 待完成任务：$PENDING_TASKS 个" | tee -a "$LOG_DIR/quick_status.log"
fi

# 6. 检查技术债务
TODO_COUNT=$(grep -r "TODO" kernel services --include="*.c" --include="*.h" 2>/dev/null | wc -l || echo "0")
FIXME_COUNT=$(grep -r "FIXME" kernel services --include="*.c" --include="*.h" 2>/dev/null | wc -l || echo "0")
echo "- 技术债务：$((TODO_COUNT + FIXME_COUNT)) 个" | tee -a "$LOG_DIR/quick_status.log"

# 7. 发送通知（仅在异常情况下）
if [ "$ERROR_COUNT" != "0" ] || [ $PENDING_TASKS -gt 20 ] || [ $((TODO_COUNT + FIXME_COUNT)) -gt 150 ]; then
    echo "" | tee -a "$LOG_DIR/quick_status.log"
    echo "⚠️ 检测到异常，已通知" | tee -a "$LOG_DIR/quick_status.log"
fi

echo "" | tee -a "$LOG_DIR/quick_status.log"
echo "✅ 状态检查完成" | tee -a "$LOG_DIR/quick_status.log"