#!/bin/bash
# AISafeOS64 Agent使用情况监控脚本

echo "=== OpenClaw Agent 使用统计 ==="
echo ""

# 检查配置
echo "📊 当前配置:"
CONFIG_FILE="$HOME/.openclaw/openclaw.json"
if [ -f "$CONFIG_FILE" ]; then
    MAX_CONCURRENT=$(grep -A 2 '"maxConcurrent"' "$CONFIG_FILE" | head -1 | grep -o '[0-9]*' | head -1)
    SUB_MAX=$(grep -A 4 '"subagents"' "$CONFIG_FILE" | grep '"maxConcurrent"' | grep -o '[0-9]*')
    PRIMARY_MODEL=$(grep -A 5 '"id": "aisafeos"' "$CONFIG_FILE" | grep '"primary"' | cut -d'"' -f4)

    echo "  主并发数: ${MAX_CONCURRENT:-未知}"
    echo "  子Agent并发: ${SUB_MAX:-未知}"
    echo "  aisafeos主模型: ${PRIMARY_MODEL:-未知}"
else
    echo "  配置文件不存在"
fi
echo ""

# 检查当前运行中的agents
echo "🔄 当前运行中的Agents:"
RUNS_FILE="$HOME/.openclaw/subagents/runs.json"
if [ -f "$RUNS_FILE" ]; then
    RUNNING_COUNT=$(python3 -c "import json,os; d=json.load(open(os.path.expanduser('$RUNS_FILE'))); print(len([r for r in d.get('runs',{}).values() if 'endedAt' not in r]))" 2>/dev/null || echo "0")
    echo "  $RUNNING_COUNT 个运行中"
else
    echo "  无运行中的agents"
fi
echo ""

# 统计历史任务
echo "📈 历史任务统计:"
if [ -f "$RUNS_FILE" ]; then
    python3 -c "
import json,os
from datetime import datetime

try:
    with open(os.path.expanduser('$RUNS_FILE')) as f:
        data = json.load(f)
    runs = data.get('runs', {})
    total = len(runs)
    outcomes = [r.get('outcome', {}).get('status') for r in runs.values()]
    ok = outcomes.count('ok')
    timeout = outcomes.count('timeout')
    other = total - ok - timeout

    print(f'  总任务数: {total}')
    print(f'  成功: {ok} ({ok*100//total if total>0 else 0}%)')
    print(f'  超时: {timeout} ({timeout*100//total if total>0 else 0}%)')
    print(f'  其他: {other}')
except Exception as e:
    print(f'  错误: {e}')
" 2>/dev/null

    echo ""
    echo "📅 今日任务:"
    python3 -c "
import json,os
from datetime import datetime

try:
    with open(os.path.expanduser('$RUNS_FILE')) as f:
        data = json.load(f)
    today = datetime.now().strftime('%Y-%m-%d')
    today_runs = [r for r in data.get('runs',{}).values() if datetime.fromtimestamp(r['createdAt']/1000).strftime('%Y-%m-%d')==today]
    print(f'  任务数: {len(today_runs)}')
    if today_runs:
        today_outcomes = [r.get('outcome',{}).get('status') for r in today_runs]
        print(f'  成功: {today_outcomes.count(\"ok\")}')
        print(f'  超时: {today_outcomes.count(\"timeout\")}')
        durations = [r.get('accumulatedRuntimeMs',0) for r in today_runs]
        avg_duration = sum(durations)/len(durations)/1000 if durations else 0
        print(f'  平均耗时: {avg_duration:.1f}秒')
except:
    print('  无数据')
" 2>/dev/null
else
    echo "  无历史记录"
fi
echo ""

# 诊断建议
echo "💡 诊断建议:"
echo "  如遇到Rate Limit，请查看优化指南："
echo "  /home/kerfs/AISafeOS64/AISafeOS64/AGENT_OPTIMIZATION.md"
echo ""

echo "📝 相关文件:"
echo "  OpenClaw配置: ~/.openclaw/openclaw.json"
echo "  Agent记录: ~/.openclaw/subagents/runs.json"
