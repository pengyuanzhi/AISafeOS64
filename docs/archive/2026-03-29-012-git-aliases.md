# Git Aliases 配置说明

本文档说明如何配置Git别名以便快速提交代码。

## 方法1: 使用Makefile（推荐）

```bash
# 提交到master分支
make MSG="feat: 添加新功能" git-push

# 提交到其他分支
make MSG="fix: 修复bug" commit BRANCH=develop

# 查看状态
make status

# 查看日志
make log
```

## 方法2: 直接使用脚本

```bash
# 提交到master分支
./scripts/git_push.sh "feat: 添加新功能"

# 提交到其他分支
./scripts/git_push.sh "fix: 修复bug" develop
```

## 方法3: 配置Git别名（可选）

### 全局配置（推荐）

将以下别名添加到 `~/.gitconfig`:

```bash
git config --global alias.push-code '!f() { ./scripts/git_push.sh "$@"; }; f'
```

然后可以使用：

```bash
git push-code "feat: 添加新功能"
git push-code "fix: 修复bug" develop
```

### 仅当前仓库配置

```bash
git config alias.push-code '!f() { ./scripts/git_push.sh "$@"; }; f'
```

## 其他有用的Git别名

### 添加到 ~/.gitconfig

```ini
[alias]
    # 状态和日志
    st = status
    lg = log --oneline --graph -10
    lol = log --oneline --graph --decorate --all

    # 提交相关
    cm = commit
    amend = commit --amend --no-edit
    unstage = reset HEAD --

    # 分支相关
    br = branch
    co = checkout
    cob = checkout -b

    # 推送和拉取
    ps = push
    pl = pull --rebase
```

## 使用示例

### 1. 日常提交流程

```bash
# 1. 修改文件后查看状态
make status

# 2. 提交并推送
make MSG="docs: 更新CLAUDE.md文档" git-push

# 3. 查看提交历史
make log
```

### 2. 不同类型的提交

```bash
# 新功能
make MSG="feat(sched): 添加EDF调度算法" git-push

# Bug修复
make MSG="fix(mm): 修复内存泄漏问题" git-push

# 文档更新
make MSG="docs: 更新API文档" git-push

# 重构
make MSG="refactor(task): 优化任务创建流程" git-push

# 性能优化
make MSG="perf(spinlock): 减少自旋锁开销" git-push
```

### 3. 提交到不同分支

```bash
# 使用脚本直接指定分支
./scripts/git_push.sh "feat: 新功能" feature/new-scheduler

# 或使用git别名
git push-code "feat: 新功能" feature/new-scheduler
```

## 提交消息规范

遵循 [Conventional Commits](https://www.conventionalcommits.org/) 规范：

### 格式

```
<type>(<scope>): <subject>

<body>

<footer>
```

### Type（类型）

- **feat**: 新功能
- **fix**: Bug修复
- **docs**: 文档变更
- **style**: 代码格式（不影响代码运行）
- **refactor**: 重构（既不是新功能也不是修复）
- **perf**: 性能优化
- **test**: 测试相关
- **chore**: 构建过程或辅助工具的变动

### Scope（范围）

常见的scope：
- **sched**: 调度器
- **mm**: 内存管理
- **sync**: 同步原语
- **irq**: 中断处理
- **driver**: 驱动程序
- **fs**: 文件系统
- **arch**: 架构相关

### 示例

```bash
make MSG="feat(sched): 添加EDF调度器支持

实现了最早截止时间优先（EDF）调度算法，支持实时任务调度。

- 添加sched_edf.c模块
- 实现任务截止时间管理
- 支持动态优先级调整

Closes #123" git-push
```

## 注意事项

1. **提交前检查**: 脚本会自动检查变更并显示预览
2. **分支默认**: 默认推送到master分支，可指定其他分支
3. **自动格式化**: 提交消息会自动添加Co-Authored-By信息
4. **错误处理**: 如果提交或推送失败，脚本会显示错误信息
