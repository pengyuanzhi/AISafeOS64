# 智能提交助手 v2.0 - 使用指南

## 概述

`smart_commit.sh` 是一个全自动的 Git 提交工具，支持命令行参数控制，可以一键完成从文件暂存到推送到远程的全流程。

## 特性

- ✅ **默认模式**：自动暂存所有更改
- ✅ **交互模式**：手动选择要暂存的文件
- ✅ **自动推送**：提交后自动推送到远程仓库
- ✅ **Conventional Commits**：自动生成符合规范的提交消息
- ✅ **彩色输出**：清晰的文件状态显示

## 快速开始

### 1. 基本用法

```bash
# 默认模式：暂存所有更改并提交
./scripts/smart_commit.sh

# 交互模式：手动选择文件
./scripts/smart_commit.sh -i

# 提交后自动推送
./scripts/smart_commit.sh -p

# 交互模式 + 推送
./scripts/smart_commit.sh -i -p

# 查看帮助
./scripts/smart_commit.sh --help
```

### 2. 使用场景

#### 场景 1：快速提交（最常用）

```bash
# 修改文件后，只需一个命令
vim src/kernel/scheduler.c

# 一键提交所有更改
./scripts/smart_commit.sh

# 如果需要推送
./scripts/smart_commit.sh -p
```

#### 场景 2：选择性提交

```bash
# 修改了多个文件，但只想提交部分
vim src/kernel/scheduler.c
vim src/mm/page_alloc.c
vim docs/README.md

# 交互模式选择文件
./scripts/smart_commit.sh -i

# 选择要提交的文件
选择文件> 1 3
选择文件> [Enter]
```

#### 场景 3：完整工作流

```bash
# 修改文件
vim src/kernel/scheduler.c

# 一键完成：暂存 + 提交 + 推送
./scripts/smart_commit.sh -p
```

## 命令行选项

| 选项 | 长选项 | 说明 |
|------|--------|------|
| `-i` | `--interactive` | 交互模式：手动选择要暂存的文件 |
| `-p` | `--push` | 提交后自动推送到远程仓库 |
| `-h` | `--help` | 显示帮助信息 |

## 工作流程

### 默认模式（无参数）

```
运行脚本
    ↓
显示当前仓库状态
    ↓
自动暂存所有更改
    ↓
确认提交
    ↓
生成提交消息（交互式）
    ↓
执行提交
    ↓
（如果使用 -p）推送到远程
```

### 交互模式（-i 参数）

```
运行脚本
    ↓
显示当前仓库状态
    ↓
显示文件列表
    ↓
选择文件（交互式）
    ↓
暂存选中的文件
    ↓
确认提交
    ↓
生成提交消息（交互式）
    ↓
执行提交
    ↓
（如果使用 -p）推送到远程
```

## 交互式文件选择

### 模式 1：交互式选择

```bash
$ ./scripts/smart_commit.sh -i

========================================
  智能 Git 提交助手 v2.0
========================================

当前仓库状态:

分支: master

修改的文件 (未暂存):
  M src/kernel/scheduler.c
  M src/mm/page_alloc.c

未跟踪的新文件:
  ?? docs/new_feature.md

========================================
  选择要暂存的文件
========================================

  [ 1] M src/kernel/scheduler.c
  [ 2] M src/mm/page_alloc.c
  [ 3] ?? docs/new_feature.md

操作说明:
  输入文件编号 (如: 1 3 5) - 选择/取消选择文件
  a - 全选
  n - 取消全选
  d <编号> - 查看文件差异 (如: d 1)
  Enter - 确认并继续
  q - 取消

选择文件> 1 3

  [ 1] ✓ src/kernel/scheduler.c
  [ 2]   src/mm/page_alloc.c
  [ 3] ✓ docs/new_feature.md

选择文件> [Enter]

暂存: src/kernel/scheduler.c
暂存: docs/new_feature.md
✓ 已暂存 2 个文件
```

### 交互式命令说明

| 命令 | 说明 |
|------|------|
| `1 3 5` | 选择/取消选择文件（支持多选） |
| `a` | 全选所有文件 |
| `n` | 取消全选 |
| `d 1` | 查看文件 1 的详细差异 |
| `Enter` | 确认并暂存 |
| `q` | 取消操作 |

## 推送到远程

### 自动推送

使用 `-p` 或 `--push` 选项可以在提交成功后自动推送到远程：

```bash
# 默认模式 + 推送
./scripts/smart_commit.sh -p

# 交互模式 + 推送
./scripts/smart_commit.sh -i -p
```

### 推送过程

```
提交成功!

========================================
  推送到远程仓库
========================================

推送分支: master
远程仓库: origin

Username: 'your_token'
Password: '********'
✓ 推送成功!
```

### 配置推送

脚本会自动检测当前分支和远程仓库：

```bash
# 默认推送到 origin/master
git push origin master

# 如果分支设置了不同的远程
git push <remote> <branch>
```

## 完整示例

### 示例 1：快速提交并推送

