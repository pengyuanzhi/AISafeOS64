# Conventional Commits 工具更新日志

## v2.0.0 - 2025-01-11

### 🎉 重大更新：全自动提交流程

**新增功能：**

#### 1. 智能文件检测与暂存
- ✅ 自动检测所有修改的文件（修改/删除/新增）
- ✅ 彩色显示文件状态（黄色=修改，红色=删除，绿色=新增）
- ✅ 显示当前分支信息
- ✅ 显示已暂存的文件列表

#### 2. 三种暂存模式
- **交互式选择** (推荐)：
  - 逐个选择要暂存的文件
  - 支持查看文件差异（`d <编号>`）
  - 支持全选（`a`）和取消全选（`n`）
  - 支持多选（输入多个编号）
- **暂存所有更改**：一键暂存所有修改
- **跳过暂存**：使用已暂存的文件

#### 3. 改进的用户界面
- 彩色输出（ANSI 颜色代码）
- 清晰的文件状态显示
- 友好的操作提示
- 详细的错误消息

#### 4. 完整的提交流程
```
1. 运行脚本
   ↓
2. 显示仓库状态
   ↓
3. 选择暂存方式
   ↓
4. 交互式选择文件（可选）
   ↓
5. 确认暂存内容
   ↓
6. 生成提交消息
   ↓
7. 执行提交
```

**使用示例：**

```bash
# 1. 修改文件
vim src/kernel/scheduler.c

# 2. 运行智能提交（只需一个命令！）
./scripts/smart_commit.sh

# 3. 选择模式
选择暂存方式:
  1) 交互式选择文件 (推荐)  # ← 选这个
  2) 暂存所有更改
  3) 跳过暂存

# 4. 选择文件
选择文件> 1 3    # 选择文件 1 和 3
选择文件> [Enter]

# 5. 提交消息生成（交互式）
选择类型: feat
输入主题: add EDF scheduling algorithm

# 6. 完成！
✓ 提交成功!
```

**交互式命令：**

| 命令 | 说明 |
|------|------|
| `1 3 5` | 选择/取消选择文件（支持多选） |
| `a` | 全选所有文件 |
| `n` | 取消全选 |
| `d 1` | 查看文件 1 的详细差异 |
| `Enter` | 确认并暂存 |
| `q` | 取消操作 |

**文件状态说明：**

| 状态 | 颜色 | 说明 |
|------|------|------|
| `M` | 黄色 | 修改的文件 |
| `D` | 红色 | 删除的文件 |
| `??` | 绿色 | 未跟踪的新文件 |
| `✓` | 绿色 | 已选择暂存 |

**Bug 修复：**
- 修复 Windows 编码兼容性问题（UTF-8）
- 修复 bash `read -p` 语法错误
- 改进错误处理和边界情况

**文档更新：**
- 新增完整的快速入门指南
- 新增交互式选择示例
- 更新完整使用指南
- 新增故障排除章节

**依赖项：**
- Python 3.6+
- Git 2.0+
- Bash 4.0+ (或兼容的 shell)

---

## v1.0.0 - 2025-01-11

### 初始版本

**核心功能：**
- Conventional Commits 格式验证
- 交互式提交消息生成
- Git Hooks 集成
- 跨平台支持（Windows/Linux/macOS）

**已创建的文件：**
- `.commitlintrc.yml` - Commitlint 配置
- `scripts/conventional_commit.py` - 核心验证和生成工具
- `scripts/install_hooks.sh` - Linux/macOS 安装脚本
- `scripts/install_hooks.ps1` - Windows 安装脚本
- `scripts/smart_commit.sh` - 智能提交助手
- `docs/CONVENTIONAL_COMMITS.md` - 完整使用指南
- `docs/CONVENTIONAL_COMMITS_QUICKSTART.md` - 快速入门
- `scripts/README_CONVENTIONAL_COMMITS.md` - 工具总结

**支持的提交类型：**
`feat` | `fix` | `docs` | `style` | `refactor` | `perf` | `test` | `chore` | `ci` | `revert`

**支持的作用域：**
`kernel` | `scheduler` | `mm` | `ipc` | `fs` | `driver` | `arch` | `crypto` | `build` | `config`

---

## 使用指南

### 安装

```bash
# Linux/macOS/Git Bash
./scripts/install_hooks.sh

# Windows PowerShell
.\scripts\install_hooks.ps1
```

### 日常使用

```bash
# 全自动提交（推荐）
./scripts/smart_commit.sh

# 仅生成消息
python scripts/conventional_commit.py generate

# 验证消息
python scripts/conventional_commit.py validate <file>
```

### 配置

编辑 `.commitlintrc.yml` 自定义验证规则。

---

## 路线图

### 未来计划

- [ ] 支持 `.gitignore` 智能过滤
- [ ] 支持提交模板（常用提交消息预设）
- [ ] 支持批量提交模式
- [ ] 支持 commitlint 集成
- [ ] 支持自动生成 CHANGELOG
- [ ] 支持提交消息搜索和统计
- [ ] 支持 Git GUI 集成

### 贡献

欢迎提交 Issue 和 Pull Request！

---

## 参考资料

- [Conventional Commits 规范](https://www.conventionalcommits.org/)
- [项目编码规范 - Git 工作流](./.claude/rules/git-workflow.md)
- [快速入门指南](./docs/CONVENTIONAL_COMMITS_QUICKSTART.md)
- [完整使用指南](./docs/CONVENTIONAL_COMMITS.md)
