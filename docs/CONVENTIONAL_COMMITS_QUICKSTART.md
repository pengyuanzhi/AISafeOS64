# Conventional Commits 快速入门

## 概述

全自动 Git 提交工具 - 无需手动执行 `git add`，自动检测并暂存文件！

## 快速开始

### 1. 安装 Git Hooks（一次性）

**Linux / macOS / Git Bash:**
```bash
./scripts/install_hooks.sh
```

**Windows PowerShell:**
```powershell
.\scripts\install_hooks.ps1
```

### 2. 全自动提交（推荐）

```bash
# 只需一个命令，完成从文件选择到提交的全流程
./scripts/smart_commit.sh
```

**工具会自动：**
1. ✅ 检测所有修改的文件
2. ✅ 显示文件状态（修改/删除/新增）
3. ✅ 让你选择要暂存的文件
4. ✅ 生成符合规范的提交消息
5. ✅ 执行提交

### 3. 提交流程

```
运行脚本
    ↓
显示当前仓库状态
    ↓
选择暂存方式:
  • 1) 交互式选择文件 (推荐)
  • 2) 暂存所有更改
  • 3) 跳过暂存 (仅使用已暂存的文件)
    ↓
确认暂存内容
    ↓
生成提交消息
    ↓
执行提交
```

## 交互式文件选择

### 模式 1: 交互式选择（推荐）

```bash
$ ./scripts/smart_commit.sh

========================================
  全自动 Git 提交助手
========================================

当前仓库状态:

分支: master

修改的文件 (未暂存):
  M src/kernel/scheduler.c
  M src/mm/page_alloc.c

未跟踪的新文件:
  ?? docs/new_feature.md

选择暂存方式:
  1) 交互式选择文件 (推荐)
  2) 暂存所有更改
  3) 跳过暂存 (仅使用已暂存的文件)
  q) 取消

选择 (1-3/q): 1

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

选择文件> [按 Enter]

暂存: src/kernel/scheduler.c
暂存: docs/new_feature.md
✓ 已暂存 2 个文件
```

### 交互式命令说明

| 命令 | 说明 |
|------|------|
| `1 3 5` | 选择/取消选择文件（编号） |
| `a` | 全选所有文件 |
| `n` | 取消全选 |
| `d 1` | 查看文件 1 的详细差异 |
| `Enter` | 确认并暂存选中的文件 |
| `q` | 取消操作 |

### 模式 2: 暂存所有更改

```bash
选择暂存方式:
  1) 交互式选择文件 (推荐)
  2) 暂存所有更改
  3) 跳过暂存 (仅使用已暂存的文件)
  q) 取消

选择 (1-3/q): 2

暂存所有更改...
✓ 已暂存 5 个文件
```

### 模式 3: 跳过暂存

如果你已经手动执行了 `git add`，可以选择此模式：

```bash
选择暂存方式:
  1) 交互式选择文件 (推荐)
  2) 暂存所有更改
  3) 跳过暂存 (仅使用已暂存的文件)
  q) 取消

选择 (1-3/q): 3

跳过暂存，使用已有文件
```

## 提交类型速查表

| 类型 | 说明 | 示例 |
|------|------|------|
| `feat` | 新功能 | `feat(mm): add transparent huge page support` |
| `fix` | Bug 修复 | `fix(scheduler): resolve race condition in migration` |
| `docs` | 文档更新 | `docs(readme): update build instructions` |
| `style` | 代码格式 | `style(kernel): fix indentation` |
| `refactor` | 重构 | `refactor(ipc): simplify message queue` |
| `perf` | 性能优化 | `perf(scheduler): optimize priority lookup` |
| `test` | 测试相关 | `test(mm): add page allocator tests` |
| `chore` | 构建/工具链 | `chore(cmake): update toolchain` |
| `ci` | CI/CD 配置 | `ci(gitlab): add MISRA check` |
| `revert` | 回滚提交 | `revert: feat(api): remove deprecated API` |

## 常用命令

```bash
# 全自动提交（推荐）
./scripts/smart_commit.sh

# 仅生成提交消息（不立即提交）
python scripts/conventional_commit.py generate

# 验证提交消息
python scripts/conventional_commit.py validate <file>

# 跳过验证（不推荐）
git commit --no-verify -m "message"
```

## 文件状态说明

| 状态 | 颜色 | 说明 |
|------|------|------|
| `M` | 黄色 | 修改的文件 |
| `D` | 红色 | 删除的文件 |
| `??` | 绿色 | 未跟踪的新文件 |
| `✓` | 绿色 | 已选择暂存 |

## 验证规则

- [x] 提交类型必须是预定义的类型之一
- [x] 作用域必须是预定义的作用域之一
- [x] 主题至少 10 个字符，不超过 72 字符
- [x] 主题不应以句号结尾
- [x] 主题以小写字母开头
- [x] 正文每行不超过 72 字符

## 故障排除

### 问题：提交验证失败

**解决方法：**
1. 查看错误消息
2. 使用智能提交助手重新生成：`./scripts/smart_commit.sh`
3. 或手动修正消息

### 问题：Windows 下中文显示乱码

**解决方法：**
这是终端编码问题，不影响功能。可以使用：
```bash
# Git Bash
export LANG=en_US.UTF-8

# PowerShell
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
```

### 问题：工具未找到 Python

**解决方法：**
1. 确保已安装 Python 3.6+
2. 将 Python 添加到 PATH
3. 或修改脚本中的 `python3` 为 `python`

### 问题：只想提交部分文件

**解决方法：**
使用交互式选择模式：
```bash
./scripts/smart_commit.sh
# 选择 1) 交互式选择文件
# 只勾选你想提交的文件
```

## 完整示例

```bash
# 1. 修改了一些文件
vim src/kernel/scheduler.c
vim docs/README.md

# 2. 运行智能提交
./scripts/smart_commit.sh

# 3. 选择要暂存的文件
选择暂存方式:
  1) 交互式选择文件 (推荐)  # ← 选择这个

# 4. 选择文件
选择文件> 1     # 只选择第一个文件
选择文件> [按 Enter]

# 5. 确认提交
是否继续提交? (Y/n): [按 Enter]

# 6. 选择提交类型
选择提交类型:
  1. feat - 新功能  # ← 选择这个

# 7. 输入主题
输入主题描述: add EDF scheduling algorithm

# 8. （可选）添加详细描述
输入详细描述: 实现最早截止时间优先调度算法

# 9. 完成！
✓ 提交成功!
```

## 更多信息

- [完整使用指南](./CONVENTIONAL_COMMITS.md)
- [Conventional Commits 规范](https://www.conventionalcommits.org/)
- [项目编码规范 - Git 工作流](../.claude/rules/git-workflow.md)
