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
- 必须指定：text < 50KB 约束、中文注释、Allman 括号

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

## 强制开发规则：自动提交（Auto-Commit）

**每个任务完成后必须自动提交代码，确保代码持续集成。**

### 提交时机
- ✅ **每次任务完成后**：立即提交（不等待多个任务累积）
- ✅ **Bug 修复后**：立即提交验证
- ✅ **新增功能后**：立即提交测试通过版本
- ✅ **重构完成后**：立即提交优化版本

### 提交规范
1. **文件选择**：只提交相关文件
   - 只添加/修改完成任务所需的文件
   - 不提交构建产物（`build/`、`*.dis`、`*.bin` 等）
   - 不提交临时文件和调试文件

2. **Commit Message 格式**：使用 Conventional Commits
   - 标题：`<type>(<scope>): <description>`
   - 类型：`feat` / `fix` / `docs` / `refactor` / `test`
   - 描述：中文，简洁明了
   - 正文：详细说明（可选）

3. **Commit 内容**：包含完整的变更说明
   - 列出新增/修改的文件
   - 说明核心功能和技术要点
   - 验证结果（QEMU 测试、单元测试）
   - 架构特点和设计决策

4. **验证清单**（提交前检查）
   - [ ] 编译成功（无警告）
   - [ ] 测试通过（QEMU 验证）
   - [ ] 代码风格符合规范（MISRA C:2012）
   - [ ] 注释完整（中文 Doxygen）
   - [ ] text 段大小检查（内核 < 50KB）

### 提交命令模板
```bash
# 添加相关文件
git add <相关文件列表>

# 提交（包含详细说明）
git commit -m "<type>(<scope>): <中文描述>

## 核心功能

### 1. <功能模块 1>
- ✅ <具体实现 1>
- ✅ <具体实现 2>

### 2. <功能模块 2>
- ✅ <具体实现 1>

## 验证结果
- ✅ <测试 1>
- ✅ <测试 2>

## 架构特点
- <特点 1>
- <特点 2>"
```

### 禁止事项
- ❌ 禁止多个任务累积后一次性提交
- ❌ 禁止提交未验证的代码
- ❌ 禁止提交包含编译错误的代码
- ❌ 禁止提交构建产物和临时文件
- ❌ 禁止使用空 commit message

## 强制开发规则：体系架构分层（Architecture Independence）

**内核核心代码必须体系架构无关，所有硬件相关操作必须通过 HAL 层接口。**

### 分层架构

```
┌─────────────────────────────────────────┐
│  用户态服务 (Rust/C)                      │
├─────────────────────────────────────────┤
│  内核核心 (C - 体系架构无关)               │  ← scheduler, ipc, cap, mm(高层)
│    仅通过 hal.h / hal_*.h 接口访问硬件     │
├─────────────────────────────────────────┤
│  HAL 硬件抽象层 (C - 体系架构相关)         │  ← hal.h, hal_timer.h, hal_mmu.h ...
├─────────────────────────────────────────┤
│  架构实现 (C/ASM - ARM64/RISC-V/...)      │  ← kernel/arch/arm64/
└─────────────────────────────────────────┘
```

### 目录结构规则

| 目录 | 允许的内容 | 禁止的内容 |
|------|-----------|------------|
| `kernel/sched/` | 调度算法、线程管理 | `__asm__`, `msr`, `mrs`, `isb`, `wfe` |
| `kernel/ipc/` | IPC 通道/端点/通知 | `__asm__`, 寄存器名 |
| `kernel/cap/` | 能力系统 | `__asm__`, 寄存器名 |
| `kernel/mm/` | 高层内存管理 | `__asm__`, `ttbr`, `tlbi`, `msr` |
| `kernel/irq/` | 中断/系统调用分发 | 直接寄存器操作 |
| `kernel/verify/` | 形式化验证 | 体系架构相关代码 |
| `kernel/arch/arm64/` | ARM64 实现 | — (这里可以写任何东西) |

### HAL 接口清单（kernel/arch/arm64/hal.h）

内核核心代码 **只能** 使用以下 HAL 接口访问硬件：

