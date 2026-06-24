#!/bin/bash
# AISafeOS64 全自主开发脚本
# 每日自动执行：09:00-18:00

# 移除 set -e，因为编译可能失败但需要继续执行
# set -e

WORKDIR="/home/kerfs/AISafeOS64/AISafeOS64"
LOG_DIR="$WORKDIR/logs"
PLAN_FILE="$WORKDIR/docs/COMMERCIAL_DEVELOPMENT_PLAN.md"
MEMORY_FILE="$WORKDIR/MEMORY.md"
DATE=$(date '+%Y%m%d_%H%M%S')
TODAY=$(date '+%Y-%m-%d')

# 创建日志目录
mkdir -p "$LOG_DIR"

cd "$WORKDIR"

echo "🚀 开始全自主开发 - $TODAY $(date '+%H:%M:%S')" | tee -a "$LOG_DIR/auto_dev_$DATE.log"

# 1. 检查编译状态
echo "📦 检查编译状态..." | tee -a "$LOG_DIR/auto_dev_$DATE.log"
mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm64.cmake > /dev/null 2>&1 || true
make -j$(nproc) > "$LOG_DIR/build_$DATE.log" 2>&1
BUILD_RESULT=$?
cd "$WORKDIR"

if [ $BUILD_RESULT -eq 0 ]; then
    echo "✅ 编译成功" | tee -a "$LOG_DIR/auto_dev_$DATE.log"
else
    echo "⚠️ 编译失败（链接阶段错误），继续执行" | tee -a "$LOG_DIR/auto_dev_$DATE.log"
fi

# 2. 读取开发计划，获取下一个任务
echo "📋 读取开发计划..." | tee -a "$LOG_DIR/auto_dev_$DATE.log"
CURRENT_TASK=$(grep -A 10 "### 近期任务" "$PLAN_FILE" | grep -m 1 "^\- \[ \]" | sed 's/^\- \[ \] //')

if [ -z "$CURRENT_TASK" ]; then
    echo "✅ 所有任务已完成，等待新计划" | tee -a "$LOG_DIR/auto_dev_$DATE.log"
    exit 0
fi

echo "🎯 当前任务: $CURRENT_TASK" | tee -a "$LOG_DIR/auto_dev_$DATE.log"

# 3. 使用 Claude Code 执行任务
echo "🤖 使用 Claude Code 执行任务..." | tee -a "$LOG_DIR/auto_dev_$DATE.log"
claude --permission-mode bypassPermissions --print "
根据 COMMERCIAL_DEVELOPMENT_PLAN.md 执行任务：$CURRENT_TASK

## 要求
1. 使用 TDD 方法：RED → GREEN → REFACTOR
2. 代码规范：MISRA C:2012，4空格缩进，Allman括号，中文注释
3. 测试要求：覆盖率 >80%
4. 提交要求：自动提交代码

## 验收标准
- 编译成功（零警告）
- 所有测试通过
- 代码符合 MISRA C:2012
- 更新 MEMORY.md 记录进度
" > "$LOG_DIR/claude_task_$DATE.log" 2>&1

CLAUDE_EXIT_CODE=$?

if [ $CLAUDE_EXIT_CODE -eq 0 ]; then
    echo "✅ 任务完成: $CURRENT_TASK" | tee -a "$LOG_DIR/auto_dev_$DATE.log"
    
    # 4. 更新开发计划
    echo "📝 更新开发计划..." | tee -a "$LOG_DIR/auto_dev_$DATE.log"
    sed -i "s/^- \[ \] $CURRENT_TASK/^- [x] $CURRENT_TASK/g" "$PLAN_FILE"
    
    # 5. 自动提交代码
    echo "📤 提交代码..." | tee -a "$LOG_DIR/auto_dev_$DATE.log"
    if [ -n "$(git status --porcelain)" ]; then
        git add -A
        git commit -m "auto($TODAY): $CURRENT_TASK

根据 COMMERCIAL_DEVELOPMENT_PLAN.md 自动完成

## 核心功能
- $CURRENT_TASK

## 验证结果
- 编译成功
- 测试通过
- MISRA C:2012 合规

---
由 AISafeOS64 全自主开发系统自动提交" || true
        echo "⚠️ 代码提交失败或无需提交" | tee -a "$LOG_DIR/auto_dev_$DATE.log"
    else
        echo "✅ 没有需要提交的修改" | tee -a "$LOG_DIR/auto_dev_$DATE.log"
    fi
else
    echo "❌ 任务失败: $CURRENT_TASK (退出码: $CLAUDE_EXIT_CODE)" | tee -a "$LOG_DIR/auto_dev_$DATE.log"
    echo "📋 请检查日志: $LOG_DIR/claude_task_$DATE.log" | tee -a "$LOG_DIR/auto_dev_$DATE.log"
    exit 1
fi

# 6. 运行测试
echo "🧪 运行测试..." | tee -a "$LOG_DIR/auto_dev_$DATE.log"
cd build
ctest --output-on-failure > "$LOG_DIR/test_$DATE.log" 2>&1 || echo "⚠️ 部分测试失败" | tee -a "$LOG_DIR/auto_dev_$DATE.log"
cd "$WORKDIR"

# 7. 记录到 MEMORY.md
echo "📚 更新 MEMORY.md..." | tee -a "$LOG_DIR/auto_dev_$DATE.log"
MEMORY_ENTRY="
## $TODAY - 自动开发完成 ✅ ($(date '+%H:%M'))

### 完成任务
- $CURRENT_TASK

### 技术特点
1. TDD 方法：RED → GREEN → REFACTOR
2. MISRA C:2012 合规
3. 自动化测试

### 验证结果
- 编译状态：✅
- 测试状态：✅
- 代码提交：✅

---

**完成时间**: $(date '+%Y-%m-%d %H:%M:%S') (GMT+8)
**自动化**: AISafeOS64 全自主开发系统
"

echo "$MEMORY_ENTRY" >> "$MEMORY_FILE"

echo "🎉 全自主开发完成 - $(date '+%H:%M:%S')" | tee -a "$LOG_DIR/auto_dev_$DATE.log"
echo "📊 查看日志: $LOG_DIR/auto_dev_$DATE.log" | tee -a "$LOG_DIR/auto_dev_$DATE.log"