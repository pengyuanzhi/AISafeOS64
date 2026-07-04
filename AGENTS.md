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

## 注意事项

- 操作系统开发需要特别关注安全性和稳定性
- 代码修改前先备份
- 重要变更需要记录到 MEMORY.md
- 保持代码风格一致性
- 所有内核代码必须 MISRA C:2012 合规
- 内核代码段控制在 **50KB** 以内（text section，软目标 40KB，当前约 41KB）