```c
/* CPU 信息 */
uint32_t hal_get_cpu_id(void);
uint32_t hal_get_current_el(void);

/* 中断控制 */
void hal_irq_enable(void);
void hal_irq_disable(void);
void hal_irq_disable_all(void);
uint32_t hal_irq_saved_state(void);
void hal_irq_restore(uint32_t state);

/* 缓存维护 */
void hal_dcache_clean(uint64_t start, uint64_t size);
void hal_dcache_invalidate(uint64_t start, uint64_t size);
void hal_dcache_clean_and_invalidate(uint64_t start, uint64_t size);
void hal_tlb_invalidate_all(void);

/* UART 输出 */
void hal_uart_init(uint64_t base);
void hal_uart_putc(uint64_t base, char ch);
void hal_uart_puts(uint64_t base, const char *str);
void hal_uart_enable_rx_irq(uint64_t base);
int hal_uart_getc(uint64_t base, char *ch);

/* 栈对齐检查 */
void hal_enable_stack_alignment_check(void);
```

### 已补充的 HAL 接口（均已实现）

以下接口已全部从内核核心代码抽离到 HAL 层，在 `kernel/arch/arm64/hal.h` 声明、`hal.c` 实现，核心代码（`kernel/sched/`、`kernel/ipc/`、`kernel/mm/`）已无任何 `__asm__`/`msr`/`mrs`/`wfe`/`tlbi` 等体系架构相关代码：

```c
/* 定时器 - 已从 timer.c 抽离 */
uint64_t hal_timer_get_count(void);        /* 替代 mrs cntpct_el0 */
uint64_t hal_timer_get_freq(void);         /* 替代 mrs cntfrq_el0 */
uint64_t hal_timer_get_control(void);      /* 替代 mrs cntp_ctl_el0 */
void hal_timer_set_compare(uint64_t val);  /* 替代 msr cntp_cval_el0 + isb */
void hal_timer_set_control(uint64_t val);  /* 替代 msr cntp_ctl_el0 + isb */

/* 内存屏障 - 已从 ic2.c 抽离 */
void hal_dmb_ish(void);       /* 数据内存屏障: Inner Shareable */
void hal_dmb_ishst(void);     /* 数据内存屏障: Inner Shareable, Store */
void hal_dmb_ishld(void);     /* 数据内存屏障: Inner Shareable, Load */

/* 页表 - 已从 page_table.c 抽离 */
uint64_t hal_read_ttbr0(void);          /* 替代 mrs ttbr0_el1 */
uint64_t hal_read_ttbr1(void);          /* 替代 mrs ttbr1_el1 */
void hal_write_ttbr0(uint64_t val);     /* 替代 msr ttbr0_el1 + isb */
void hal_write_ttbr1(uint64_t val);     /* 替代 msr ttbr1_el1 + isb */
void hal_tlb_invalidate_asid(uint64_t asid);  /* 替代 tlbi aside1is */
void hal_tlb_invalidate_all(void);      /* 替代 tlbi vmalle1is */

/* 低功耗等待 - 已从 scheduler.c / thread.c 抽离 */
void hal_wfe(void);  /* 替代 wfe 指令（scheduler.c 5处、thread.c 2处、spinlock.c 1处） */
```

> **注意**：ASID 切换原计划的 `hal_set_asid()` 接口未单独实现，地址空间切换通过
> `hal_write_ttbr0(val)`（val 包含 ASID 字段）完成，效果等价，无需额外接口。

### 体系架构独立性现状（已全部修复）

内核核心代码（`kernel/` 非 `arch/` 目录）的 HAL 迁移**已全部完成**。历史违规清单中的所有项均已修复：

| 文件 | 历史违规 | 修复方式 |
|------|---------|---------|
| `kernel/sched/timer.c` | `mrs cntpct_el0` 等 4 处 | 改用 `hal_timer_*` 接口 |
| `kernel/sched/scheduler.c` | `wfe` 5 处 | 改用 `hal_wfe()` |
| `kernel/sched/thread.c` | `wfe` 2 处 | 改用 `hal_wfe()` |
| `kernel/sched/spinlock.c` | `wfe` 1 处 | 改用 `hal_wfe()` |
| `kernel/ipc/ic2.c` | `dmb ish/ishst/ishld` 3 处 | 改用 `hal_dmb_*` 接口 |
| `kernel/mm/page_table.c` | `mrs/msr ttbr*_el1`、`tlbi` 9 处 | 改用 `hal_read/write_ttbr*`、`hal_tlb_*` |
| `kernel/mm/vmspace.c` | `msr ttbr0_el1`、`isb` 3 行 | 改用 `hal_write_ttbr0` |

**核查方法**：`grep -rE "__asm|mrs |msr |wfe|wfi|tlbi|dmb " kernel/sched kernel/ipc kernel/mm kernel/cap kernel/irq kernel/verify` 应无任何匹配（仅注释文本可忽略）。

### 代码审查规则

**每次代码审查必须检查以下项：**

