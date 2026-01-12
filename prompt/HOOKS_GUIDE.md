# Claude Code Hooks 配置指南 (修正版)

## ✅ 已修正的问题

### 1. 事件命名格式

**错误** ❌:
```json
{
  "hooks": {
    "user-prompt-submit": "python .claude/hooks/log-prompt.py"
  }
}
```

**正确** ✅:
```json
{
  "hooks": {
    "SessionStart": "python .claude/hooks/session_start.py",
    "UserPromptSubmit": "python .claude/hooks/user_prompt_submit.py",
    "Stop": "python .claude/hooks/stop.py",
    "SessionEnd": "python .claude/hooks/session_end.py"
  }
}
```

**说明**: Claude Code Hooks 使用**驼峰命名**（CamelCase），而非短横线命名。

### 2. 数据传递方式

**错误** ❌:
```python
# 假设从环境变量读取
user_message = os.getenv('CLAUDE_USER_PROMPT', '')
context_files = os.getenv('CLAUDE_CONTEXT_FILES', '[]')
```

**正确** ✅:
```python
# 从标准输入（stdin）读取 JSON
import sys
import json

input_data = json.loads(sys.stdin.read())
prompt_content = input_data.get('prompt', '')
context_files = input_data.get('contextFiles', [])
```

**说明**: Hook 脚本通过 **stdin** 接收 JSON 数据，而非环境变量。

### 3. Hook 脚本返回值

**错误** ❌:
```python
print(f"[Prompt Log] Session ID: {session_id}")
```

**正确** ✅:
```python
# 不需要返回值时，返回 None 或不输出
return None

# 或需要返回响应时，输出 JSON
if result:
    print(json.dumps(result))
```

**说明**: Hook 脚本可以通过返回值修改 Claude Code 的行为，但大多数情况下只需要记录数据。

## 📋 已配置的 Hooks

| Hook 事件 | 脚本文件 | 功能 | 触发时机 |
|-----------|----------|------|----------|
| `SessionStart` | `session_start.py` | 记录会话开始 | Claude Code 启动或恢复会话 |
| `UserPromptSubmit` | `user_prompt_submit.py` | 记录用户提示词 | 用户提交提示词时 |
| `Stop` | `stop.py` | 记录响应完成 | Claude 完成响应时 |
| `SessionEnd` | `session_end.py` | 记录会话结束 | 会话结束时 |

## 📁 生成的日志文件

```
prompt/
├── prompt.md          # 提示词和响应记录
├── sessions.md        # 会话开始/结束记录
├── 20260112-143022.txt # 单独的提示词备份
└── ...
```

## 🔧 Hook 数据结构

### SessionStart

```json
{
  "sessionId": "string",
  "model": "string",
  "isResume": boolean,
  "workingDirectory": "string"
}
```

### UserPromptSubmit

```json
{
  "sessionId": "string",
  "prompt": "string",
  "model": "string",
  "workingDirectory": "string",
  "contextFiles": [
    {
      "path": "string",
      "lines": "string (optional)"
    }
  ]
}
```

### Stop

```json
{
  "sessionId": "string",
  "response": "string",
  "toolCalls": [
    {
      "name": "string",
      "input": "object"
    }
  ]
}
```

### SessionEnd

```json
{
  "sessionId": "string",
  "durationSeconds": number,
  "promptCount": number,
  "toolUseCount": number
}
```

## 🧪 测试 Hooks

运行测试脚本验证配置：

```bash
python .claude/hooks/test_hooks.py
```

预期输出：
```
============================================================
Claude Code Hooks 测试
============================================================

[TEST] 测试 session_start.py...
[OK] session_start.py 执行成功

[TEST] 测试 user_prompt_submit.py...
[OK] user_prompt_submit.py 执行成功

[TEST] 测试 stop.py...
[OK] stop.py 执行成功

[TEST] 测试 session_end.py...
[OK] session_end.py 执行成功

============================================================
测试总结: 4 通过, 0 失败
============================================================
[OK] 所有测试通过！
```

## 🚀 使用示例

### 1. 自动记录流程

当你在 Claude Code 中输入提示词时：

```
> 帮我分析信号量实现中的 TOCTOU 问题
```

自动触发以下 Hooks：

