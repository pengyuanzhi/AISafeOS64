# Conventional Commits 工具套件 - 总结

## 已创建的文件

### 配置文件
- **`.commitlintrc.yml`** - Commitlint 配置文件（供参考）
  - 定义了所有允许的提交类型和作用域
  - 配置了验证规则

### 核心工具
- **`scripts/conventional_commit.py`** - Python 验证和生成工具
  - 提交消息格式验证
  - 交互式提交消息生成
  - 支持 Windows/Linux/macOS
  - UTF-8 编码兼容

### Git Hooks 安装脚本
- **`scripts/install_hooks.sh`** - Linux/macOS/Git Bash 安装脚本
- **`scripts/install_hooks.ps1`** - Windows PowerShell 安装脚本

### 辅助工具
- **`scripts/smart_commit.sh`** - 智能提交助手
  - 引导式提交流程
  - 自动生成符合规范的消息
  - 一键完成提交

### 文档
- **`docs/CONVENTIONAL_COMMITS.md`** - 完整使用指南
- **`docs/CONVENTIONAL_COMMITS_QUICKSTART.md`** - 快速入门

## 功能特性

### 1. 提交消息验证
- 自动验证格式是否符合 Conventional Commits 规范
- 检查类型、作用域、主题、正文等各部分
- 提供详细的错误提示

### 2. 交互式消息生成
- 引导选择提交类型
- 引导选择作用域
- 逐步收集主题、正文、页脚信息
- 支持破坏性变更标记

### 3. Git Hooks 集成
- commit-msg hook：自动验证提交消息
- prepare-commit-msg hook：可扩展的消息模板
- 跨平台支持（Windows/Linux/macOS）

### 4. 智能提交助手
- 一键完成从暂存到提交的全流程
- 显示暂存的更改
- 自动生成符合规范的消息

## 使用流程

### 首次使用：安装 Git Hooks

```bash
# Linux/macOS/Git Bash
./scripts/install_hooks.sh

# Windows PowerShell
.\scripts\install_hooks.ps1
```

### 日常使用：智能提交

```bash
# 1. 暂存文件
git add <files>

# 2. 智能提交
./scripts/smart_commit.sh
```

### 高级用法：仅生成消息

```bash
# 1. 生成消息
python scripts/conventional_commit.py generate

# 2. 手动提交
git commit -F .git/COMMIT_EDITMSG
```

## 提交类型

| 类型 | 说明 |
|------|------|
| `feat` | 新功能 |
| `fix` | Bug 修复 |
| `docs` | 文档更新 |
| `style` | 代码格式 |
| `refactor` | 重构 |
| `perf` | 性能优化 |
| `test` | 测试相关 |
| `chore` | 构建/工具链 |
| `ci` | CI/CD 配置 |
| `revert` | 回滚提交 |

## 提交作用域

- `kernel` - 内核核心
- `scheduler` - 调度器
- `mm` - 内存管理
- `ipc` - 进程间通信
- `fs` - 文件系统
- `driver` - 设备驱动
- `arch` - 架构相关代码
- `crypto` - 加密/签名
- `build` - 构建系统
- `config` - 配置系统

## 提交消息格式

```
<type>(<scope>): <subject>

<body>

<footer>
```

**示例：**
```
feat(mm): add transparent huge page support

- Implement 2MB page allocation
- Add automatic huge page promotion
- Update page fault handler

This reduces TLB pressure and improves performance.

Closes #123
```

## 验证规则

- ✅ 提交类型必须是预定义的类型之一
- ✅ 作用域必须是预定义的作用域之一
- ✅ 主题至少 10 个字符
- ✅ 主题不超过 72 字符
- ✅ 主题不以句号结尾
- ✅ 主题以小写字母开头
- ✅ 正文每行不超过 72 字符
- ✅ 破坏性变更必须有说明

## 测试

工具已通过以下测试：
- ✅ 有效提交消息验证通过
- ✅ 无效提交消息正确拒绝
- ✅ Windows 编码兼容
- ✅ 错误消息显示正确

## 依赖项

- Python 3.6+
- Git 2.0+

## 故障排除

### Windows 编码问题
如果中文显示乱码，执行：
```bash
# Git Bash
export LANG=en_US.UTF-8

# PowerShell
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
```

### 跳过验证（不推荐）
```bash
git commit --no-verify -m "message"
```

## 参考资源

- [Conventional Commits 规范](https://www.conventionalcommits.org/)
- [项目编码规范 - Git 工作流](../.claude/rules/git-workflow.md)
- [完整使用指南](../docs/CONVENTIONAL_COMMITS.md)
- [快速入门](../docs/CONVENTIONAL_COMMITS_QUICKSTART.md)

## 贡献

欢迎提交 Issue 和 Pull Request 来改进这个工具！