1. **体系架构独立性**: `kernel/` 非 `arch/` 目录下禁止出现 `__asm__`、`msr`、`mrs`、`isb`、`dsb`、`dmb`、`tlbi`、`wfe`、`wfi` 等体系架构相关代码
2. **HAL 接口使用**: 所有硬件操作必须通过 `hal.h` 中定义的接口
3. **MISRA C:2012 合规**: Rule 1.1 (未使用代码)、Rule 8.13 (pointer should be const)、Dir 4.9 (结构体/联合体应有 typedef)
4. **text < 50KB**: 每次提交后检查 `aarch64-linux-gnu-size build/kernel/aisafe64.elf.elf`（当前约 41KB，软目标 40KB，硬上限 50KB）
5. **无重复定义**: 同一符号不允许出现多次 tentative definition
6. **中文注释**: 所有公共 API 使用中文 Doxygen 注释
7. **Allman 括号 + 4空格缩进**

### 给 Claude Code 的审查 Prompt

使用 `/project:code-review` 或以下 prompt 进行代码审查：
```
审查以下文件的体系架构独立性：
1. 检查 kernel/ 非 arch/ 目录下是否有 __asm__ 内联汇编
2. 检查是否有直接使用 ARM64 寄存器名（msr/mrs/ttbr/tlbi/wfe 等）
3. 检查是否有 #include "hal.h" 以外的硬件相关头文件
4. 验证所有 HAL 接口调用是否正确
5. 检查 MISRA C:2012 合规性
```

## 强制开发规则：禁止折中方案（No Compromise）

**这是商业安全关键系统（ISO 26262 ASIL-D / IEC 61508 SIL-4），所有实现必须是最优设计，禁止任何形式的折中、妥协或"过渡方案"。**

### 核心原则

1. **方案最优**：每个设计决策必须是该问题的最优解，不能因为"简单"或"快速"而选择次优方案
2. **设计先进**：架构必须对标 seL4/Zircon/Fuchsia 等最先进的微内核设计
3. **实时性保证**：所有路径必须满足硬实时约束（确定性延迟、无不可控阻塞）
4. **彻底实现**：不允许"TODO"、"后续完善"、"暂时回退"等遗留
5. **根因修复**：遇到问题必须找到根因并彻底修复，不允许在表面打补丁

### 禁止事项

- ❌ **禁止过渡方案**：如"先用 owner 检查，后续替换为 cap_validate"——必须一次到位
- ❌ **禁止硬编码 hack**：如硬编码物理地址、魔法数字绕过问题
- ❌ **禁用条件编译掩盖问题**：如 `#if 0` 或 `#if CONFIG_DEBUG` 排除有问题的代码
- ❌ **禁止"能用就行"**：代码不仅要工作，还必须设计合理、架构正确
- ❌ **禁止绕过 HAL**：内核核心代码不得直接操作硬件寄存器
- ❌ **禁止 Demand Paging / 内存换出**：硬实时系统要求 WCET 可静态分析。
  demand paging 引入不可预测的缺页中断延迟（毫秒级），破坏时序确定性。
  所有用户内存必须**预映射（eager mapping）**，物理内存必须预留保证。
  参照 seL4/QNX/VxWorks 等安全关键 RTOS 的设计：无换出、无 overcommit。
  缺页即程序错误（越界/空指针），终止出错线程。

### 验证标准

每个实现必须满足：
- ✅ 编译无警告（`-Wall -Wextra`）
- ✅ 单核 + 多核 QEMU 均正常运行
- ✅ text 段 < 50KB（内核映像大小约束）
- ✅ MISRA C:2012 合规
- ✅ 中文 Doxygen 注释完整
- ✅ 设计文档说明架构决策

### 当遇到困难时

- 不要降低标准，而是**深入调试到根因**
- 使用 GDB/QEMU `-d guest_errors` 等工具精确诊断
- 必要时重写整个模块（如 elf_loader 完全重写）
- 如果当前方案无法满足要求，**重新设计**而非打补丁

## 强制开发规则：实时性保证（Real-Time Guarantees）

**这是硬实时操作系统，所有内核路径必须满足确定性延迟约束。不允许任何不可预测的时间行为。**

### 实时性硬约束

1. **中断延迟（Interrupt Latency）**
   - 关中断区间（`hal_irq_disable` → `hal_irq_restore`）**不得超过 10μs**
   - 临界区内**禁止**调用任何可能阻塞的函数（I/O、kmalloc、长循环）
   - 临界区内**禁止**超过 50 行代码（保持极短路径）

