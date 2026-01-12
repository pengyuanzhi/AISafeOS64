#!/usr/bin/env python3
"""
Claude Code Hook: 记录 AI 响应到 prompt/prompt.md

触发时机: assistant-response (假设支持)
功能: 记录 AI 的响应内容
"""

import os
import sys
from datetime import datetime
from pathlib import Path

# 项目根目录
PROJECT_ROOT = Path(__file__).parent.parent.parent
PROMPT_LOG = PROJECT_ROOT / "prompt" / "prompt.md"


def format_timestamp():
    """格式化时间戳"""
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def append_response_log(session_id, response_content, tool_calls=None):
    """
    追加响应记录到 prompt.md

    Args:
        session_id: 会话ID（从提示词记录中获取）
        response_content: AI的响应内容
        tool_calls: 工具调用记录
    """
    timestamp = format_timestamp()

    # 构建响应条目
    entry = f"""### AI 响应

{response_content}

"""

    # 如果有工具调用，添加工具调用记录
    if tool_calls:
        entry += f"""### 工具调用

```json
{tool_calls}
```

"""

    entry += f"""**响应时间**: {timestamp}

---

"""

    # 追加到文件
    with open(PROMPT_LOG, 'a', encoding='utf-8') as f:
        f.write(entry)

    return 0


def main():
    """主函数"""
    # 从命令行参数或环境变量获取信息
    if len(sys.argv) > 1:
        session_id = sys.argv[1]
    else:
        session_id = os.getenv('CLAUDE_SESSION_ID', 'unknown')

    response_content = os.getenv('CLAUDE_RESPONSE', '')
    tool_calls = os.getenv('CLAUDE_TOOL_CALLS', '{}')

    append_response_log(
        session_id=session_id,
        response_content=response_content,
        tool_calls=tool_calls
    )

    return 0


if __name__ == '__main__':
    sys.exit(main())
