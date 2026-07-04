# CLAUDE.md - AISafeOS64 内核项目 Claude Code 规范

**版本**: 5.0（内核专用）
**最后更新**: 2026-07-04
**状态**: 🔴 激活 - 强制执行

> 本文件是 Claude Code 在本项目的操作规范。
> 所有强制开发规则见 `AGENTS.md`（11 条规则）。

---

## 1. 项目结构

```
AISafeOS64/
├── kernel/                 # 内核源码
│   ├── arch/arm64/         # ARM64 架构实现（boot.S, mmu.c, hal.c, gic.c ...）
│   ├── sched/              # 调度器（scheduler.c, thread.c, timer.c, edf.c, smp.c）
│   ├── ipc/                # IPC（endpoint.c, channel.c, ic2.c, notification.c）
│   ├── cap/                # 能力系统（cspace.c, capability.c）
│   ├── mm/                 # 内存管理（page_table.c, vmspace.c, kmalloc.c, phys_mem.c）
│   ├── irq/                # 中断/系统调用（syscall_dispatch.c）
│   ├── driver/             # 内核驱动（driver_core.c, drv_uart.c）
│   └── verify/             # 形式化验证
├── include/                # 公共头文件
│   └── kernel/             # 内核 API 头文件
├── services/               # 用户态服务
├── tests/                  # 单元测试
├── lds/                    # 链接脚本（kernel.ld）
├── cmake/                  # CMake 工具链配置
├── scripts/                # 构建/测试/检查脚本
├── docs/                   # 文档
├── memory/                 # AI 记忆文件
├── AGENTS.md               # 强制开发规则（11 条规则）
└── CLAUDE.md               # 本文件
```

### 目录约束

- **根目录**：仅放配置文件（CMakeLists.txt, .gitignore, AGENTS.md, CLAUDE.md）
- **kernel/ 非 arch/ 目录**：禁止内联汇编/寄存器操作（必须通过 HAL，见 AGENTS.md 架构分层规则）
- **scripts/**：所有脚本（kebab-case 命名）
- **docs/**：文档按 requirements/design/plans/logs/archive 分类

---

## 2. 构建与验证

### 构建命令

```bash
# 配置（首次或 CMakeLists 改动后）
cd build && cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm64.cmake

# 编译
cd build && make -j4

# 验证内核映像大小（必须 < 50KB）
aarch64-linux-gnu-size build/kernel/aisafe64.elf.elf

# QEMU 单核验证
qemu-system-aarch64 -M virt -cpu cortex-a57 -nographic \
  -kernel build/kernel/aisafe64.elf.elf -m 512M -smp 1

# QEMU 多核验证
qemu-system-aarch64 -M virt -cpu cortex-a57 -nographic \
  -kernel build/kernel/aisafe64.elf.elf -m 512M -smp 4

# 宿主机测试
cd build && ctest --output-on-failure
```

### 验证门控（每次提交前）

- [ ] `make -j4` 零错误零警告
- [ ] `aarch64-linux-gnu-size` text < 50KB
- [ ] QEMU 单核引导到 `[k] Start sched`
- [ ] 无 `[exception]` panic 输出

---

## 3. Commit 规范

### 格式

```
<type>(<scope>): <中文描述>

## 核心变更 / 验证结果 / 架构特点（可选）
```

### type

| type | 用途 |
|------|------|
| `feat` | 新功能 |
| `fix` | Bug 修复 |
| `refactor` | 重构（不改变行为） |
| `docs` | 文档 |
| `test` | 测试 |
| `chore` | 构建/工具 |

### scope

模块名：`mm`, `sched`, `ipc`, `cap`, `arch`, `irq`, `driver`, `kernel`

---

## 4. 代码规范速查

| 项目 | 规范 |
|------|------|
| 语言 | C11（freestanding），MISRA C:2012 |
| 缩进 | 4 空格 |
| 括号 | Allman 风格 |
| 命名 | snake_case（函数/变量），UPPER_SNAKE（宏），PascalCase（typedef） |
| 注释 | 中文 Doxygen |
| 行宽 | ≤120 字符 |
| 圈复杂度 | ≤10 |
| 递归 | ❌ 禁止（MISRA Rule 17.2） |
| VLA | ❌ 禁止（MISRA Rule 18.8） |
| 内联汇编 | 仅在 kernel/arch/ 目录 |

---

## 5. 与 AGENTS.md 的关系

`AGENTS.md` 是最高优先级的强制规则文件（11 条规则）：
1. 必须使用 Claude Code 编码
2. TDD
3. 自动提交
4. 架构分层（HAL）
5. 禁止折中方案
6. 实时性保证
7. 确定性约束
8. 可靠性与容错
9. 并发与锁正确性
10. 内存安全
11. 多核性能设计

**当本文件与 AGENTS.md 冲突时，以 AGENTS.md 为准。**

---

## 6. 禁止事项（速查）

- ❌ 在根目录创建临时文件
- ❌ 提交 build/ 产物
- ❌ 手动逐行编辑 >20 行代码（用 Claude Code）
- ❌ kernel/ 非 arch/ 目录使用内联汇编
- ❌ demand paging / 内存换出
- ❌ 全局锁在热点路径
- ❌ per-CPU 数据不加 cache 行对齐
- ❌ 递归 / VLA / strcpy / sprintf

---

**维护者**: AISafe64 Team
**版本**: 5.0