2. **调度延迟（Scheduling Latency）**
   - 高优先级线程就绪到获得 CPU：**≤ 1 个 tick（1ms）**
   - 主动抢占路径（唤醒 → schedule）：**≤ 10μs**
   - 禁止在中断处理中执行可能超过 50μs 的操作

3. **系统调用延迟**
   - 每个系统调用必须有**有界执行时间**
   - 禁止在系统调用中等待不可预测事件（如磁盘 I/O、网络）
   - IPC Send/Receive 超时必须有界（不允许永久阻塞而无超时）

4. **确定性内存分配**
   - 内核初始化后**禁止动态内存分配用于关键路径**（kmalloc 仅用于非实时路径）
   - 实时线程的资源（栈、页表、消息缓冲）必须在**创建时预分配**
   - 参照 IEC 61508 Part 3 Table A.4："安全相关软件应避免动态对象"

### 禁止的非确定性行为

- ❌ **禁止动态分配在实时路径**：调度、IPC、中断处理中禁止 kmalloc/kfree
- ❌ **禁止无界循环**：所有循环必须有明确的退出条件（超时或计数器）
- ❌ **禁止不可预测的 I/O 等待**：DMA 传输必须有超时机制
- ❌ **禁止锁竞争不可预测**：所有自旋锁区域必须有界（见并发规则）
- ❌ **禁止优先级反转**：共享资源必须使用优先级继承协议（Priority Inheritance）

### WCET 可分析性

每个内核函数应具备**可静态分析的 WCET**（最坏情况执行时间）：
- 避免 `while (*status & FLAG)` 类轮询（无超时）
- 避免递归（栈深度不可预测）
- 避免函数指针（调用图不可分析，除非通过受控的分发表）
- 中断处理路径的指令数应有文档说明

## 强制开发规则：确定性约束（Determinism）

**内核行为必须在所有情况下可预测、可复现。同样的输入必须产生同样的执行路径。**

### 确定性要求

1. **调度确定性**
   - 相同的线程优先级集合必须产生相同的调度序列
   - EDF 调度的截止时间排序必须确定（相同截止时间用线程 ID 打破平局）
   - 禁止使用 `rand()`、时间戳等非确定源做调度决策

2. **内存布局确定性**
   - BSS 段在启动时清零（boot.S 保证）
   - 物理内存分配器（buddy）从固定基址开始，相同序列的分配产生相同布局
   - 禁用 ASLR（地址空间随机化）——安全关键系统需要确定性地址

3. **初始化确定性**
   - 所有子系统的初始化顺序必须固定（entry.c 中的调用序列）
   - 硬件寄存器必须显式初始化（不依赖复位值）
   - 全局变量必须有显式初值（不依赖 BSS 零值，除非有意为之）

4. **并发确定性**
   - 锁获取顺序必须固定（避免死锁，见并发规则）
   - IPI 消息处理顺序必须确定
   - 禁止依赖线程执行速度的时序假设

### 禁止事项

- ❌ **禁止随机化**：无 ASLR、无随机堆偏移、无随机栈位置
- ❌ **禁止依赖时序的并发逻辑**：不能用 `delay_us(10)` 做同步
- ❌ **禁止未定义行为**：所有 C 语言的未定义行为必须消除（MISRA C:2012 Rule 1.3）

## 强制开发规则：可靠性与容错（Reliability & Fault Tolerance）

**系统必须在硬件故障、软件 bug、外部攻击下保持安全状态。故障不得导致不可预测行为。**

### 故障检测

1. **栈溢出检测**（已实现）
   - 每个线程栈底有金丝雀（STACK_CANARY_MAGIC）
   - 调度切换时检查金丝雀
   - 检测到溢出：终止线程 + 安全报告

2. **页表完整性验证**
   - PTE 的物理地址必须指向已分配的物理页
   - 关键页表操作后应做一致性检查（开发阶段）

3. **内核 panic 处理**
   - EL1 同步异常（Data/Instruction Abort）= 内核 bug → panic
   - panic 时：打印完整诊断（EC/ESR/FAR/ELR/SPSR）+ 死循环
   - **禁止 panic 后继续执行**（可能产生不可预测行为）

4. **Watchdog Timer**
   - 内核应配置硬件看门狗
   - 正常运行时定期喂狗
   - 死锁/死循环 → 看门狗触发系统重启

### 故障恢复

1. **用户态故障恢复**
   - EL0 异常（缺页/非法指令）→ 终止出错线程，内核和其它线程继续运行
   - 线程终止时释放资源（栈、页表、能力）

