# 代码格式化指南

## 概述

AISafe64 项目使用 **clang-format** 工具自动强制执行代码风格规范，确保所有提交的代码符合统一的标准。

## 自动格式化

### Git Hooks

项目使用 Git `pre-commit` hook 自动检查代码格式：

1. **提交前自动检查**：每次 `git commit` 时自动运行
2. **自动格式化**：如果代码格式不符合标准，自动运行 `clang-format`
3. **拒绝提交**：如果格式化改变了文件，拒绝提交并提示用户
4. **用户确认**：用户检查格式化后的更改，然后重新提交

### 工作流程

```bash
# 1. 编辑代码（可能格式不规范）
vim src/kernel/scheduler.c

# 2. 暂存文件
git add src/kernel/scheduler.c

# 3. 尝试提交（pre-commit hook 运行）
git commit

# 输出：
# 正在检查代码格式...
#   格式化: src/kernel/scheduler.c
#
# 警告: 代码格式不正确，已自动格式化
#
# 以下文件已被格式化：
#   src/kernel/scheduler.c
#
# 请检查格式化后的更改，然后重新提交：
#   git add .
#   git commit

# 4. 检查格式化后的更改
git diff

# 5. 确认无误后，重新暂存并提交
git add .
git commit

# 输出：
# 正在检查代码格式...
# 代码格式检查通过
```

### 跳过格式化检查（不推荐）

如果确实需要跳过格式化检查：

```bash
git commit --no-verify -m "message"
```

## 手动格式化

### 格式化单个文件

```bash
clang-format -i src/kernel/scheduler.c
```

### 格式化所有 C/C++ 文件

**Linux/macOS:**
```bash
find . -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i
```

**Windows PowerShell:**
```powershell
Get-ChildItem -Recurse -Include *.c,*.h,*.cpp,*.hpp | ForEach-Object {
    clang-format -i $_.FullName
}
```

### 检查格式但不修改文件

```bash
# 查看格式化后的输出（不修改文件）
clang-format src/kernel/scheduler.c

# 使用差异模式查看会改变什么
clang-format --dry-run --Werror src/kernel/scheduler.c
```

## .clang-format 配置

项目根目录的 `.clang-format` 文件定义了代码格式规则。

### 主要配置项

| 配置项 | 值 | 说明 |
|--------|-----|------|
| **基于样式** | None | 自定义样式 |
| **括号风格** | Allman | 左大括号必须换行 |
| **缩进** | 4空格 | 不使用Tab |
| **行长度** | 120字符 | 最大行宽 |
| **指针对齐** | 右对齐 | `int* p` 而非 `int *p` |
| **单语句大括号** | 强制 | 即使单语句也必须使用大括号 |

### 关键规则

#### 1. Allman 括号风格

```c
/* ✅ 正确 */
void function(void)
{
    if (condition)
    {
        do_something();
    }
    else
    {
        do_other_thing();
    }
}

/* ❌ 错误 - K&R 风格 */
void function(void) {
    if (condition) {
        do_something();
    }
}
```

#### 2. 单语句必须使用大括号

```c
/* ✅ 正确 */
if (condition)
{
    x = 1;
}

/* ❌ 错误 */
if (condition) {
    x = 1;
}
```

#### 3. 4空格缩进

```c
/* ✅ 正确 */
void function(void)
{
    int x = 0;
    if (x > 0)
    {
        x = x + 1;
    }
}

/* ❌ 错误 - 使用 Tab */
void function(void)
{
	int x = 0;
}
```

#### 4. 行长度限制

```c
/* ✅ 正确 - 不超过120字符 */
uint32_t result = scheduler_task_create(
    task_entry_function,
    priority_value
);

/* ❌ 错误 - 超过120字符 */
uint32_t result = scheduler_task_create(task_entry_function, priority_value, stack_size_bytes, task_name_string);
```

## 编辑器集成

### VS Code

安装 **C/C++** 扩展并配置：

`.vscode/settings.json`:
```json
{
    "[c]": {
        "editor.formatOnSave": true,
        "editor.defaultFormatter": "ms-vscode.cpptools"
    },
    "[cpp]": {
        "editor.formatOnSave": true,
        "editor.defaultFormatter": "ms-vscode.cpptools"
    },
    "C_Cpp.clang_format_style": "file",
    "C_Cpp.clang_format_fallbackStyle": "none"
}
```

