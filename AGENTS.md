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

### 需要补充的 HAL 接口

以下接口需要从内核核心代码中抽离到 HAL 层：

```c
/* 定时器 - 从 timer.c 抽离 */
uint64_t hal_timer_get_count(void);        /* 替代 mrs cntpct_el0 */
uint64_t hal_timer_get_freq(void);         /* 替代 mrs cntfrq_el0 */
uint64_t hal_timer_get_control(void);      /* 替代 mrs cntp_ctl_el0 */
void hal_timer_set_compare(uint64_t val);  /* 替代 msr cntp_cval_el0 + isb */
void hal_timer_set_control(uint64_t val);  /* 替代 msr cntp_ctl_el0 + isb */

/* 内存屏障 - 从 ic2.c 抽离 */
void hal_dmb_ish(void);       /* 数据内存屏障: Inner Shareable */
void hal_dmb_ishst(void);     /* 数据内存屏障: Inner Shareable, Store */
void hal_dmb_ishld(void);     /* 数据内存屏障: Inner Shareable, Load */

/* 页表 - 从 page_table.c 抽离 */
uint64_t hal_read_ttbr0(void);          /* 替代 mrs ttbr0_el1 */
uint64_t hal_read_ttbr1(void);          /* 替代 mrs ttbr1_el1 */
void hal_write_ttbr0(uint64_t val);     /* 替代 msr ttbr0_el1 + isb */
void hal_write_ttbr1(uint64_t val);     /* 替代 msr ttbr1_el1 + isb */
void hal_tlb_invalidate_asid(uint64_t asid);  /* 替代 tlbi aside1is */
void hal_tlb_invalidate_all(void);      /* 替代 tlbi vmalle1is */

/* 低功耗等待 - 从 scheduler.c / thread.c 抽离 */
void hal_wfe(void);  /* 替代 wfe 指令 */

/* 地址空间切换 - 从 vmspace.c 抽离 */
void hal_set_asid(uint16_t asid);  /* 替代直接操作 TTBR 寄存器 */
```

### 当前违规清单（待修复）

| 文件 | 违规内容 | 需要的 HAL 接口 |
|------|---------|----------------|
| `kernel/sched/timer.c` | `mrs cntpct_el0`, `cntfrq_el0`, `cntp_ctl_el0`, `msr cntp_cval_el0` | `hal_timer_*` |
| `kernel/sched/scheduler.c` | `wfe` (5处) | `hal_wfe()` |
| `kernel/sched/thread.c` | `wfe` (2处) | `hal_wfe()` |
| `kernel/ipc/ic2.c` | `dmb ish/ishst/ishld` (3处) | `hal_dmb_*` |
| `kernel/mm/page_table.c` | `mrs/msr ttbr0_el1`, `ttbr1_el1`, `tlbi` (9处) | `hal_read/write_ttbr*`, `hal_tlb_*` |
| `kernel/mm/vmspace.c` | `msr ttbr0_el1`, `isb` (3行) | `hal_write_ttbr0` |

### 代码审查规则

**每次代码审查必须检查以下项：**

1. **体系架构独立性**: `kernel/` 非 `arch/` 目录下禁止出现 `__asm__`、`msr`、`mrs`、`isb`、`dsb`、`dmb`、`tlbi`、`wfe`、`wfi` 等体系架构相关代码
2. **HAL 接口使用**: 所有硬件操作必须通过 `hal.h` 中定义的接口
3. **MISRA C:2012 合规**: Rule 1.1 (未使用代码)、Rule 8.13 (pointer should be const)、Dir 4.9 (结构体/联合体应有 typedef)
4. **text < 30KB**: 每次提交后检查 `aarch64-linux-gnu-size build/kernel/aisafe64.elf.elf`
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

## 强制开发规则：用户态 C 库（标准 musl + AISafeOS64 适配层）

**所有用户态服务必须通过 C 库进行系统调用，禁止直接使用底层 syscall 桩函数。**

### 设计原则（参考 seL4/musllibc 方案）

**❌ 错误做法**: 自己手写 musl 子集 — 不标准、不可维护、无法通过安全认证
**✅ 正确做法**: 使用标准 musl 上游代码 + 最小适配层（arch + syscall shim）

seL4 项目的 musllibc 适配证明了此方案的可行性：
- 保留标准 musl 源码不动（MIT 许可证，安全认证友好）
- 只创建 `arch/<arch>_aisafe/` 适配目录（约 6 个文件）
- 核心适配文件 `syscall_arch.h` 通过 `__sysinfo` 函数指针路由所有系统调用
- 实现 `__sysinfo` 分发器：Linux syscall 号 → AISafeOS64 SVC/IPC 翻译

### 架构

