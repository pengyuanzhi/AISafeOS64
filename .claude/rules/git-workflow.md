## 15.3 Git 提交规范（Conventional Commits）

### 15.3.1 提交消息格式

AISafe64 项目严格遵循 **Conventional Commits** 规范，以确保提交历史的清晰和可追溯性。

#### 提交消息结构

```
<type>(<scope>): <subject>

<body>

<footer>
```

#### 语言规范

**AISafe64 项目强制使用中文编写提交消息**，以提高团队协作效率和代码可读性。

- ✅ **推荐**：使用中文描述提交内容和原因
- ⚠️ **允许**：type 和 scope 使用英文（便于工具解析）
- ❌ **禁止**：纯英文提交消息（除非特殊情况并经团队同意）

**示例对比**：

```bash
# ✅ 推荐：中文提交消息
feat(scheduler): 添加 EDF 调度算法支持

fix(mm): 修复页表损坏问题

docs(readme): 更新构建说明

# ⚠️ 允许但不推荐：英文提交消息
feat(scheduler): add EDF scheduling algorithm

# ❌ 禁止：中英文混杂
feat(scheduler): 添加 EDF scheduling support
```

#### 提交类型（type）

| 类型 | 描述 | 中文示例 |
|------|------|----------|
| `feat` | 新功能 | feat(scheduler): 添加 EDF 调度算法支持 |
| `fix` | Bug 修复 | fix(mm): 修复页表损坏问题 |
| `docs` | 文档更新 | docs(readme): 更新构建说明 |
| `style` | 代码格式（不影响功能） | style(kernel): 修复调度器缩进 |
| `refactor` | 重构（既不是新功能也不是修复） | refactor(ipc): 简化消息队列实现 |
| `perf` | 性能优化 | perf(scheduler): 使用 CLZ 优化优先级查找 |
| `test` | 测试相关 | test(mm): 添加页分配器单元测试 |
| `chore` | 构建/工具链相关 | chore(cmake): 更新工具链要求 |
| `ci` | CI/CD 配置 | ci(gitlab): 添加 MISRA 检查流水线 |
| `revert` | 回滚之前的提交 | revert: fix(mm): 修复页表损坏问题 |

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

- 使用动词原形开头（中文：添加、修复、更新等；英文：add、fix、update）
- 首字母大写（中文）或小写（英文）
- 不以句号结尾
- 限制在 50 个字符以内

**中文示例**：
```
✅ good: feat(scheduler): 添加 EDF 调度支持
✅ good: fix(mm): 修复页表损坏问题
❌ bad: 添加了 EDF 调度支持
❌ bad: feat(scheduler): 添加了EDF调度支持。
```

**英文示例（不推荐）**：
```
✅ good: feat(scheduler): add EDF scheduling support
❌ bad: Added EDF scheduling support.
```

#### 正文（body）

- 详细说明本次提交的**内容**和**原因**
- 每行限制在 72 个字符以内
- **必须**说明"是什么"和"为什么"
- 使用列表（- 或 *）列举主要变更

**中文示例**：
```
feat(scheduler): 添加 EDF 调度支持

- 实现最早截止时间优先（EDF）调度算法
- 添加红黑树用于截止时间跟踪
- 集成到现有的调度器类框架
- 更新配置以支持 FIFO/EDF/CFS 选择

这允许实时任务使用动态优先级调度，满足周期性任务的
截止时间要求，相比静态优先级 FIFO 提高了可调度性。

性能：入队/出队操作 O(log n)
```

**英文示例（不推荐）**：
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

- 关联 Issue：`关闭 #123`, `修复 #456` 或 `Closes #123`, `Fixes #456`
- 破坏性变更：`BREAKING CHANGE: <description>`（可用中文或英文）
- 引用相关提交：`联合署名: <name> <email>` 或 `Co-Authored-By: <name> <email>`

**中文示例**：
```
feat(api): 移除已废弃的 task_create 接口

破坏性变更: 旧的 task_create() 接口已被移除。
请迁移到 task_create_ex()，它支持更多参数。

关闭 #789

联合署名: 张三 <zhangsan@example.com>
```