### Vim/Neovim

创建 `~/.vim/ftplugin/c.vim`:
```vim
" 保存时自动格式化
autocmd BufWritePre *.c,*.h,*.cpp,*.hpp :clang-format

" 手动格式化快捷键
autocmd FileType c,cpp,h,hpp :nnoremap <buffer> <Leader>f :clang-format<CR>
autocmd FileType c,cpp,h,hpp :xnoremap <buffer> <Leader>f :clang-format<CR>
```

### Emacs

```elisp
;; 自动格式化
(add-hook 'c-mode-hook
          (lambda ()
            (add-hook 'before-save-hook 'clang-format-buffer nil t)))

;; 手动格式化
(global-set-key (kbd "C-c f") 'clang-format-buffer)
```

## CI/CD 集成

### 检查未格式化的文件

```bash
# 检查所有文件是否格式正确
find . -name '*.c' -o -name '*.h' | while read file; do
    if ! clang-format --dry-run --Werror "$file"; then
        echo "未格式化的文件: $file"
    fi
done
```

### GitLab CI 示例

```yaml
format-check:
  stage: test
  script:
    - find . -name '*.c' -o -name '*.h' | xargs clang-format --dry-run --Werror
  allow_failure: false
```

## 常见问题

### Q: 格式化后代码风格不喜欢怎么办？

A: 项目的代码风格是经过团队协商确定的，确保：
- 代码可读性一致
- 符合 MISRA-C:2012 规范
- 便于代码审查和维护

如果您认为某些规则不合理，请联系团队讨论修改 `.clang-format` 配置。

### Q: 格式化破坏了代码怎么办？

A: `clang-format` 非常稳定，极少破坏代码。如果发生：

1. 检查 `git diff` 查看具体变更
2. 使用 `git checkout -- .` 恢复
3. 报告问题给团队

### Q: 某些文件不想格式化怎么办？

A: 可以在文件中添加特殊注释：

```c
/* clang-format off */
void badly_formatted_function() {
    int x=1;int y=2;
}
/* clang-format on */
```

### Q: 如何检查格式化工具是否正确工作？

A: 运行测试：

```bash
# 创建测试文件
cat > test_format.c << 'EOF'
int main(){int x=1;return x;}
EOF

# 格式化并查看结果
clang-format -i test_format.c
cat test_format.c

# 预期输出（Allman 风格，4空格缩进）：
# int main()
# {
#     int x = 1;
#     return x;
# }
```

## 相关文档

- **编码规范**: `.claude/rules/code-style.md`
- **MISRA-C:2012**: `.claude/rules/misra-c2012.md`
- **Git Hooks 安装**: `scripts/install_hooks.sh`
- **Clang-Format 文档**: https://clang.llvm.org/docs/ClangFormat.html

## 配合使用

### 推荐工作流

```bash
# 1. 编辑代码
vim src/kernel/scheduler.c

# 2. 查看更改
git diff src/kernel/scheduler.c

# 3. 暂存（pre-commit hook 会在提交时检查格式）
git add src/kernel/scheduler.c

# 4. 尝试提交（自动格式化）
git commit

# 5. 如果格式被改变，检查变更
git diff

# 6. 确认后重新提交
git add .
git commit
```

### 智能提交助手

使用 `smart_commit.sh` 自动处理格式化：

```bash
# 自动格式化 + 提交
./scripts/smart_commit.sh -p
```

脚本会：
1. 自动暂存所有更改
2. pre-commit hook 自动格式化代码
3. 如果格式改变，提示重新暂存
4. 生成符合规范的提交消息
5. 提交并推送

## 总结

- ✅ **自动化**：Git hooks 自动检查和格式化
- ✅ **一致性**：所有代码遵循统一风格
- ✅ **MISRA合规**：符合 MISRA-C:2012 规范
- ✅ **易于使用**：格式化透明进行，用户无需手动操作
- ✅ **质量保证**：确保代码库始终保持高质量

**记住**：代码风格的一致性对于安全关键系统至关重要！
