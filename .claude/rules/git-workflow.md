## 15.3 Git 提交规范（Conventional Commits）

### 15.3.1 提交消息格式

AISafe64 项目严格遵循 **Conventional Commits** 规范，以确保提交历史的清晰和可追溯性。

#### 提交消息结构

```
<type>(<scope>): <subject>

<body>

<footer>
```

#### 提交类型（type）

| 类型 | 描述 | 示例 |
|------|------|------|
| `feat` | 新功能 | feat(scheduler): add EDF scheduling algorithm |
| `fix` | Bug 修复 | fix(mm): resolve page table corruption issue |
| `docs` | 文档更新 | docs(readme): update build instructions |
| `style` | 代码格式（不影响功能） | style(kernel): fix indentation in scheduler.c |
| `refactor` | 重构（既不是新功能也不是修复） | refactor(ipc): simplify message queue implementation |
| `perf` | 性能优化 | perf(scheduler): optimize priority lookup with CLZ |
| `test` | 测试相关 | test(mm): add unit tests for page allocator |
| `chore` | 构建/工具链相关 | chore(cmake): update toolchain requirements |
| `ci` | CI/CD 配置 | ci(gitlab): add MISRA check pipeline |
| `revert` | 回滚之前的提交 | revert: fix(mm): resolve page table corruption |

#### 提交作用域（scope）

作用域用于指定提交影响的模块：

| 模块 | 说明 |
|------|------|
| `kernel` | 内核核心 |
| `scheduler` | 调度器 |
| `mm` | 内存管理 |
| `ipc` | 进程间通信 |
| `fs` | 文件系统 |
| `driver` | 设备驱动 |
| `arch` | 架构相关代码 |
| `crypto` | 加密/签名 |
| `build` | 构建系统 |
| `config` | 配置系统 |

#### 主题（subject）

- 使用动词原形开头（如 add、fix、update）
- 首字母小写
- 不以句号结尾
- 限制在 50 个字符以内

**示例：**
```
✅ good: feat(scheduler): add EDF scheduling support
❌ bad: Added EDF scheduling support.
❌ bad: feat(scheduler): Added EDF scheduling support.
```

#### 正文（body）

- 详细说明本次提交的**内容**和**原因**
- 每行限制在 72 个字符以内
- **必须**说明"是什么"和"为什么"

**示例：**
```
feat(scheduler): add EDF scheduling support

- Implement earliest deadline first algorithm
- Add red-black tree for deadline tracking
- Integrate with existing scheduler class framework
- Update configuration to select between FIFO/EDF/CFS

This allows dynamic priority scheduling for real-time tasks
with periodic deadlines, improving schedulability compared
to static priority FIFO.

Performance: O(log n) enqueue/dequeue operations
```

#### 页脚（footer）

- 关联 Issue：`Closes #123`, `Fixes #456`
- 破坏性变更：`BREAKING CHANGE: <description>`
- 引用相关提交：`Co-Authored-By: <name> <email>`

**示例：**
```
feat(api): remove deprecated task_create interface

BREAKING CHANGE: The old task_create() interface has been removed.
Migrate to task_create_ex() which supports additional parameters.

Closes #789

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>
```

### 15.3.2 提交消息示例

#### 示例 1：新功能
```
feat(mm): add transparent huge page support

- Implement 2MB page allocation
- Add automatic huge page promotion
- Update page fault handler to support mixed page sizes
- Add sysfs interface for statistics

This reduces TLB pressure and improves performance for
large memory allocations by up to 30%.

Performance: 2MB page allocation takes <1ms
```

#### 示例 2：Bug 修复
```
fix(scheduler): resolve race condition in task migration

The task migration code had a race condition where a task
could be migrated while being scheduled on another CPU,
leading to a use-after-free.

Fix: Add RCU read-side lock around migration check
and update scheduler to handle migrating tasks correctly.

Reported-by: John Doe <john@example.com>
Tested-by: Jane Smith <jane@example.com>
Fixes #1234
```

#### 示例 3：文档更新
```
docs(CLARUDE.md): add Git commit convention rules

- Document Conventional Commits specification
- Add commit type definitions and examples
- Include scope guidelines and best practices

This ensures consistent commit messages across the project.
```

