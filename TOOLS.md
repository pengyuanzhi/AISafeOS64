# TOOLS.md - AISafeOS64 项目工具配置

Skills define _how_ tools work. This file is for _your_ specifics.

## 项目信息

- **项目目录**: `/home/kerfs/AISafeOS64/AISafeOS64`
- **构建目录**: `build/`
- **输出文件**: `build/aisafe64.elf`, `build/aisafe64.bin`

## 开发工具

### 构建系统
```bash
# 配置项目
mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm64.cmake

# 编译
make -j$(nproc)

# 清理
make clean
```

### ⚠️ 强制编码规则：必须使用 Claude Code

**所有代码编写任务必须通过 Claude Code 完成，禁止手动逐行编辑大段代码。**

#### Claude Code CLI (推荐)
```bash
cd /home/kerfs/AISafeOS64/AISafeOS64
claude --permission-mode bypassPermissions --print '任务描述'
```

#### OpenClaw ACP Harness
```
sessions_spawn(
    runtime: "acp",
    task: "任务描述",
    cwd: "/home/kerfs/AISafeOS64/AISafeOS64"
)
```

#### 何时允许手动编辑
- 仅限单行 bug 修复、添加 #include、修改注释
- 超过 20 行的代码修改必须通过 Claude Code

### QEMU 模拟
```bash
# 运行内核
qemu-system-aarch64 \
  -M virt \
  -cpu cortex-a57 \
  -smp 4 \
  -m 1G \
  -kernel build/aisafe64.elf \
  -nographic \
  -serial mon:stdio
```

### 交叉编译工具链
- **工具链**: aarch64-none-elf-gcc
- **目标**: ARMv8-A (AArch64)
- **CMake 工具链**: `cmake/toolchain-arm64.cmake`

## ACP Harness 配置

### Claude Code
- **Runtime**: `acp`
- **Agent ID**: `claude-code` (或使用 `acp.defaultAgent`)
- **工作模式**: 代码开发、代码审查、调试

### Codex / Pi
- **Runtime**: `acp`
- **用途**: 备选开发助手

## 代码质量工具

### MISRA C:2012 检查
- **工具**: cppcheck / PC-lint / MISRA checker
- **要求**: 零偏差
- **CI 集成**: 自动检查

### 静态分析
- **工具**: clang-tidy, cppcheck
- **配置**: `.clang-format`, `.clang-tidy`

## 测试工具

### 单元测试
- **框架**: Unity / CppUTest
- **目录**: `tests/`
- **覆盖率要求**: 核心模块 > 80%

### QEMU 测试
```bash
# 运行测试
make test

# 或手动运行
qemu-system-aarch64 -M virt -kernel build/aisafe64.elf
```

## 调试工具

### GDB 调试
```bash
# 启动 QEMU 并等待 GDB
qemu-system-aarch64 -M virt -kernel build/aisafe64.elf -s -S

# 连接 GDB
aarch64-none-elf-gdb build/aisafe64.elf
(gdb) target remote localhost:1234
```

## 文档工具

### 架构图
- **工具**: Draw.io, PlantUML
- **目录**: `docs/design/`

### API 文档
- **工具**: Doxygen
- **输出**: `docs/api/`

## 注意事项

- 所有代码修改必须通过 MISRA C:2012 检查
- 提交前运行单元测试
- 重要的架构变更需要更新 `docs/design/ARCHITECTURE.md`
- 性能敏感代码需要在 QEMU 中测试延迟
