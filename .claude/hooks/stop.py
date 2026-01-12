#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Claude Code Hook: Stop

触发时机: 当 Claude Code 完成响应时运行
功能: 记录响应内容（可选，可能较大）
"""

import sys
import json
from datetime import datetime
from pathlib import Path

# 项目根目录
PROJECT_ROOT = Path(__file__).parent.parent.parent
PROMPT_DIR = PROJECT_ROOT / "prompt"
PROMPT_LOG = PROMPT_DIR / "prompt.md"


def handle_hook(event_data):
    """
    处理 Stop 事件

    Args:
        event_data: Hook 事件数据

    Returns:
        dict: Hook 响应（可选）
    """
    # 提取数据
    session_id = event_data.get('sessionId', 'unknown')
    response_content = event_data.get('response', '')
    tool_calls = event_data.get('toolCalls', [])

    # 只记录工具调用摘要，不记录完整响应（避免文件过大）
    if tool_calls:
        tools_used = [call.get('name', 'unknown') for call in tool_calls]
        tools_summary = ", ".join(tools_used)
    else:
        tools_summary = "无"

    # 生成时间戳
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    # 追加到 prompt.md（在对应的提示词记录后）
    entry = f"""### AI 响应

**完成时间**: {timestamp}
**使用工具**: {tools_summary}

"""

    with open(PROMPT_LOG, 'a', encoding='utf-8') as f:
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