```
┌──────────────────────────────────────────────────────┐
│  用户态服务 (fs/proc/mem/net/security/vmm/...)       │
│    #include <stdio.h>  #include <string.h>  ...       │
├──────────────────────────────────────────────────────┤
│  标准 musl libc (upstream, 不修改源码)               │
│    string / stdio / stdlib / time / signal / math ... │
│    完整 POSIX C 库功能                                │
├──────────────────────────────────────────────────────┤
│  AISafeOS64 musl 适配层 (lib/musl_aisafe/)           │
│    ├── arch/aarch64_aisafe/syscall_arch.h  ← 核心适配  │
│    │     __syscall*() → __sysinfo 函数指针路由        │
│    ├── arch/aarch64_aisafe/atomic_arch.h              │
│    ├── arch/aarch64_aisafe/crt_arch.h                 │
│    ├── arch/aarch64_aisafe/pthread_arch.h             │
│    ├── src/syscall_dispatch.c  __sysinfo 分发器        │
│    │     Linux syscall 号 → AISafeOS64 SVC 翻译       │
│    └── src/musl_safety.c  功能安全改造包装              │
│         参数验证 / 确定性 / 审计日志 / MISRA 包装       │
├──────────────────────────────────────────────────────┤
│  内核 (AISafeOS64 SVC 系统调用分发)                   │
└──────────────────────────────────────────────────────┘
```

### 目录结构

```
lib/
├── musl_upstream/            # 标准 musl 1.2.x (git submodule 或 vendor)
│   ├── src/                  # musl 标准实现（不修改）
│   ├── include/              # musl 标准头文件（不修改）
│   ├── arch/aarch64/         # 标准 ARM64 支持（不修改）
│   └── ...                   # musl 完整源码树
│
├── musl_aisafe/              # AISafeOS64 musl 适配层（我们写的）
│   ├── arch/aarch64_aisafe/  # musl 架构适配（覆盖 musl arch/）
│   │   ├── syscall_arch.h    # ← 核心: __syscall* → __sysinfo 路由
│   │   ├── atomic_arch.h     # 原子操作适配
│   │   ├── crt_arch.h        # 启动代码适配
│   │   ├── pthread_arch.h    # 线程适配
│   │   ├── reloc.h           # 重定位支持
│   │   └── bits/             # 类型定义
│   ├── src/                  # 适配实现
│   │   ├── syscall_dispatch.c   # __sysinfo 分发器
│   │   │     Linux syscall → AISafeOS64 SVC 翻译表
│   │   ├── musl_safety.c        # 功能安全改造包装
│   │   │     参数验证 / 确定性 / 审计 / MISRA 包装
│   │   ├── tls.c                # Thread-local storage 适配
│   │   └── boot.c               # 启动初始化
│   └── CMakeLists.txt        # 构建配置
│
├── musl_legacy/              # 旧手写代码（仅保留参考，逐步废弃）
│   └── (现有 lib/musl/ 内容移入此处作为过渡)
│
├── libkernel/                # 内核系统调用桩（保持不变）
│   └── syscall.c
└── kernel_string.c           # 内核字符串库（保持不变）
```

### syscall_arch.h 核心机制

参考 seL4 的做法，musl 的所有系统调用通过 `__sysinfo` 函数指针分发：

```c
/* arch/aarch64_aisafe/syscall_arch.h */
#define __SYSCALL_LL_E(x) (x)
#define __SYSCALL_LL_O(x) (x)

extern unsigned long __sysinfo;

/* 所有 __syscall* 通过 __sysinfo 函数指针路由 */
#define CALL_SYSINFO(n, ...) ((long(*)(long,...))__sysinfo)(n, ##__VA_ARGS__)

static inline long __syscall0(long n) { return CALL_SYSINFO(n); }
static inline long __syscall1(long n, long a1) { return CALL_SYSINFO(n, a1); }
// ... __syscall2 ~ __syscall6
```

```c
/* src/syscall_dispatch.c */

/* Linux syscall 号 → AISafeOS64 SVC 翻译表 */
static long aisafe_syscall_dispatch(long nr, ...)
{
    switch (nr) {
        case SYS_linux_read:     → SYS_MSG_SEND + IPC → FS 服务
        case SYS_linux_write:    → SYS_DEBUG_PRINT 或 IPC → FS 服务
        case SYS_linux_exit:     → SYS_THREAD_EXIT
        case SYS_linux_getpid:   → SYS_THREAD_GET_ID
        case SYS_linux_mmap:     → SYS_VM_MAP
        case SYS_linux_munmap:   → SYS_VM_UNMAP
        case SYS_linux_ioctl:    → IPC → 驱动服务
        // ... 完整映射表
        default:                 → return -ENOSYS;
    }
}

/* 设置 __sysinfo 函数指针（在 C 库初始化时调用） */
unsigned long __sysinfo = (unsigned long)aisafe_syscall_dispatch;
```