2. **内核态故障处理**
   - 内核异常 = 不可恢复 → panic + 停机
   - **禁止在内核异常后"跳过指令"继续执行**

3. **资源泄漏容忍**
   - 线程异常终止时，必须释放其所有资源
   - 物理页、能力、IPC 端点、地址空间必须正确回收

### 安全降级

- 硬件故障（ECC 错误、总线错误）→ 隔离故障 CPU + 报告
- 内存不足 → 拒绝新的分配请求（返回 ENOMEM），不 crash
- IPC 端点耗尽 → 返回 ENOMEM，不 crash

## 强制开发规则：并发与锁正确性（Concurrency & Locking）

**多核并发是内核最难正确的部分。所有共享数据的访问必须有明确的同步策略。**

### 锁设计原则

1. **锁粒度**：尽量用 per-CPU 数据（无需锁），其次细粒度锁，最后全局锁
2. **锁类型选择**：
   - per-CPU 队列操作：`ticket_lock_acquire_irqsave`（关中断 + 自旋锁）
   - 短临界区（<10 条指令）：`ticket_lock_acquire`（仅自旋锁）
   - 长临界区或可能阻塞：不应用自旋锁（需要信号量/互斥锁）
3. **持锁时间**：自旋锁持锁时间必须极短（<1μs），禁止在持锁时调用可能阻塞或耗时的函数

### 死锁预防

1. **锁排序**：全局定义锁的获取顺序，所有代码必须按序获取
   ```
   锁优先级顺序（从高到低）：
   1. per-CPU 就绪队列锁（cpu_q->lock）
   2. CSpace 锁（cspace_t.lock）
   3. 端点锁（ipc_endpoint_t.lock）
   4. 定时器队列锁（s_timer_locks）
   5. 睡眠队列锁（s_sleep_locks）
   6. kmalloc 锁（s_kmalloc_state.lock）
   ```
   - 获取多个锁时，必须按上述顺序
   - 跨 CSpace 操作（如 cap_copy）按 CSpace 地址序获取（双锁按地址序）

2. **禁止锁升级**：持锁时禁止获取同优先级或更高优先级的锁（除非按排序规则）

3. **优先级继承**：互斥锁（mutex）必须实现优先级继承协议，防止优先级反转

### 禁止事项

- ❌ **禁止持锁时 sleep/schedule**：自旋锁持锁时禁止调用 `schedule()`/`kthread_sleep()`
- ❌ **禁止持锁时 kmalloc**：分配器有自己的锁，可能导致递归锁或死锁
- ❌ **禁止持锁时 context_switch**：切换栈后锁可能永远无法释放
- ❌ **禁止裸 `barrier()` 代替锁**：`barrier()` 不提供 SMP 可见性保证，必须用锁

### 验证

- 中断上下文中获取的锁必须用 `irqsave` 版本
- `schedule()` 内的队列操作必须在锁内完成，锁在 `context_switch` 前释放
- 每个新共享数据结构必须有明确的锁策略文档

## 强制开发规则：内存安全（Memory Safety）

**内存安全漏洞是内核最高危的缺陷。所有内存操作必须有边界检查。**

### 边界检查

1. **所有用户态指针必须通过 `access_ok` 验证**（已实现）
2. **所有数组访问必须有索引检查**：`if (idx >= ARRAY_SIZE) return -EINVAL;`
3. **所有 memcpy/memset 的长度必须验证**：禁止用户态控制的 `size` 直接传入
4. **字符串操作必须有终止符保证**：`strncpy_from_user` 强制终止符

### 生命周期安全

1. **禁止 use-after-free**：释放后的指针应置 NULL（`kfree_secure` 清零后释放）
2. **禁止 double-free**：`kfree` 检查 `is_free` 标志（已实现）
3. **能力/端点 generation 防护**：已释放对象通过 generation 号检测 use-after-free
4. **RCU 或 epoch 回收**：对热路径中的对象回收，考虑延迟释放（当前用 generation 即可）

### 栈安全

1. **每个线程有独立栈**（kthread_create 分配）
2. **栈底有金丝雀**（已实现 stack_guard）
3. **栈大小有界**：最小 4KB，默认 8KB，最大 64KB
4. **禁止变长数组（VLA）**：MISRA C:2012 禁止，栈消耗不可预测
5. **禁止递归**：所有函数必须可转换为迭代（MISRA C:2012 Rule 17.2）

### 禁止事项

