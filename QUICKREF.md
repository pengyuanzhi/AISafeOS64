# AISafeOS64 Agent 快速参考

## Agent 信息

- **名称**: Kernel (微内核守护精灵)
- **标识**: 🛡️
- **工作空间**: `/home/kerfs/AISafeOS64/AISafeOS64`
- **项目**: AISafeOS64 微内核操作系统

## 核心文件

| 文件 | 用途 | 加载时机 |
|------|------|---------|
| `AGENTS.md` | 职责和工作流程 | 每次会话 |
| `SOUL.md` | 人格和专业理念 | 每次会话 |
| `IDENTITY.md` | 身份标识 | 每次会话 |
| `USER.md` | 开发者信息 | 每次会话 |
| `MEMORY.md` | 长期记忆 | 每次会话（仅主会话） |
| `TOOLS.md` | 工具配置 | 按需参考 |
| `HEARTBEAT.md` | 定期任务 | 心跳轮询时 |
| `memory/YYYY-MM-DD.md` | 日常日志 | 按需记录 |

## 主要能力

### 代码开发
- 使用 ACP Harness 调用 Claude Code / Codex / Pi
- 微内核核心代码开发（调度器、IPC、虚拟内存等）
- 用户态服务开发（Rust/C）

### 代码审查
- MISRA C:2012 合规检查
- 安全漏洞审查
- 性能分析

### 架构设计
- 微内核架构设计
- 模块划分和接口定义
- 技术决策支持

### 调试支持
- 问题排查
- QEMU 调试辅助
- 性能调优

## 项目状态（2026-04-03）

### 已完成 (~35-40%)
- ✅ 调度器 (256级优先级位图)
- ✅ IPC 子系统 (通道-连接模型)
- ✅ 虚拟内存管理
- ✅ 能力系统基础
- ✅ 线程管理
- ✅ 同步原语
- ✅ ARM64 HAL

### 进行中
- 🔄 能力系统完善
- 🔄 SMP 多核支持

### 待完成
- ⏳ 用户态服务（文件系统、网络、进程管理）
- ⏳ 安全认证准备
- ⏳ 形式化验证

## 技术规范

### MISRA C:2012 要求
- 零偏差目标
- 所有内核代码必须合规
- CI 自动检查

### 代码风格
- 4 空格缩进
- Allman 括号风格
- 最大行宽 120 字符
- 圈复杂度 <= 10

### 提交规范
```
feat(module): 添加新功能
fix(module): 修复问题
docs(module): 文档更新
```

## 快速命令

### 构建项目
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm64.cmake
make -j$(nproc)
```

### 运行 QEMU
```bash
qemu-system-aarch64 -M virt -cpu cortex-a57 -smp 4 -m 1G \
  -kernel build/aisafe64.elf -nographic -serial mon:stdio
```

### GDB 调试
```bash
# 终端1: 启动 QEMU
qemu-system-aarch64 -M virt -kernel build/aisafe64.elf -s -S

# 终端2: 连接 GDB
aarch64-none-elf-gdb build/aisafe64.elf
(gdb) target remote localhost:1234
```

## 使用 Agent

### 通过 OpenClaw 主会话
直接在当前会话中请求开发任务，agent 会自动使用工具。

### 通过 sessions_spawn
```bash
# 生成子 agent 处理任务
sessions_spawn --workspace /home/kerfs/AISafeOS64/AISafeOS64 \
  --runtime acp \
  "审查 scheduler.c 的代码质量"
```

---

**版本**: 1.0
**创建**: 2026-04-03
**维护**: Kernel 🛡️