### 功能安全改造（musl_safety.c）

标准 musl 不满足 MISRA C:2012 / ISO 26262 要求，通过包装层改造：

1. **参数验证**: 每个 syscall 路径添加参数边界检查
2. **确定性行为**: 替换 musl 中不确定行为（malloc 使用确定性分配器）
3. **错误路径覆盖**: 补齐 musl 未覆盖的边界条件
4. **审计日志**: 关键 syscall 路径添加安全审计点
5. **MISRA 包装头**: 对 musl 公共 API 提供符合 MISRA 的薄包装
6. **递归消除**: musl 内部使用递归的地方（如 regex），替换为迭代

### 开发阶段

**Phase 0: musl 上游集成（当前阶段）**
- [ ] 获取 musl 1.2.x 源码（git submodule `lib/musl_upstream/`）
- [ ] 创建 `lib/musl_aisafe/arch/aarch64_aisafe/` 适配目录（6 个文件）
- [ ] 实现 `syscall_arch.h` — __sysinfo 函数指针路由
- [ ] 实现 `syscall_dispatch.c` — Linux→AISafeOS64 syscall 号翻译
- [ ] 实现 `crt_arch.h` — 启动代码适配
- [ ] 交叉编译 musl 静态库 (`libmusl.a`)
- [ ] 验证: 简单 hello world 程序通过 musl + SVC 在 QEMU 中运行

**Phase 1: 核心 POSIX 接口验证**
- [ ] string/stdio/stdlib 通过 musl 标准实现可用
- [ ] unistd/fcntl/stat 通过 syscall 分发器工作
- [ ] errno/thread-local 正确工作
- [ ] 宿主机单元测试通过（已有的 247 个测试迁移到 musl upstream）

**Phase 2: 功能安全改造**
- [ ] musl_safety.c 参数验证包装
- [ ] 确定性内存分配器替换 musl malloc
- [ ] 审计日志集成
- [ ] MISRA 包装头文件
- [ ] 递归消除

**Phase 3: 服务迁移**
- [ ] 所有用户态服务从旧 `lib/musl/` 迁移到标准 musl
- [ ] 移除 `lib/musl_legacy/`
- [ ] 完整 POSIX 子集验证

**Phase 4: 安全认证**
- [ ] musl 适配层 MISRA C:2012 合规
- [ ] musl 本身安全认证偏差记录
- [ ] ISO 26262 ASIL-D 软件组件认证

### 旧手写代码处理（lib/musl_legacy/）

现有的 `lib/musl/` 手写代码（Phase 1+2, ~3,786 行, 247 测试全通过）有价值：
- **保留为参考**: 移入 `lib/musl_legacy/`，标记为 deprecated
- **测试可复用**: 宿主机测试用例继续使用，验证 musl upstream 行为一致
- **适配层参考**: syscall 号映射逻辑可参考旧 `aisafe/syscall.h`
- **逐步废弃**: Phase 3 完成后可删除

### 给 Claude Code 的 Prompt 模板

```
开发 AISafeOS64 musl 适配层 <模块名>：

## 背景
AISafeOS64 是 64 位微内核 RTOS，采用标准 musl libc + 最小适配层方案（参考 seL4/musllibc）。

## 架构
- 标准 musl 源码在 lib/musl_upstream/（不修改）
- 适配层在 lib/musl_aisafe/（我们写的）
- 核心: syscall_arch.h 通过 __sysinfo 函数指针路由
- 分发器: syscall_dispatch.c 做 Linux syscall → AISafeOS64 SVC 翻译

## 系统调用映射
- AISafeOS64 调用号定义在 include/kernel/syscall.h
- Linux 标准调用号在 musl include/bits/syscall.h
- 映射策略:
  - 直接映射: getpid→SYS_THREAD_GET_ID, _exit→SYS_THREAD_EXIT
  - IPC 路由: read/write/open→IPC→FS 服务
  - ENOSYS 桩: 暂未实现的功能

## 任务
<具体任务描述>

## 代码规范
- 适配层: MISRA C:2012, 4空格缩进, Allman括号, 中文注释
- 不修改 musl upstream 源码
- 所有适配通过 arch/ 和 src/ 覆盖机制实现
```

### 用户态服务迁移规则

**新代码**：使用标准 musl 头文件（`#include <stdio.h>` 等），编译时链接 musl 静态库。

**现有代码**：Phase 3 统一迁移。迁移期间旧代码可继续使用 `lib/musl_legacy/`。

## 注意事项

- 操作系统开发需要特别关注安全性和稳定性
- 代码修改前先备份
- 重要变更需要记录到 MEMORY.md
- 保持代码风格一致性
- 所有内核代码必须 MISRA C:2012 合规
- 内核代码段控制在 **40KB** 以内（text section）
