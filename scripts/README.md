# AISafe64 脚本工具说明

本目录包含AISafe64项目的辅助脚本。

## git_push.sh - Git提交和推送脚本

### 功能

便捷地提交代码到远程Git仓库，自动处理：
- ✅ Git仓库状态检查
- ✅ 文件变更检测
- ✅ 自动添加所有变更文件
- ✅ 提交预览
- ✅ 创建提交（自动添加Co-Authored-By）
- ✅ 推送到远程仓库
- ✅ 彩色输出和错误处理

### 使用方法

#### 1. 直接使用脚本

```bash
# 提交到master分支（默认）
./scripts/git_push.sh "feat: 添加新功能"

# 提交到指定分支
./scripts/git_push.sh "fix: 修复bug" develop
./scripts/git_push.sh "docs: 更新文档" feature/new-feature
```

#### 2. 使用Makefile命令（推荐）

```bash
# 提交并推送
make MSG="feat: 添加新功能" git-push

# 查看Git状态
make status

# 查看提交历史
make log
```

#### 3. 配置Git别名（可选）

```bash
# 配置别名
git config --global alias.push-code '!f() { ./scripts/git_push.sh "$@"; }; f'

# 使用别名
git push-code "feat: 添加新功能"
git push-code "fix: 修复bug" develop
```

### 提交消息规范

遵循 [Conventional Commits](https://www.conventionalcommits.org/) 规范：

```
<type>(<scope>): <subject>
```

**类型 (type):**
- `feat` - 新功能
- `fix` - Bug修复
- `docs` - 文档变更
- `style` - 代码格式
- `refactor` - 重构
- `perf` - 性能优化
- `test` - 测试
- `chore` - 构建工具

**示例:**
```bash
make MSG="feat(sched): 添加EDF调度器" git-push
make MSG="fix(mm): 修复内存泄漏" git-push
make MSG="docs: 更新API文档" git-push
```

### 脚本特性

1. **安全检查**
   - 提交前显示变更文件列表
   - 显示提交预览
   - 遇到错误自动退出

2. **友好输出**
   - 彩色输出（信息/成功/警告/错误）
   - 清晰的进度提示
   - 详细的错误信息

3. **自动化**
   - 自动添加所有变更文件
   - 自动添加Co-Authored-By信息
   - 自动推送到远程仓库

### 故障排除

**问题：推送失败**
```
[ERROR] 推送失败
[INFO] 请检查网络连接或远程仓库配置
```

**解决方案：**
1. 检查网络连接
2. 验证远程仓库配置：`git remote -v`
3. 检查分支是否存在：`git branch -a`
4. 如果是权限问题，检查SSH密钥或凭据

**问题：没有检测到变更**
```
[WARNING] 没有检测到文件变更
```

**解决方案：**
1. 检查是否有实际修改：`git status`
2. 确认文件未被.gitignore忽略
3. 查看未跟踪的文件：`git status -u`

## 其他脚本

### parse_config.py
解析内核配置文件。

### sign_ecdsa.sh
使用ECDSA对文件进行数字签名。

## 贡献

添加新脚本时，请：
1. 添加可执行权限：`chmod +x scripts/your_script.sh`
2. 添加详细的使用说明
3. 包含错误处理
4. 遵循现有脚本的风格

## 相关文档

- [Git别名配置](../docs/git-aliases.md)
- [提交规范](../docs/CLAUDE.md#15.3-git-提交规范conventional-commits)
- [项目Makefile](../Makefile)
