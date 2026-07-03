# 2026-07-03 SMP 多核启动诊断 ✅ (已完成诊断，修复待续)

## 完成工作

### P1-1: QEMU 端到端启动验证

#### 单核启动：完全成功 ✅

内核在 QEMU `-smp 1` 下完整引导，初始化序列全部通过：
```
✅ MMU 初始化（双地址空间 TTBR0/TTBR1）
✅ GIC 中断控制器初始化
✅ Timer 初始化
✅ 调度器初始化
✅ SMP 初始化
✅ 驱动框架初始化
✅ 启动调度器 → 进入 idle 循环（稳定运行）
```

#### 多核启动（-smp 4）：从核崩溃 ❌

**现象**：主核正常，三个从核（CPU 1/2/3）被 PSCI 成功唤醒后崩溃。
- 异常类型：`Instruction Abort (EL1)`，EC=0x21，ISS=0x0D（Translation fault level 1）
- FAR=0x0 递增（0→4→8→c），从核在地址 0（非 RAM 区域）取指

### 诊断过程（逐步定位崩溃点）

通过在 boot.S 和 smp_boot.c 中插入 UART 标记（A-M），精确定位从核执行路径：

| 标记 | 位置 | 结果 |
|------|------|------|
| A-D | boot.S secondary_entry（禁中断→读mpidr→设vbar→设栈） | ✅ 全部到达 |
| E-F | smp_secondary_entry 入口（参数检查） | ✅ 全部到达 |
| G | mmu_init_secondary()（启用从核 MMU） | ✅ 全部到达 |
| H | gic_init_secondary()（GIC CPU interface） | ✅ 全部到达 |
| I | 从核定时器初始化 | ✅ 全部到达 |
| J | interrupt_subsys_init() | ✅ 全部到达 |
| K | percpu 数据初始化 + s_secondary_ready=1 | ✅ 全部到达 |
| L | hal_irq_enable()（使能中断） | ✅ 全部到达 |
| M | scheduler_start_secondary() 入口 | ✅ 全部到达 |
| — | scheduler_start_secondary() 内部 | ❌ 崩溃（PC=0） |

**结论**：PSCI 唤醒、从核早期初始化（MMU/GIC/定时器/中断）**全部正常**。
崩溃发生在 **`scheduler_start_secondary()` 内部的上下文切换**。

### 崩溃根因分析

`scheduler_start_secondary()`（scheduler.c:734）执行流程：
1. `scheduler_pick_next()` 选线程
2. 若无就绪线程，用 `cpu_q->idle_thread`
3. `cpu_switch_to_first_task(idle_thread->context)` 通过 `eret` 切换

`cpu_switch_to_first_task`（context.S:160）从 context 数组恢复寄存器后 `eret` 到 ELR。
**推测崩溃点**：`eret` 跳转后取指失败 → PC=0。

可能原因（按优先级）：
1. **idle 线程 context 的 ELR 指向的 `thread_entry_trampoline` 代码对从核不可见**（缓存一致性）
   - 虽然加了 `flush_code_cache`（覆盖 secondary_entry 段），但未覆盖调度器/context.S 代码
2. **`scheduler_pick_next()` 在从核返回了不属于该核的线程**，切换到无效上下文
3. 从核 `eret` 时 SPSR 状态问题（SPSR=0x200003C5 的 bit21 IL 标志）

### 已实施的改进（保留）

1. **`flush_code_cache()` 函数**（smp_boot.c）：PSCI 唤醒前清理 secondary_entry 代码段缓存
   - 这是 bare-metal PSCI 唤醒的标准正确实践
   - 当前虽未解决根因，但消除了潜在的缓存一致性风险
2. **诊断标记全部移除**，代码恢复干净

## 下一步（P1-2 任务）

### SMP 调度器从核切换修复

1. **扩大缓存清理范围**：`flush_code_cache` 覆盖整个 `.text` 段（调度器、context.S、idle 入口）
2. **验证 `scheduler_pick_next()` 从核行为**：确保从核只选自己的 idle 线程或亲和线程
3. **检查 `cpu_switch_to_first_task` 从核路径**：确认 eret 目标地址有效且代码可见
4. **添加 SMP 运行时验证**：从核成功进入 idle 循环后打印 CPU online 信息

## 代码统计

| 文件 | 改动 | 说明 |
|------|------|------|
| `kernel/arch/arm64/smp_boot.c` | +58 行 | 新增 flush_code_cache() + PSCI 唤醒前缓存清理 |
| `kernel/arch/arm64/boot.S` | 0（诊断标记已移除） | 临时诊断后恢复原状 |

## 验证结果

- ✅ 单核 QEMU 启动完整通过
- ✅ 交叉编译 make 全绿
- ✅ 宿主机测试 20/20 通过
- ✅ PSCI CPU_ON 调用成功（ret=0，entry=0x400000B0 正确）
- ✅ 从核早期初始化全部正常（MMU/GIC/定时器/中断/percpu）
- ❌ 从核调度器上下文切换崩溃（P1-2 待修复）

## 关键技术发现

1. **QEMU virt PSCI**: method=hvc，CPU_ON 工作正常，从核确实到达 entry_point
2. **QEMU virt RAM 起始**: 0x40000000，地址 0 是 boot ROM 区域（从核 PC=0 意味着跳到了非 RAM）
3. **从核唤醒时 MMU 关闭**：secondary_entry 必须能在 MMU 关闭时执行（当前代码满足）
4. **PSCI 缓存清理**是必要的防御性措施，即使不是当前根因