### 15.3.3 提交最佳实践

#### DO（推荐做法）

```bash
# 1. 每个提交做一件事
git commit -m "feat(scheduler): add EDF algorithm"
git commit -m "test(scheduler): add EDF unit tests"

# 2. 使用完整的句子解释
git commit -m "fix(mm): resolve memory leak in page allocator

The page allocator was not freeing pages on error path,
causing a memory leak of 4KB per failed allocation.

Fix: Add proper cleanup in error handling path."

# 3. 引用相关 Issue
git commit -m "feat(driver): add GPIO driver

Implements basic GPIO operations for Raspberry Pi 4.

Closes #456"
```

#### DON'T（不推荐做法）

```bash
# ❌ 1. 不要混合多个不相关的修改
git commit -m "update various things"

# ❌ 2. 不要使用模糊的描述
git commit -m "fix stuff"
git commit -m "update code"

# ❌ 3. 不要在提交消息中包含敏感信息
git commit -m "add password hardcoding: admin123"

# ❌ 4. 不要使用过长的主题行
git commit -m "feat(scheduler): implement a very complex scheduling algorithm \
that does many things and has a very long description that exceeds fifty characters"
```

### 15.3.4 提交检查清单

在执行 `git commit` 前检查：

- [ ] 提交类型符合 Conventional Commits 规范
- [ ] 作用域（scope）明确指定
- [ ] 主题行不超过 50 个字符
- [ ] 主题行以动词原形开头，首字母小写
- [ ] 主题行不以句号结尾
- [ ] 正文解释了"是什么"和"为什么"
- [ ] 正文每行不超过 72 个字符
- [ ] 关联了相关 Issue（如果存在）
- [ ] 标记了破坏性变更（如果有）
- [ ] 没有包含敏感信息

### 15.3.5 Git 配置

#### 自动化提交消息检查

安装 commitlint 工具：

```bash
npm install -g @commitlint/cli @commitlint/config-conventional
```

配置文件 `.commitlintrc.yml`：

```yaml
extends:
  - '@commitlint/config-conventional'

rules:
  type-enum:
    - feat
    - fix
    - docs
    - style
    - refactor
    - perf
    - test
    - chore
    - ci
    - revert

  scope-enum:
    - kernel
    - scheduler
    - mm
    - ipc
    - fs
    - driver
    - arch
    - crypto
    - build
    - config

  subject-case:
    - lower-case

  body-max-line-length: 72
```

#### Git Hooks 配置

`.git/hooks/commit-msg`:

```bash
#!/bin/bash
commitlint --edit "$1"
```

### 15.3.6 提交工作流

#### 功能开发工作流

```bash
# 1. 创建特性分支
git checkout -b feature/edf-scheduler

# 2. 开发并提交（遵循 Conventional Commits）
git add src/scheduler/edf.c
git commit -m "feat(scheduler): add EDF scheduling algorithm"

# 3. 更多提交
git add tests/test_edf.c
git commit -m "test(scheduler): add EDF unit tests"

# 4. 推送到远程
git push origin feature/edf-scheduler

# 5. 创建 Pull Request
# GitHub 会自动检测提交类型
```

#### Bug 修复工作流

```bash
# 1. 创建修复分支
git checkout -b fix/mm-page-leak

# 2. 修复并提交
git add src/mm/page_alloc.c
git commit -m "fix(mm): resolve memory leak in page allocator

The page allocator was not freeing pages on error path.

Fix: Add proper cleanup in error handling path.

Fixes #1234"

# 3. 推送并创建 PR
git push origin fix/mm-page-leak
```

### 15.3.7 版本号规范

遵循语义化版本（Semantic Versioning）：`MAJOR.MINOR.PATCH`

- **MAJOR**：不兼容的 API 变更
- **MINOR**：向后兼容的新功能
- **PATCH**：向后兼容的 Bug 修复

示例：
- `1.0.0` → `1.1.0`：添加新功能（MINOR）
- `1.1.0` → `1.1.1`：Bug 修复（PATCH）
- `1.1.1` → `2.0.0`：破坏性变更（MAJOR）

