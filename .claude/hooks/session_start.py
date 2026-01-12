#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Claude Code Hook: SessionStart

触发时机: 当 Claude Code 启动新会话或恢复现有会话时运行
功能: 记录会话开始信息
"""

import sys
import json
from datetime import datetime
from pathlib import Path

# 项目根目录
PROJECT_ROOT = Path(__file__).parent.parent.parent
PROMPT_DIR = PROJECT_ROOT / "prompt"
SESSION_LOG = PROMPT_DIR / "sessions.md"

# 确保目录存在
PROMPT_DIR.mkdir(exist_ok=True)


def handle_hook(event_data):
    """
    处理 SessionStart 事件

    Args:
        event_data: Hook 事件数据

    Returns:
        dict: Hook 响应（可选）
    """
    # 提取数据
    session_id = event_data.get('sessionId', 'unknown')
    model = event_data.get('model', 'unknown')
    is_resume = event_data.get('isResume', False)
    working_dir = event_data.get('workingDirectory', str(PROJECT_ROOT))

    # 生成时间戳和短会话ID
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    session_short_id = datetime.now().strftime("%Y%m%d-%H%M%S")
    session_type = "恢复会话" if is_resume else "新会话"

    # 构建日志条目
    entry = f"""---
## 会话开始 - {session_short_id}

**时间**: {timestamp}
**会话ID**: {session_id}
**会话类型**: {session_type}
**模型**: {model}
**工作目录**: `{working_dir}`

---

"""

    # 如果是第一次运行，创建文件头
    if not SESSION_LOG.exists():
        with open(SESSION_LOG, 'w', encoding='utf-8') as f:
            f.write("# Claude Code 会话日志\n\n")
            f.write("> 本文件记录所有 Claude Code 会话的开始和结束\n\n")
            f.write("---\n\n")

    # 追加到文件
    with open(SESSION_LOG, 'a', encoding='utf-8') as f:
        f.write(entry)

    return None


def main():
    """主函数"""
    try:
        # 从标准输入读取 JSON 数据
        input_data = json.loads(sys.stdin.read())

        # 处理事件
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