**英文示例（不推荐）**：
```
feat(api): remove deprecated task_create interface

BREAKING CHANGE: The old task_create() interface has been removed.
Migrate to task_create_ex() which supports additional parameters.

Closes #789

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>
```

### 15.3.2 提交消息示例

#### 示例 1：新功能

**中文（推荐）**：
```
feat(mm): 添加透明大页支持

- 实现 2MB 大页分配
- 添加自动大页提升
- 更新页错误处理程序以支持混合页大小
- 添加 sysfs 接口用于统计

这减少了 TLB 压力，大型内存分配性能提升高达 30%。

性能：2MB 页分配耗时 <1ms
```

**英文（不推荐）**：
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

**中文（推荐）**：
```
fix(scheduler): 修复任务迁移中的竞态条件

任务迁移代码存在竞态条件，任务在另一个 CPU 上调度时
可能被迁移，导致释放后使用（use-after-free）。

修复：在迁移检查周围添加 RCU 读侧锁，
并更新调度器以正确处理迁移中的任务。

报告者: 张三 <zhangsan@example.com>
测试者: 李四 <lisi@example.com>
修复 #1234
```

**英文（不推荐）**：
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

**中文（推荐）**：
```
docs(CLAUDE.md): 添加 Git 提交规范规则

- 记录 Conventional Commits 规范
- 添加提交类型定义和示例
- 包含作用域指南和最佳实践
- **强制要求使用中文编写提交消息**

这确保项目中的提交消息保持一致性。
```

**英文（不推荐）**：
```
docs(CLARUDE.md): add Git commit convention rules

- Document Conventional Commits specification
- Add commit type definitions and examples
- Include scope guidelines and best practices

This ensures consistent commit messages across the project.
```

### 15.3.3 提交最佳实践

#### 必须遵循的规则

1. **使用中文编写提交消息**（type 和 scope 除外）
2. **使用 Conventional Commits 格式**
3. **每条提交只做一件事**
4. **提交消息清晰描述"是什么"和"为什么"**

#### DO（推荐做法）

```bash
# 1. 每个提交做一件事（使用中文）
git commit -m "feat(scheduler): 添加 EDF 调度算法"
git commit -m "test(scheduler): 添加 EDF 单元测试"

# 2. 使用完整的中文句子解释
git commit -m "fix(mm): 修复页分配器中的内存泄漏

页分配器在错误路径上未释放页面，导致每次失败分配
泄漏 4KB 内存。

修复：在错误处理路径中添加适当的清理。"

# 3. 引用相关 Issue
git commit -m "feat(driver): 添加 GPIO 驱动

为树莓派 4 实现基本的 GPIO 操作。

关闭 #456"
```

#### DON'T（不推荐做法）

```bash
# ❌ 1. 不要混合多个不相关的修改
git commit -m "更新各种内容"

# ❌ 2. 不要使用模糊的描述
git commit -m "修复问题"
git commit -m "更新代码"

# ❌ 3. 不要在提交消息中包含敏感信息
git commit -m "添加密码硬编码: admin123"

# ❌ 4. 不要使用过长的主题行
git commit -m "feat(scheduler): 实现一个非常复杂的调度算法，它做很多事情并且有非常长的描述，超过五十个字符"
```

### 15.3.4 提交检查清单

在执行 `git commit` 前检查：

- [ ] 提交类型符合 Conventional Commits 规范
- [ ] 作用域（scope）明确指定
- [ ] **使用中文编写提交消息**（type 和 scope 除外）
- [ ] 主题行不超过 50 个字符
- [ ] 主题行以动词原形开头（中文或英文）
- [ ] 主题行不以句号结尾
- [ ] 正文解释了"是什么"和"为什么"
- [ ] 正文每行不超过 72 个字符
- [ ] 没有包含敏感信息
- [ ] 关联了相关 Issue（如果存在）
- [ ] 标记了破坏性变更（如果有）

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