- ❌ **禁止 `strcpy`/`strcat`**：用 `strncpy`/`strncat`（有界版本）
- ❌ **禁止 `sprintf`**：用 `snprintf`（有界版本）
- ❌ **禁止 `gets`**：无任何场景可用
- ❌ **禁止裸指针算术跨边界**：所有指针 + offset 必须验证不越界

## 强制开发规则：多核性能设计（SMP Performance）

**多核系统的核心性能原则：per-CPU 数据优先，尽量无锁，消除 cache 行伪共享。**
**锁是最后的手段，不是第一选择。**

### 设计优先级（从优到劣）

1. **✅ per-CPU 独占数据（最优）**：每个 CPU 有自己的副本，完全无锁
2. **✅ per-CPU freelists + 慢速全局回退**：快路径无锁，仅回退路径加锁
3. **✅ Read-Copy-Update (RCU) / Epoch 回收**：读路径无锁，写路径复制
4. **✅ 细粒度自旋锁（per-object）**：每个对象独立锁，减少竞争面
5. **⚠️ 全局自旋锁（最后手段）**：仅用于极冷路径（初始化/配置）
6. **❌ 全局互斥锁（禁止在内核使用）**：睡眠锁破坏实时性

### per-CPU 数据原则

1. **热点数据必须 per-CPU 化**：
   - 就绪队列（已实现：`g_scheduler.cpu_queues[cpu_id]`）
   - 内存分配器 freelist（**kmalloc 应改造**：per-CPU freelist + 全局中央堆）
   - 定时器队列（已实现：`s_timer_queues[cpu_id]`）
   - 睡眠队列（已实现：`s_sleep_queues[cpu_id]`）
   - IPC 消息缓冲池（应改造：per-CPU 缓存）
   - 中断统计计数器（应改造：per-CPU 计数）

2. **per-CPU 数据本核访问无需锁**：
   - CPU X 访问自己的 `cpu_queues[X]` 不需要自旋锁
   - 只有**跨核访问**（负载均衡/工作窃取/迁移）才需要锁
   - 当前代码的 per-CPU 就绪队列锁是为迁移设计的——本核快路径不应持锁

3. **per-CPU 数据访问宏**：
   ```c
   /* 正确模式：本核访问无需锁 */
   PerCPUReadyQueue_t *my_q = &g_scheduler.cpu_queues[hal_get_cpu_id()];
   /* 本核 enqueue/dequeue 无需锁（只有远程迁移时加锁） */
   ```

### 无锁设计模式

1. **单生产者单消费者（SPSC）环形缓冲**：IC2 通道已实现（`kernel/ipc/ic2.c`）
   - 读指针仅消费者写，写指针仅生产者写
   - `LOAD-ACQUIRE` / `STORE-RELEASE` 保证可见性（已用 `hal_dmb_ish`）

2. **原子计数器替代锁**：统计/引用计数用 `atomic_inc_u64` / `atomic_dec_u64`
   - 如 `s_system_ticks` 已用原子递增（多核安全）

3. **generation 号防 ABA**：端点/能力的 generation 已实现
   - 无锁回收需要 generation（避免 ABA 问题）

### Cache 行对齐（消除伪共享）

**所有 per-CPU 数据结构必须 cache 行对齐（64 字节），防止伪共享（false sharing）。**

1. **per-CPU 数组对齐**：
   ```c
   /* 正确：CACHE_ALIGN 确保每个 CPU 的副本在不同 cache 行 */
   static PerCPUReadyQueue_t CACHE_ALIGN(64) g_cpu_queues[CONFIG_MAX_CPUS];
   /* 错误：未对齐，相邻 CPU 数据在同一 cache 行 → 伪共享 */
   static PerCPUReadyQueue_t g_cpu_queues[CONFIG_MAX_CPUS];
   ```

2. **必须对齐的 per-CPU 数据**：
   - 就绪队列（已对齐：`CACHE_ALIGN(64)`）
   - percpu 数据（`s_percpu_data[cpu_id]` 应对齐）
   - 定时器队列（`s_timer_queues[cpu_id]` 应对齐）
   - kmalloc per-CPU freelist（改造后须对齐）

3. **热/冷字段分离**：频繁修改的字段与只读字段分到不同 cache 行
   ```c
   typedef struct {
       /* 热字段（频繁修改）—— 独占 cache 行 */
       volatile uint64_t nr_running CACHE_ALIGN(64);
       volatile uint64_t ticks;
       /* 冷字段（少修改）—— 可共享 cache 行 */
       KThread_t *idle_thread;
       uint32_t cpu_id;
   } PerCPUData_t;
   ```

### 锁的最低必要原则