```bash
# 1. 修改文件
vim src/kernel/scheduler.c

# 2. 一键完成：暂存 + 提交 + 推送
./scripts/smart_commit.sh -p

# 输出：
# ========================================
#   智能 Git 提交助手 v2.0
# ========================================
#
# 当前仓库状态:
#
# 分支: master
#
# 修改的文件 (未暂存):
#   M src/kernel/scheduler.c
#
# 暂存所有更改...
# ✓ 已暂存 1 个文件
#
# ========================================
#   确认提交
# ========================================
#
# 暂存的更改:
#  src/kernel/scheduler.c | 10 +++++-----
#
# 是否继续提交? (Y/n): [Enter]
#
# ========================================
#   生成提交消息
# ========================================
#
# [交互式生成提交消息...]
#
# ✓ 提交成功!
#
# ========================================
#   推送到远程仓库
# ========================================
#
# ✓ 推送成功!
```

### 示例 2：选择性提交

```bash
# 1. 修改多个文件
vim src/kernel/scheduler.c
vim src/mm/page_alloc.c
vim docs/README.md

# 2. 交互模式选择文件
./scripts/smart_commit.sh -i

# 3. 只提交第一个文件
选择文件> 1
选择文件> [Enter]

# 4. 生成提交消息并提交
# [完成]
```

### 示例 3：查看文件差异

```bash
$ ./scripts/smart_commit.sh -i

选择文件> d 1

文件: src/kernel/scheduler.c
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
diff --git a/src/kernel/scheduler.c b/src/kernel/scheduler.c
index 1234567..abcdefg 100644
--- a/src/kernel/scheduler.c
+++ b/src/kernel/scheduler.c
@@ -42,7 +42,7 @@ void scheduler_task_create(void) {

     /* 检查任务数量 */
     if (task_count >= MAX_TASKS) {
-        return ERROR_FAILED;
+        return ERROR_OUT_OF_MEMORY;
     }

     /* 创建任务 */
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

## 文件状态说明

| 状态 | 颜色 | 说明 |
|------|------|------|
| `M` | 黄色 | 修改的文件 |
| `D` | 红色 | 删除的文件 |
| `??` | 绿色 | 未跟踪的新文件 |
| `✓` | 绿色 | 已选择暂存 |

## 提交消息生成

脚本会调用 `conventional_commit.py` 交互式生成提交消息：

```bash
# 1. 选择提交类型
选择提交类型:
  1. feat - 新功能
  2. fix - Bug修复
  ...

# 2. 选择作用域
选择作用域:
  1. kernel
  2. scheduler
  ...

# 3. 输入主题
输入主题: add EDF scheduling algorithm

# 4. 输入详细描述（可选）
输入详细描述: 实现最早截止时间优先调度算法

# 5. 确认
# [生成提交消息并提交]
```

## 常见问题

### Q: 如何跳过确认？

在确认提示时直接按 `Enter` 或 `Y` 即可继续。

### Q: 如何取消操作？

在任何确认提示时输入 `n` 或 `N`，或在文件选择时输入 `q`。

### Q: 推送失败怎么办？

```bash
# 检查网络连接
ping github.com

# 检查远程仓库配置
git remote -v

# 手动推送
git push origin master
```

### Q: 如何只提交不推送？

不使用 `-p` 参数即可：

```bash
./scripts/smart_commit.sh
```

## 高级用法

### 别名配置

在 `~/.bashrc` 或 `~/.gitconfig` 中添加别名：

```bash
# ~/.bashrc
alias gc='./scripts/smart_commit.sh'
alias gci='./scripts/smart_commit.sh -i'
alias gcp='./scripts/smart_commit.sh -p'
```

使用：

```bash
gc     # 默认模式
gci    # 交互模式
gcp    # 提交并推送
```

### Git 别名

```bash
# ~/.gitconfig
[alias]
  smart = "!f() { ./scripts/smart_commit.sh \"$@\"; }; f"
  smart-i = "!f() { ./scripts/smart_commit.sh -i \"$@\"; }; f"
  smart-p = "!f() { ./scripts/smart_commit.sh -p \"$@\"; }; f"
```

使用：

```bash
git smart      # 默认模式
git smart-i    # 交互模式
git smart-p    # 提交并推送
```

## 对比旧版本

| 功能 | v1.0 | v2.0 |
|------|------|------|
| 默认行为 | 询问暂存方式 | 自动暂存所有 |
| 交互模式 | 必须选择 | 可选（-i 参数） |
| 推送功能 | 无 | 有（-p 参数） |
| 命令行参数 | 无 | 支持 |
| 使用便捷性 | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |

## 相关文档

- [Conventional Commits 规范](https://www.conventionalcommits.org/)
- [完整使用指南](./CONVENTIONAL_COMMITS.md)
- [快速入门](./CONVENTIONAL_COMMITS_QUICKSTART.md)
- [更新日志](../CHANGELOG_CONVENTIONAL_COMMITS.md)
