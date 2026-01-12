#!/usr/bin/env python3
"""
导入历史提示词文件到 prompt.md

用法:
    python import_history.py prompt/2026-01-08-64arm64.txt
    python import_history.py --all
"""

import os
import sys
import re
from pathlib import Path
from datetime import datetime

# 项目根目录
PROJECT_ROOT = Path(__file__).parent.parent.parent
PROMPT_DIR = PROJECT_ROOT / "prompt"
PROMPT_LOG = PROMPT_DIR / "prompt.md"


def parse_session_file(file_path):
    """
    解析历史会话文件，提取提示词

    Args:
        file_path: 历史文件路径

    Returns:
        list: 提示词列表
    """
    content = file_path.read_text(encoding='utf-8')

    prompts = []

    # 提取用户输入（以 "> " 开头的行）
    current_prompt = []
    in_prompt = False

    for line in content.split('\n'):
        # 检测用户提示词开始
        if line.startswith('> '):
            if current_prompt:
                prompts.append('\n'.join(current_prompt))
            current_prompt = [line[2:]]  # 去掉 "> " 前缀
            in_prompt = True
        elif in_prompt:
            if line.strip() and not line.startswith('●'):
                # 继续追加多行提示词
                current_prompt.append(line)
            else:
                # 提示词结束
                if current_prompt:
                    prompts.append('\n'.join(current_prompt))
                current_prompt = []
                in_prompt = False

    # 处理最后一个提示词
    if current_prompt:
        prompts.append('\n'.join(current_prompt))

    return prompts


def import_prompt(text, timestamp, source_file):
    """
    导入单个提示词到 prompt.md

    Args:
        text: 提示词内容
        timestamp: 时间戳
        source_file: 来源文件名
    """
    # 生成会话ID（基于文件名和时间戳）
    session_id = timestamp.strftime("%Y%m%d-%H%M%S")

    entry = f"""---
## 提示词记录 #{session_id}

**时间**: {timestamp.strftime("%Y-%m-%d %H:%M:%S")}
**会话类型**: import
**模型**: unknown
**工作目录**: `D:\\AI\\homework\\ClaudeCode\\AISafeOS64`
**来源文件**: `{source_file}`

### 用户提示词

```
{text}
```

### 会话元数据

```json
{{
  "session_id": "{session_id}",
  "timestamp": "{timestamp.strftime("%Y-%m-%d %H:%M:%S")}",
  "model": "unknown",
  "source_file": "{source_file}",
  "imported_at": "{datetime.now().strftime("%Y-%m-%d %H:%M:%S")}"
}}
```

---

"""

    # 追加到文件
    with open(PROMPT_LOG, 'a', encoding='utf-8') as f:
        f.write(entry)

    print(f"✅ 导入提示词: {text[:50]}...")


def import_file(file_path):
    """
    导入单个历史文件

    Args:
        file_path: 文件路径
    """
    print(f"\n📄 处理文件: {file_path.name}")

    if not file_path.exists():
        print(f"❌ 文件不存在: {file_path}")
        return 0

    # 从文件名提取日期
    date_match = re.search(r'(\d{4}-\d{2}-\d{2})', file_path.name)
    if date_match:
        date_str = date_match.group(1)
        try:
            timestamp = datetime.strptime(date_str, "%Y-%m-%d")
        except ValueError:
            timestamp = datetime.now()
    else:
        timestamp = datetime.now()

    # 解析文件
    prompts = parse_session_file(file_path)

    if not prompts:
        print(f"⚠️  未找到提示词")
        return 0

    # 导入每个提示词
    count = 0
    for i, prompt in enumerate(prompts, 1):
        if prompt.strip():
            import_prompt(prompt, timestamp, file_path.name)
            count += 1

    print(f"✅ 完成：导入 {count} 个提示词")
    return count


def import_all():
    """导入所有历史文件"""
    print("🔍 扫描历史文件...")

    # 查找所有 .txt 文件（排除 prompt.md）
    txt_files = [
        f for f in PROMPT_DIR.glob("*.txt")
        if f.name != "prompt.md" and f.name.startswith("2026-")
    ]

    if not txt_files:
        print("⚠️  未找到历史文件")
        return 0

    print(f"📁 找到 {len(txt_files)} 个历史文件\n")

    total_count = 0
    for file_path in sorted(txt_files):
        count = import_file(file_path)
        total_count += count

    print(f"\n✅ 总计导入 {total_count} 个提示词")
    return total_count


def main():
    """主函数"""
    if len(sys.argv) < 2:
        print("用法:")
        print("  python import_history.py <文件路径>")
        print("  python import_history.py --all")
        print("\n示例:")
        print("  python import_history.py prompt/2026-01-08-64arm64.txt")
        print("  python import_history.py --all")
        return 1

    arg = sys.argv[1]

    if arg == "--all":
        return import_all()
    else:
        file_path = Path(arg)
        if not file_path.is_absolute():
            file_path = PROJECT_ROOT / file_path
        return import_file(file_path)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n\n⚠️  导入被中断")
        sys.exit(1)
    except Exception as e:
        print(f"\n❌ 错误: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
