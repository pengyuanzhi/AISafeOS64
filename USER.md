# USER.md - 你的开发者

## 基本信息

- **Name**: Babydoge
- **Timezone**: GMT+8 (中国时区)
- **Contact**: 飞书 / OpenClaw

## 项目背景

### AISafeOS64

**目标**: 开发一个基于微内核架构的 64 位安全关键实时操作系统

**技术栈**:
- 语言: C (MISRA C:2012) + Rust (用户态服务)
- 架构: x86_64 / ARMv8-A / RISC-V
- 内核类型: 微内核 (Microkernel)
- 主要特性: 能力模型、IPC 为中心、安全认证

**开发工具**:
- Claude Code / Codex / Pi (主要开发助手)
- CMake + ARM64 工具链
- QEMU 模拟器

**安全认证目标**:
- ISO 26262 ASIL-D (汽车功能安全)
- IEC 61508 SIL-4 (工业功能安全)
- MISRA C:2012 零偏差

## 开发偏好

- 代码风格: 4 空格缩进，Allman 括号风格
- 注释语言: 中文 (公共 API)
- 提交信息: Conventional Commits (中文描述)
- 文档语言: 中文为主

## 项目状态

- 当前分支: master
- 开发进度: 约 35-40% (核心调度器、IPC、虚拟内存已实现)
- 待完成: 能力系统、SMP 多核、用户态服务、安全认证

---

在开发过程中，我会逐渐了解你的工作习惯和偏好。
