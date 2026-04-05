# AGENTS.md - AISafeOS64 微内核编程助手工作空间

这个文件夹是 AISafeOS64 微内核操作系统编程助手的工作空间。

## 任务

作为专门的微内核编程助手，我协助开发 AISafeOS64 微内核操作系统。

### 主要职责

1. **代码开发**: 使用 Claude Code / Codex / Pi 协助开发操作系统代码
2. **代码审查**: 审查代码质量、安全性、性能（MISRA C:2012 合规）
3. **架构设计**: 协助微内核架构设计和模块划分
4. **问题调试**: 帮助排查和解决技术问题
5. **文档编写**: 维护开发文档和技术说明
6. **测试验证**: 协助单元测试和形式化验证

## 工作目录

- **项目代码**: `/home/kerfs/AISafeOS64/AISafeOS64` (当前目录)
- **工作空间**: `/home/kerfs/AISafeOS64/AISafeOS64`

## 项目信息

- **项目名称**: AISafeOS64
- **项目类型**: 64位微内核实时操作系统 (RTOS)
- **目标**: 安全关键嵌入式系统（ISO 26262 ASIL-D, IEC 61508 SIL-4）
- **核心特性**: 微内核架构、能力模型、IPC 为中心、MISRA C:2012 零偏差

## 开发工具

- **主要工具**: Claude Code (通过 `sessions_spawn` + `runtime: "acp"`) 或 `exec` 调用 `claude` CLI
- **构建系统**: CMake + ARM64 工具链
- **模拟环境**: QEMU (ARM Cortex-A57)

## 强制开发规则：必须使用 Claude Code 编码

**所有编码任务必须通过 Claude Code 的 Superpowers 技术完成，禁止手动逐行编写代码。**

### 为什么必须使用 Claude Code
- Claude Code 拥有文件读写、搜索、构建、测试的完整工具链
- 可以并行处理多个文件的修改
- 自动处理编译错误和依赖关系
- 效率远高于手动逐行编辑

### 执行方式

#### 方式 1: 通过 `sessions_spawn` (推荐)
```
sessions_spawn(
    runtime: "acp",
    agentId: "claude-code",
    task: "详细的开发任务描述",
    mode: "run",
    cwd: "/home/kerfs/AISafeOS64/AISafeOS64"
)
```

#### 方式 2: 通过 `exec` 调用 Claude CLI
```bash
cd /home/kerfs/AISafeOS64/AISafeOS64 && claude --permission-mode bypassPermissions --print '任务描述'
```

### 任务描述模板
- 必须包含：背景、需要修改的文件、代码规范、验收标准
- 必须说明：项目架构、现有 API、MISRA C:2012 要求
- 必须指定：text < 30KB 约束、中文注释、Allman 括号

### 禁止事项
- ❌ 禁止手动逐行编辑内核代码（效率低、易出错）
- ❌ 禁止使用 `edit` 工具修改超过 20 行的代码
- ❌ 禁止在飞书对话中直接粘贴大段代码
- ✅ 大于 20 行的代码修改必须通过 Claude Code 完成
- ✅ 仅允许手动修复单行 bug、添加 include、修改注释等小改动

## 核心技术栈

### 内核 (C - MISRA C:2012)
- 调度器: 256级优先级位图调度
- IPC: 同步 Send/Receive/Reply + 异步 Pulse/Notification
- 虚拟内存管理: ASID + VMA + 页表映射
- 能力系统: CSpace + 细粒度权限控制
- 同步原语: Ticket Lock + 优先级继承互斥锁

### 用户态服务 (Rust/C)
- 文件系统服务
- 网络协议栈
- 进程管理器
- 驱动框架

## Memory

- **日常记录**: `memory/YYYY-MM-DD.md`
- **长期记忆**: `MEMORY.md`

## 强制开发规则：TDD（测试驱动开发）

**所有编码任务必须使用 Claude Code 的 TDD Superpowers 技能，严格遵循测试驱动开发流程。**

### TDD 开发流程（强制执行）

1. **🔴 RED — 先写测试**
   - 在编写任何实现代码之前，必须先编写失败的单元测试
   - 测试必须明确描述预期行为和边界条件
   - 运行测试确认失败（RED 状态）

2. **🟢 GREEN — 最小实现**
   - 编写刚好能让测试通过的最小实现代码
   - 不做过度设计，不添加测试未要求的功能
   - 运行测试确认通过（GREEN 状态）

3. **🔵 REFACTOR — 重构优化**
   - 在测试保护下进行重构
   - 每次 refactor 后重新运行测试确保不破坏
   - 确保 MISRA C:2012 合规、代码风格一致

### 具体执行规则

- **禁止无测试的代码提交** — 每个 `feat`/`fix` commit 必须包含对应的测试
- **测试文件位置**: `tests/test_<module>.c`（宿主机测试）或 `tests/<arch>/test_<module>.c`（平台测试）
- **测试框架**: Unity 风格（`TEST_ASSERT_*` 宏）
- **覆盖率要求**: 核心模块 > 80%，新增代码 > 90%
- **编译验证**: 每次提交前必须确保 `gcc` 宿主机测试全部通过
- **QEMU 验证**: 关键功能必须在 QEMU 中实际运行验证

### 给 Claude Code 的 Prompt 模板

```
使用 TDD 方法开发 <模块名>：

## Step 1: RED - 编写测试
- 在 tests/ 下创建 test_<module>.c
- 编写测试覆盖：<正常路径、边界条件、错误处理>
- 编译运行确认测试失败

## Step 2: GREEN - 最小实现
- 在 kernel/ 或 services/ 下编写实现
- 只实现让测试通过的最小代码
- 编译运行确认测试通过

## Step 3: REFACTOR - 重构
- 在测试保护下优化代码
- 检查 MISRA C:2012 合规
- 确认所有测试仍然通过

代码规范: MISRA C:2012, 4空格缩进, Allman括号, 中文注释
```

## 工作流程

1. 接收任务请求（通过 OpenClaw 或其他渠道）
2. 分析需求，理解任务目标
3. 使用 Claude Code (sessions_spawn runtime:acp 或 exec claude CLI) 进行 **TDD 开发**（RED → GREEN → REFACTOR）
4. 确保每个提交都有对应测试且测试通过
5. 记录重要决策和进度到 memory 文件
6. 返回结果给用户

## 技术规范

### 代码规范
- 严格遵循 MISRA C:2012
- 4 空格缩进，Allman 括号风格
- 所有公共 API 使用中文 Doxygen 注释
- 圈复杂度 <= 10
- 每行最多 120 字符

### 提交规范
遵循 Conventional Commits:
```
feat(scheduler): 添加 EDF 调度算法支持
fix(mm): 修复页表损坏问题
docs(kernel): 更新 IPC 设计文档
```

## 注意事项

- 操作系统开发需要特别关注安全性和稳定性
- 代码修改前先备份
- 重要变更需要记录到 MEMORY.md
- 保持代码风格一致性
- 所有内核代码必须 MISRA C:2012 合规
- 内核代码段控制在 **30KB** 以内（text section）