1. **本核快路径无锁**：调度 enqueue/dequeue（本核）不应持锁
   - 仅在**远程迁移路径**加锁（迁移是慢路径，可接受锁开销）

2. **锁只保护真正的跨核共享**：
   - 端点锁：保护 send/receive 跨核交互（正确）
   - kmalloc 锁：**不应保护全局 freelist**，应改为 per-CPU freelist
   - CSpace 锁：保护能力表的跨核访问（正确，但高频 CSpace 应考虑 per-CPU 缓存）

3. **锁粒度递进**：
   - 先用 per-CPU 避免竞争
   - 竞争仍严重时用 per-CPU 缓存 + 慢速回退
   - 最后才用全局锁

### kmalloc 多核性能改造方向（当前问题）

当前 kmalloc 用全局锁（`s_kmalloc_state.lock`），所有 CPU 的 kmalloc/kfree 都竞争同一锁。

**正确改造（slub 风格）**：
```
per-CPU freelist（快路径，无锁）
    ↓ 空闲时
per-NUMA-node partial list（中速路径，细粒度锁）
    ↓ 仍空闲时
全局中央堆（慢路径，全局锁）
```

这是 Linux SLUB 分配器的设计，确保 99% 的分配在 per-CPU freelist 完成（无锁）。

### 验证指标

- **锁竞争次数**：通过锁等待计数器统计（应趋近于 0）
- **per-CPU 数据命中率**：快路径无锁命中率应 > 99%
- **cache 行伪共享检测**：使用 `perf c2c` 或 QEMU cache 模拟

### 禁止事项

- ❌ **禁止 per-CPU 数据本核访问加锁**：本核操作自己的队列/freelist 不需要锁
- ❌ **禁止热点路径用全局锁**：kmalloc/scheduler/IPC 快路径禁止全局锁
- ❌ **禁止 per-CPU 数组不对齐**：必须 `CACHE_ALIGN(64)`
- ❌ **禁止频繁修改的字段与只读字段共享 cache 行**
- ❌ **禁止本核队列操作关中断**（除非有远程迁移并发可能）

## 强制开发规则：错误码与返回值规范（Error Codes）

**内核 API 必须使用统一的错误码体系，禁止随意返回负数。**

### 错误码使用规则

1. **所有可能失败的函数返回 `kernel_status_t`（= `int32_t`）**：
   - 成功返回 `KERNEL_OK`（= 0）
   - 失败返回 `-(int32_t)EXXX`（负数 errno）

2. **标准 errno 使用**（见 `include/kernel/errno.h`）：
   | errno | 值 | 使用场景 |
   |-------|-----|---------|
   | EINVAL | 22 | 参数无效 |
   | ENOMEM | 12 | 内存不足 |
   | EACCES | 13 | 权限不足（能力验证失败） |
   | EFAULT | 14 | 用户指针非法（access_ok 失败） |
   | ENOENT | 2 | 对象不存在 |
   | ESRCH | 3 | 线程不存在 |
   | EBUSY | 16 | 资源忙 |
   | ENOSYS | 38 | 功能未实现 |
   | ETIME | 62 | 超时 |

3. **系统调用返回值**：`frame->x0` 存放返回值
   - 成功：`0`
   - 失败：`-(int64_t)errno`（用户态看到负数）

### 禁止事项

- ❌ 禁止返回未定义的魔法负数（如 `return -1;` 必须用 `return -(int32_t)EINVAL;`）
- ❌ 禁止混用 `KERNEL_OK` 和 `0`（用 `KERNEL_OK` 更明确）
- ❌ 禁止忽略错误返回值（MISRA Rule 17.7：返回值必须使用）

## 强制开发规则：命名规范（Naming Convention）

**统一的命名规范确保代码可读性和一致性。**

### 命名规则

| 类别 | 风格 | 示例 |
|------|------|------|
| 函数 | `snake_case` | `scheduler_pick_next()` |
| 局部变量 | `snake_case` | `highest_prio` |
| 全局变量 | `snake_case` + `s_` 前缀 | `s_kmalloc_state` |
| 宏 | `UPPER_SNAKE_CASE` | `PAGE_SIZE_4K` |
| typedef | `PascalCase_t` | `KThread_t`, `TicketLock_t` |
| 枚举值 | `UPPER_SNAKE_CASE` + 模块前缀 | `KTHREAD_STATE_READY` |
| 结构体标签 | `PascalCase`（不直接用，通过 typedef） | `struct KThread` |

### 前缀约定

