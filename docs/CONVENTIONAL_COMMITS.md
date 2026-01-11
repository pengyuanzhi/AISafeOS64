# Conventional Commits 工具使用指南

## 概述

本工具集为 AISafe64 项目提供了完整的 **Conventional Commits** 规范支持，确保所有 Git 提交消息遵循统一的格式标准，提高代码库的可维护性和可追溯性。

## 什么是 Conventional Commits?

Conventional Commits 是一种用于编写提交消息的规范：
- 基于结构化的格式
- 便于自动化工具处理
- 提高提交历史的可读性
- 支持自动生成 CHANGELOG

## 提交消息格式

```
<type>(<scope>): <subject>

<body>

<footer>
```

### 格式说明

| 部分 | 必需 | 说明 |
|------|------|------|
| **type** | 是 | 提交类型（feat、fix、docs 等） |
| **scope** | 否 | 影响的模块（kernel、scheduler、mm 等） |
| **subject** | 是 | 简短描述（使用现在时态，小写开头） |
| **body** | 否 | 详细描述（说明"是什么"和"为什么"） |
| **footer** | 否 | 关联 Issue 或破坏性变更说明 |

## 安装

### 1. Git Hooks 安装

**Linux / macOS (Git Bash):**
```bash
./scripts/install_hooks.sh
```

**Windows (PowerShell):**
```powershell
.\scripts\install_hooks.ps1
```

安装后，每次提交都会自动验证消息格式。

### 2. 手动验证（可选）

如果不想安装 Git Hooks，可以手动验证提交消息：

```bash
python3 scripts/conventional_commit.py validate .git/COMMIT_EDITMSG
```

## 使用方法

### 方法 1: 交互式生成（推荐）

使用智能提交脚本，它会引导你完成整个提交流程：

```bash
# Linux / macOS
./scripts/smart_commit.sh

# 或在 Git Bash 中
source scripts/smart_commit.sh
smart_commit
```

**交互流程：**
1. 查看暂存的更改
2. 选择提交类型（feat、fix 等）
3. 选择影响范围（scope）
4. 输入主题描述
5. （可选）添加详细描述
6. （可选）关联 Issue
7. 自动执行提交

### 方法 2: 仅生成提交消息

如果只想生成提交消息但不立即提交：

```bash
python3 scripts/conventional_commit.py generate
```

生成的消息会保存到 `.git/COMMIT_EDITMSG`，然后可以手动提交：

```bash
git commit -F .git/COMMIT_EDITMSG
```

### 方法 3: 手动编写提交消息

如果你熟悉规范，也可以直接编写提交消息：

```bash
git commit
```

然后在编辑器中按照格式编写消息。Git Hook 会自动验证格式。

## 提交类型

| 类型 | 说明 | 示例 |
|------|------|------|
| **feat** | 新功能 | `feat(scheduler): add EDF scheduling algorithm` |
| **fix** | Bug 修复 | `fix(mm): resolve memory leak in page allocator` |
| **docs** | 文档更新 | `docs(readme): update build instructions` |
| **style** | 代码格式（不影响功能） | `style(kernel): fix indentation in scheduler.c` |
| **refactor** | 重构 | `refactor(ipc): simplify message queue implementation` |
| **perf** | 性能优化 | `perf(scheduler): optimize priority lookup with CLZ` |
| **test** | 测试相关 | `test(mm): add unit tests for page allocator` |
| **chore** | 构建/工具链相关 | `chore(cmake): update toolchain requirements` |
| **ci** | CI/CD 配置 | `ci(gitlab): add MISRA check pipeline` |
| **revert** | 回滚之前的提交 | `revert: fix(mm): resolve page table corruption` |

## 提交作用域

| 作用域 | 说明 |
|--------|------|
| **kernel** | 内核核心 |
| **scheduler** | 调度器 |
| **mm** | 内存管理 |
| **ipc** | 进程间通信 |
| **fs** | 文件系统 |
| **driver** | 设备驱动 |
| **arch** | 架构相关代码 |
| **crypto** | 加密/签名 |
| **build** | 构建系统 |
| **config** | 配置系统 |

## 提交示例

### 示例 1: 新功能

```
feat(mm): add transparent huge page support

- Implement 2MB page allocation
- Add automatic huge page promotion
- Update page fault handler to support mixed page sizes
- Add sysfs interface for statistics

This reduces TLB pressure and improves performance for
large memory allocations by up to 30%.

Closes #123
```

### 示例 2: Bug 修复

```
fix(scheduler): resolve race condition in task migration

The task migration code had a race condition where a task
could be migrated while being scheduled on another CPU,
leading to a use-after-free.

Fix: Add RCU read-side lock around migration check
and update scheduler to handle migrating tasks correctly.

Reported-by: John Doe <john@example.com>
Tested-by: Jane Smith <jane@example.com>
Fixes #456
```

### 示例 3: 破坏性变更

```
feat(api): remove deprecated task_create interface

BREAKING CHANGE: The old task_create() interface has been removed.
Migrate to task_create_ex() which supports additional parameters.

The new interface provides:
- Enhanced priority control
- Stack size validation
- Error reporting improvements

Migration guide:
  task_create() -> task_create_ex()

See docs/migration.md for details.
```

### 示例 4: 文档更新

```
docs(CONVENTIONAL_COMMITS): add usage guide

- Document installation steps
- Add interactive workflow examples
- Include type and scope definitions
- Provide troubleshooting guide

This helps contributors follow the commit message standards.
```

## 验证规则

工具会自动验证以下规则：

- [ ] 提交类型必须是预定义的类型之一
- [ ] 作用域（如果提供）必须是预定义的作用域之一
- [ ] 主题必须至少 10 个字符
- [ ] 主题不应以句号结尾
- [ ] 主题应以小写字母开头
- [ ] 第一行不超过 72 个字符
- [ ] 正文每行不超过 72 个字符
- [ ] 使用 `!` 标记破坏性变更时必须提供说明

## 常见问题

### Q: 如何跳过验证？

**不推荐**，但如果确实需要：

```bash
git commit --no-verify -m "your message"
```

### Q: 提交验证失败怎么办？

1. 查看错误消息，了解哪条规则未通过
2. 使用交互式生成器重新生成消息：
   ```bash
   python3 scripts/conventional_commit.py generate
   ```
3. 或者手动修正消息后重新提交

### Q: 如何编辑之前的提交消息？

```bash
# 编辑最后一次提交消息
git commit --amend

# Git Hook 会验证修改后的消息
```

### Q: 工具支持哪些 Python 版本？

Python 3.6 及以上版本。

### Q: Windows 下如何使用？

1. 使用 Git Bash 运行 `.sh` 脚本
2. 或使用 PowerShell 运行 `.ps1` 脚本
3. 确保 Python 3 已安装并在 PATH 中

## 工具脚本说明

| 脚本 | 说明 |
|------|------|
| `scripts/conventional_commit.py` | 核心验证和生成工具 |
| `scripts/install_hooks.sh` | Git Hooks 安装脚本（Linux/macOS） |
| `scripts/install_hooks.ps1` | Git Hooks 安装脚本（Windows） |
| `scripts/smart_commit.sh` | 智能提交助手（推荐使用） |
| `.commitlintrc.yml` | Commitlint 配置文件（供参考） |

## 参考资源

- [Conventional Commits 规范](https://www.conventionalcommits.org/)
- [Commitlint 工具](https://commitlint.js.org/)
- [项目编码规范 - Git 工作流](../.claude/rules/git-workflow.md)

## 贡献

如果发现问题或有改进建议，请提交 Issue 或 Pull Request。
