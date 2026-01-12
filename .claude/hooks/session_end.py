#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Claude Code Hook: SessionEnd

触发时机: 当 Claude Code 会话结束时运行
功能: 记录会话结束信息和统计
"""

import sys
import json
from datetime import datetime
from pathlib import Path

# 项目根目录
PROJECT_ROOT = Path(__file__).parent.parent.parent
PROMPT_DIR = PROJECT_ROOT / "prompt"
SESSION_LOG = PROMPT_DIR / "sessions.md"


def handle_hook(event_data):
    """
    处理 SessionEnd 事件

    Args:
        event_data: Hook 事件数据

    Returns:
        dict: Hook 响应（可选）
    """
    # 提取数据
    session_id = event_data.get('sessionId', 'unknown')
    duration_seconds = event_data.get('durationSeconds', 0)
    prompt_count = event_data.get('promptCount', 0)
    tool_use_count = event_data.get('toolUseCount', 0)

    # 生成时间戳
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    # 格式化持续时间
    if duration_seconds > 3600:
        duration = f"{duration_seconds // 3600}h {(duration_seconds % 3600) // 60}m"
    elif duration_seconds > 60:
        duration = f"{duration_seconds // 60}m {duration_seconds % 60}s"
    else:
        duration = f"{duration_seconds}s"

    # 构建日志条目
    entry = f"""### 会话结束

**结束时间**: {timestamp}
**持续时间**: {duration}
**提示词数量**: {prompt_count}
**工具调用次数**: {tool_use_count}

---

"""

    # 追加到文件
    with open(SESSION_LOG, 'a', encoding='utf-8') as f:
        f.write(entry)

    return None


def main():
    """主函数"""
    try:
        input_data = json.loads(sys.stdin.read())
        result = handle_hook(input_data)

        if result:
            print(json.dumps(result))

        return 0

    except json.JSONDecodeError as e:
        print(f"[ERROR] Invalid JSON input: {e}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"[ERROR] Hook execution failed: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        return 1


if __name__ == '__main__':
    sys.exit(main())
