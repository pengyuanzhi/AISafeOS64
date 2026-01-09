# Git提交快速参考

## 三种提交方式

### 方式1: Makefile命令（最简单）⭐

```bash
# 提交到master分支
make MSG="feat: 添加新功能" git-push

# 提交到其他分支
make MSG="fix: 修复bug" git-push BRANCH=develop
```

### 方式2: 直接使用脚本

```bash
# 提交到master分支
./scripts/git_push.sh "feat: 添加新功能"

# 提交到其他分支
./scripts/git_push.sh "fix: 修复bug" develop
```

### 方式3: Git别名（需先配置）

```bash
# 配置别名（只需一次）
git config --global alias.push-code '!f() { ./scripts/git_push.sh "$@"; }; f'

# 使用别名
git push-code "feat: 添加新功能"
git push-code "fix: 修复bug" develop
```

## 常用命令

```bash
# 查看状态
make status

# 查看日志
make log

# 快速提交（别名）
make commit MSG="docs: 更新文档"
```

## 提交消息格式

```
<type>(<scope>): <subject>

类型: feat, fix, docs, style, refactor, perf, test, chore
范围: sched, mm, sync, irq, driver, fs, arch 等
```

## 示例

```bash
# 新功能
make MSG="feat(sched): 添加EDF调度器" git-push

# Bug修复
make MSG="fix(mm): 修复内存泄漏" git-push

# 文档
make MSG="docs: 更新README" git-push

# 重构
make MSG="refactor(task): 优化任务创建" git-push
```

## 详细文档

- [完整使用说明](./git-aliases.md)
- [脚本工具说明](../scripts/README.md)
- [提交规范](./CLAUDE.md#15.3-git-提交规范)
