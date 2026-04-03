# AISafeOS64 Agent - 使用指南

## ✅ Agent 已配置完成

**Agent ID**: `aisafeos`  
**Name**: `AISafeOS64 Kernel`  
**工作空间**: `/home/kerfs/AISafeOS64/AISafeOS64`  
**模型**: `zai/glm-5.1` ✨ (已升级)  
**Emoji**: 🛡️

---

## 🚀 使用方法

### 方法 1: 启动独立 Session（推荐）

```bash
# 启动 aisafeos agent 的独立 session
openclaw session start --agent aisafeos
```

### 方法 2: 在当前会话中调用

在 OpenClaw 主会话中，可以请求子 agent 处理任务：

```
请使用 aisafeos agent 审查 scheduler.c 的代码
```

### 方法 3: 通过 sessions_spawn

```bash
sessions_spawn --workspace /home/kerfs/AISafeOS64/AISafeOS64 \
  --runtime acp \
  --agentId aisafeos \
  "审查 kernel/scheduler.c 的 MISRA C 合规性"
```

---

## 📋 Agent 能力

### ✅ 代码开发
- 微内核核心代码（调度器、IPC、虚拟内存、能力系统）
- 用户态服务（Rust/C）
- 驱动框架

### ✅ 代码审查
- MISRA C:2012 合规检查
- 安全漏洞审查
- 性能分析

### ✅ 架构设计
- 微内核架构设计
- 模块划分和接口定义
- 技术决策支持

### ✅ 调试支持
- 问题排查
- QEMU 调试辅助
- 性能调优

---

## 🎯 示例任务

```
# 代码审查
"审查 kernel/scheduler.c 的代码质量"

# 功能实现
"帮我实现能力撤销功能"

# 性能优化
"优化 IPC 快速路径性能"

# 架构设计
"设计用户态进程管理器的接口"

# 调试支持
"帮我排查 page fault 问题"

# 文档编写
"更新 docs/design/ARCHITECTURE.md 中的 IPC 章节"
```

---

## 📂 项目结构

```
AISafeOS64/
├── kernel/          # 微内核核心（调度、IPC、能力管理）
├── drivers/         # 用户态驱动框架
├── services/        # 用户态服务
├── include/         # 公共头文件
├── tests/           # 测试套件
├── docs/            # 项目文档
└── scripts/         # 构建脚本
```

---

## 🔧 技术栈

- **语言**: C (MISRA C:2012) + Rust
- **架构**: x86_64 / ARMv8-A / RISC-V
- **构建**: CMake + ARM64 工具链
- **模拟**: QEMU (Cortex-A57)

---

## 📊 项目状态

**进度**: ~35-40% 完成

**已完成**: 调度器、IPC、虚拟内存、能力基础、线程管理、同步原语、ARM64 HAL  
**进行中**: 能力系统完善、SMP 多核  
**待完成**: 用户态服务、安全认证、形式化验证

---

**配置时间**: 2026-04-03 22:41  
**Gateway 状态**: ✅ 已重启，配置生效
