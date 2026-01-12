#!/usr/bin/env python3
"""
Claude Code Hook: 记录用户提示词到 prompt/prompt.md

触发时机: user-prompt-submit
功能: 自动记录用户的提示词、上下文文件、会话信息到规范格式的 Markdown 文件
"""

import os
import sys
import json
from datetime import datetime
from pathlib import Path

# 项目根目录
PROJECT_ROOT = Path(__file__).parent.parent.parent
PROMPT_DIR = PROJECT_ROOT / "prompt"
PROMPT_LOG = PROMPT_DIR / "prompt.md"

# 确保目录存在
PROMPT_DIR.mkdir(exist_ok=True)


def format_timestamp():
    """格式化时间戳"""
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def escape_markdown(text):
    """转义 Markdown 特殊字符"""
    special_chars = ['\\', '`', '*', '_', '{', '}', '[', ']', '(', ')', '#', '+', '-', '.', '!']
    for char in special_chars:
        text = text.replace(char, '\\' + char)
    return text


def format_files_list(files):
    """格式化文件列表"""
    if not files:
        return "无"

    formatted = []
    for file_info in files:
        path = file_info.get('path', 'unknown')
        lines = file_info.get('lines', 'all')
        formatted.append(f"- `{path}`" + (f" (行: {lines})" if lines != "all" else ""))
    return "\n".join(formatted)


def generate_session_id():
    """生成会话ID（基于时间戳）"""
    return datetime.now().strftime("%Y%m%d-%H%M%S")


def append_prompt_log(user_message, context_files=None, session_info=None):
    """
    追加提示词记录到 prompt.md

    Args:
        user_message: 用户输入的提示词
        context_files: 上下文文件列表
        session_info: 会话信息（模型、工作目录等）
    """
    session_id = generate_session_id()
    timestamp = format_timestamp()

    # 从环境变量或参数获取信息
    if session_info is None:
        session_info = {}

    model = session_info.get('model', 'unknown')
    working_dir = session_info.get('working_dir', str(PROJECT_ROOT))
    session_type = session_info.get('session_type', 'new')  # new, resume

    # 构建 Markdown 条目
    entry = f"""---
## 提示词记录 #{session_id}

**时间**: {timestamp}
**会话类型**: {session_type}
**模型**: {model}
**工作目录**: `{working_dir}`

### 用户提示词

```
{user_message}
```

### 上下文文件

{format_files_list(context_files or [])}

### 会话元数据

```json
{{
  "session_id": "{session_id}",
  "timestamp": "{timestamp}",
  "model": "{model}",
  "working_dir": "{working_dir}",
  "context_file_count": {len(context_files) if context_files else 0}
}}
```

---

"""

    # 追加到文件
    with open(PROMPT_LOG, 'a', encoding='utf-8') as f:
        f.write(entry)

    # 同时保存单独的提示词文件（备份）
    individual_log = PROMPT_DIR / f"{session_id}.txt"
    with open(individual_log, 'w', encoding='utf-8') as f:
        f.write(f"# Claude Code 提示词记录\n\n")
        f.write(f"**时间**: {timestamp}\n")
        f.write(f"**会话ID**: {session_id}\n")
        f.write(f"**模型**: {model}\n\n")
        f.write(f"## 提示词\n\n{user_message}\n\n")
        if context_files:
            f.write(f"## 上下文文件\n\n{format_files_list(context_files)}\n\n")

    return session_id


def main():
    """主函数：从环境变量读取信息"""
    # Claude Code 通过环境变量传递信息
    user_message = os.getenv('CLAUDE_USER_PROMPT', '')
    context_files_json = os.getenv('CLAUDE_CONTEXT_FILES', '[]')
    model = os.getenv('CLAUDE_MODEL', 'claude-sonnet-4.5')
    working_dir = os.getenv('CLAUDE_WORKING_DIR', str(PROJECT_ROOT))

    # 解析上下文文件
    try:
        context_files = json.loads(context_files_json)
    except json.JSONDecodeError:
        context_files = []

    # 构建会话信息
    session_info = {
        'model': model,
        'working_dir': working_dir,
        'session_type': os.getenv('CLAUDE_SESSION_TYPE', 'new')
    }

    # 记录提示词
    session_id = append_prompt_log(
        user_message=user_message,
        context_files=context_files,
        session_info=session_info
    )

    # 输出会话ID到 stdout（供 Claude Code 读取）
    print(f"[Prompt Log] Session ID: {session_id}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
