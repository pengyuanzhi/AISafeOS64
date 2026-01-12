#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Claude Code Hook: UserPromptSubmit

触发时机: 当用户提交提示时运行，在 Claude 处理之前
功能: 记录用户提示词到 prompt/prompt.md
"""

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


def handle_hook(event_data):
    """
    处理 UserPromptSubmit 事件

    Args:
        event_data: Hook 事件数据（从 stdin 读取的 JSON）

    Returns:
        dict: Hook 响应（可选）
    """
    # 提取数据
    prompt_content = event_data.get('prompt', '')
    context_files = event_data.get('contextFiles', [])
    model = event_data.get('model', 'unknown')
    working_dir = event_data.get('workingDirectory', str(PROJECT_ROOT))
    session_id = event_data.get('sessionId', 'unknown')

    # 生成时间戳
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    session_short_id = datetime.now().strftime("%Y%m%d-%H%M%S")

    # 格式化文件列表
    files_list = "无"
    if context_files:
        files_formatted = []
        for file_info in context_files:
            path = file_info.get('path', 'unknown')
            lines = file_info.get('lines')
            if lines:
                files_formatted.append(f"- `{path}` (行: {lines})")
            else:
                files_formatted.append(f"- `{path}`")
        files_list = "\n".join(files_formatted)

    # 构建 Markdown 条目
    entry = f"""---
## 提示词记录 #{session_short_id}

**时间**: {timestamp}
**会话ID**: {session_id}
**模型**: {model}
**工作目录**: `{working_dir}`

### 用户提示词

```
{prompt_content}
```

### 上下文文件

{files_list}

### 会话元数据

```json
{{
  "session_id": "{session_id}",
  "timestamp": "{timestamp}",
  "model": "{model}",
  "working_dir": "{working_dir}",
  "context_file_count": {len(context_files)}
}}
```

---

"""

    # 追加到文件
    with open(PROMPT_LOG, 'a', encoding='utf-8') as f:
        f.write(entry)

    # 同时保存单独的备份文件
    individual_log = PROMPT_DIR / f"{session_short_id}.txt"
    with open(individual_log, 'w', encoding='utf-8') as f:
        f.write(f"# Claude Code 提示词记录\n\n")
        f.write(f"**时间**: {timestamp}\n")
        f.write(f"**会话ID**: {session_id}\n")
        f.write(f"**模型**: {model}\n\n")
        f.write(f"## 提示词\n\n{prompt_content}\n\n")
        if context_files:
            f.write(f"## 上下文文件\n\n{files_list}\n\n")

    # 不需要修改行为，返回 None 或空字典
    return None


def main():
    """主函数：从 stdin 读取 JSON 数据"""
    try:
        # 从标准输入读取 JSON 数据
        input_data = json.loads(sys.stdin.read())

        # 处理事件
        result = handle_hook(input_data)

        # 如果有返回值，输出到 stdout
        if result:
            print(json.dumps(result))

        return 0

    except json.JSONDecodeError as e:
        # 输入不是有效的 JSON
        print(f"[ERROR] Invalid JSON input: {e}", file=sys.stderr)
        return 1
    except Exception as e:
        # 其他错误
        print(f"[ERROR] Hook execution failed: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        return 1


if __name__ == '__main__':
    sys.exit(main())