1. **UserPromptSubmit** → 记录提示词到 `prompt/prompt.md`
2. **Stop** → 记录响应完成和使用的工具
3. **SessionEnd** → 记录会话统计（当会话结束时）

### 2. 查看记录

```bash
# 查看最新的提示词记录
tail -n 50 prompt/prompt.md

# 查看会话日志
cat prompt/sessions.md
```

### 3. 搜索历史

```bash
# 按关键词搜索
grep -A 20 "信号量" prompt/prompt.md

# 按会话ID搜索
grep "test-session-123" prompt/prompt.md
```

## ⚙️ 高级配置

### 添加新的 Hook

创建新的 Hook 脚本：

```python
#!/usr/bin/env python3
# .claude/hooks/post_tool_use.py

import sys
import json

def handle_hook(event_data):
    tool_name = event_data.get('toolName', 'unknown')
    tool_input = event_data.get('toolInput', {})

    # 记录工具调用
    with open('prompt/tools.md', 'a') as f:
        f.write(f"- {tool_name}: {tool_input}\n")

    return None

if __name__ == '__main__':
    input_data = json.loads(sys.stdin.read())
    result = handle_hook(input_data)
    if result:
        print(json.dumps(result))
```

在 `settings.local.json` 中注册：

```json
{
  "hooks": {
    "PostToolUse": "python .claude/hooks/post_tool_use.py"
  }
}
```

### Hook 响应控制

某些 Hook 可以返回响应来控制 Claude 的行为：

#### PermissionRequest 示例

```python
def handle_hook(event_data):
    permission_type = event_data.get('permissionType', '')

    # 自动允许某些权限
    if permission_type in ['Bash(python:*)', 'Read(*)']:
        return {"allow": True}

    # 拒绝危险操作
    if permission_type.startswith('Bash(rm'):
        return {"allow": False}

    # 其他情况由用户决定
    return None
```

## 📊 Hook 工作流程图

```mermaid
graph TD
    A[用户启动 Claude Code] -->|SessionStart| B[记录会话开始]
    B --> C[等待用户输入]
    C -->|UserPromptSubmit| D[记录提示词]
    D --> E[Claude 处理]
    E -->|可能触发| F[PreToolUse/PostToolUse]
    F --> E
    E -->|Stop| G[记录响应完成]
    G --> C
    C -->|会话结束| H|SessionEnd|
    H --> I[记录会话统计]
```

## 🔍 故障排查

### Hook 未触发

1. **检查事件名称大小写**：
   ```bash
   # 查看配置
   cat .claude/settings.local.json | grep hooks
   ```

   应该是驼峰命名：
   ```json
   "UserPromptSubmit": "python ..."
   ```

2. **检查脚本路径**：
   ```bash
   # 确认脚本存在
   ls -la .claude/hooks/user_prompt_submit.py

   # 确认可执行
   chmod +x .claude/hooks/*.py
   ```

3. **手动测试脚本**：
   ```bash
   python .claude/hooks/test_hooks.py
   ```

### Hook 执行失败

1. **查看错误日志**：
   ```bash
   # 手动运行并查看输出
   echo '{}' | python .claude/hooks/user_prompt_submit.py
   ```

2. **检查 Python 语法**：
   ```bash
   python -m py_compile .claude/hooks/user_prompt_submit.py
   ```

3. **检查 JSON 解析**：
   ```python
   import json
   json.loads(sys.stdin.read())  # 确保输入是有效 JSON
   ```

### 权限问题

确保 `settings.local.json` 包含必要的权限：

```json
{
  "permissions": {
    "allow": [
      "Bash(python:*)",
      "Bash(.claude/hooks/*)"
    ]
  }
}
```

## 📚 参考资源

- [Claude Code 官方文档](https://code.claude.com/docs)
- [Hooks API 参考](https://code.claude.com/docs/cli-reference/hooks)
- [完整事件列表](https://code.claude.com/docs/cli-reference/hooks#events)

## 🎯 总结

修正后的配置：

| 项目 | 错误版本 | 正确版本 |
|------|----------|----------|
| 事件名称 | `user-prompt-submit` | `UserPromptSubmit` |
| 数据传递 | 环境变量 | stdin JSON |
| 返回值 | 打印到 stdout | 返回 None 或 JSON |
| 脚本位置 | 任意路径 | `.claude/hooks/` |

---

**最后更新**: 2026-01-12
**版本**: v2.0 (修正版)