| 前缀 | 用途 | 示例 |
|------|------|------|
| `s_` | 文件作用域静态变量 | `s_endpoints[]` |
| `g_` | 全局变量（非 static） | `g_scheduler` |
| `k_` | 常量（非宏） | `k_max_threads` |
| `hal_` | HAL 接口函数 | `hal_get_cpu_id()` |
| `paddr` | 物理地址变量 | `paddr_t page_pa` |
| `vaddr` | 虚拟地址变量 | `vaddr_t user_sp` |

### 禁止事项

- ❌ 禁止 camelCase（如 `highestPrio` → 用 `highest_prio`）
- ❌ 禁止匈牙利记号（如 `dwCount` → 用 `count`）
- ❌ 禁止缩写不明确（如 `sz` → 用 `size`，`ptr` 可接受）

## 强制开发规则：日志与诊断规范（Logging）

**内核日志必须有级别、格式统一、可配置。禁止无序的 UART 打印。**

### 日志级别

| 级别 | 宏（待实现） | 用途 | 示例 |
|------|-------------|------|------|
| ERROR | `KLOG_ERROR` | 不可恢复错误 | panic 前诊断 |
| WARN | `KLOG_WARN` | 可恢复异常 | 栈溢出检测、资源耗尽警告 |
| INFO | `KLOG_INFO` | 启动/状态信息 | `[k] MMU ok` |
| DEBUG | `KLOG_DEBUG` | 开发调试 | 页表条目、调度决策 |

### 当前阶段的诊断输出规范

在日志系统正式实现前，遵循以下规则：

1. **启动信息**前缀 `[k]`：`hal_uart_puts(QEMU_UART0_BASE, "[k] MMU ok\n")`
2. **异常诊断**前缀 `[exception]`：
3. **IPC/调度诊断**前缀 `[ipc]`/`[sched]`：
4. **生产模式（CONFIG_DEBUG=0）**：只保留 ERROR + 启动 INFO
5. **禁止在生产路径放 DEBUG 级打印**（影响实时性 + 增大 text）

### 禁止事项

- ❌ **禁止在中断处理中调用任何 klog 函数**（klog_error/warn/info/debug/putc/flush）
  中断处理只做最短路径工作，日志延迟到线程上下文（idle 线程 flush）输出。
  唯一例外：klog_panic（panic 路径直接同步输出，绕过缓冲）。
- ❌ 禁止在实时路径（调度/IPC 快路径）放同步 UART 打印
- ❌ 禁止硬编码 UART 地址（用 hal_console_* 接口，HAL 层绑定硬件）
- ❌ 禁止在循环中打印（会导致缓冲溢出和输出泛滥）
- ❌ 禁止多个 CPU 同时直接写 UART（klog_flush 已通过全局锁串行化）

## 强制开发规则：硬件资源映射管理（Hardware Resource Map）

**所有硬件资源（MMIO 地址、IRQ 号）必须有单一真源（Single Source of Truth）。**

### 单一真源原则

所有硬件地址/IRQ 定义集中在 HAL 层头文件，禁止散布在驱动代码中：

| 资源 | 定义位置 | 示例 |
|------|---------|------|
| UART 基址 | `hal.h` | `QEMU_UART0_BASE` |
| GIC 基址 | `gic.c` 内部宏 | `GICD_BASE_ADDR` |
| virtio MMIO 基址 | `hal.h`（应集中） | 待统一 |
| 定时器 IRQ | `hal.h`（应集中） | `QEMU_TIMER_IRQ` |
| IPC 中断 | `hal.h`（应集中） | — |

### 虚拟地址映射规范（TTBR1 高地址）

迁移后所有 MMIO 通过 TTBR1 高地址访问：

```
物理地址 → 虚拟地址（linear mapping，见 virt_phys.h）
0x09000000 (UART) → 0xFFFF000000900000
0x08000000 (GIC)  → 0xFFFF000000800000
0x0A000000 (virtio) → 0xFFFF000000A00000
```

### 禁止事项

- ❌ 禁止在驱动/服务代码中硬编码物理地址
- ❌ 禁止重复定义同一硬件地址（如 `0x09000000` 出现在多个文件）
- ❌ 禁止使用 `__pa(0x09000000)` 手动计算（用 HAL 宏）

## 注意事项

- 操作系统开发需要特别关注安全性和稳定性
- 代码修改前先备份
- 重要变更需要记录到 MEMORY.md
- 保持代码风格一致性
- 所有内核代码必须 MISRA C:2012 合规
- 内核代码段控制在 **50KB** 以内（text section，软目标 40KB，当前约 41KB）
