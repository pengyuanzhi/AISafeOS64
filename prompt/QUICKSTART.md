# 提示词记录快速使用指南

## 🚀 快速开始

### 1. 自动记录（推荐）

配置好 hooks 后，所有提示词会自动记录到 `prompt/prompt.md`。

**当前状态**: ✅ 已配置 `user-prompt-submit` hook

**如何验证**:
```bash
# 查看最新记录
tail -n 50 prompt/prompt.md

# 查看会话统计
python .claude/hooks/analyze_prompts.py
```

### 2. 手动导入历史

```bash
# 导入单个历史文件
python .claude/hooks/import_history.py prompt/2026-01-08-64arm64.txt

# 导入所有历史文件
python .claude/hooks/import_history.py --all
```

### 3. 分析提示词记录

```bash
# 生成统计报告
python .claude/hooks/analyze_prompts.py

# 导出为 JSON
python .claude/hooks/analyze_prompts.py --export-json
```

## 📁 文件说明

| 文件 | 说明 |
|------|------|
| `prompt/prompt.md` | 主提示词日志（所有提示词的汇总） |
| `prompt/20260112-*.txt` | 单独的提示词备份文件 |
| `prompt/QUICKSTART.md` | 本文件（快速指南） |
| `.claude/hooks/log-prompt.py` | 提示词记录脚本 |
| `.claude/hooks/import_history.py` | 历史导入脚本 |
| `.claude/hooks/analyze_prompts.py` | 分析统计脚本 |

## 🔍 搜索提示词

### 按日期搜索

```bash
# 在 prompt.md 中搜索特定日期
grep "## 提示词记录 #20260112" prompt/prompt.md
```

### 按关键词搜索

```bash
# 搜索包含"信号量"的提示词
grep -A 20 "信号量" prompt/prompt.md
```

### 按模块搜索

```bash
# 搜索内存管理相关的提示词
grep -i "MMU\|内存\|页表" prompt/prompt.md
```

## 📝 提示词格式示例

每个提示词记录包含以下部分：

```markdown
---
## 提示词记录 #20260112-143022

**时间**: 2026-01-12 14:30:22
**会话类型**: new
**模型**: claude-sonnet-4.5
**工作目录**: `D:\AI\homework\ClaudeCode\AISafeOS64`

### 用户提示词

```
如何实现信号量？
```

### 上下文文件

- `src/kernel/sync/semaphore.c` (行: 1-357)
- `include/kernel/sync.h`

### 会话元数据

```json
{
  "session_id": "20260112-143022",
  "timestamp": "2026-01-12 14:30:22",
  "model": "claude-sonnet-4.5",
  "working_dir": "D:\\AI\\homework\\ClaudeCode\\AISafeOS64",
  "context_file_count": 2
}
```

---
```

## 🔧 常见问题

### Q: Hook 没有自动触发？

**A**: 检查以下几点：

1. 确认 `settings.local.json` 配置正确
2. Python 可执行文件在系统 PATH 中
3. 手动测试 hook 脚本：

```bash
python .claude/hooks/log-prompt.py
```

### Q: 如何查看统计信息？

**A**: 运行分析脚本：

```bash
python .claude/hooks/analyze_prompts.py
```

输出示例：
```
📊 正在分析提示词记录...
✅ 找到 42 个会话

📈 基本信息
  总提示词数: 42
  时间范围: 2026-01-08 00:00:00 ~ 2026-01-12 14:30:22

🔑 热门关键词
  信号量        ████████ (8)
  内存管理      ██████ (6)
  任务调度      █████ (5)
  ...
```

### Q: 如何备份提示词记录？

**A**: 提交到 Git 或导出为 JSON：

```bash
# Git 方式（推荐）
git add prompt/
git commit -m "docs(prompts): update prompt logs"

# JSON 导出
python .claude/hooks/analyze_prompts.py --export-json --output prompts_backup.json
```

## 📚 更多文档

详细文档请参考：
- [Hooks 完整指南](.claude/hooks/README.md)
- [Claude Code 官方文档](https://code.claude.com/docs)

## 🎯 下一步

1. ✅ 自动记录已配置（每次会话自动记录）
2. 📥 可选：导入历史提示词（运行 `import_history.py --all`）
3. 📊 可选：生成统计报告（运行 `analyze_prompts.py`）
4. 🔍 开始使用：搜索和浏览 `prompt/prompt.md`

---

**最后更新**: 2026-01-12
**维护者**: AISafe64 Team
