# AISafeOS64 内核架构重构实施计划

**版本**: 1.0
**日期**: 2026-07-05
**状态**: 评审中
**关联**: [架构设计文档](../design/ARCHITECTURE_REFACTOR.md)

---

## 1. 18 个模块总览

| 阶段 | 模块 | 内容 | 预期text变化 |
|------|------|------|-------------|
| 一 | 1 | 标准日志接口（klog） | -3KB |
| 一 | 2 | 死代码大清理 | -8~15KB |
| 一 | 3 | 链表操作统一 | -1KB |
| 一 | 4 | 中断控制器HAL抽象 | ±0 |
| 二 | 5 | kobject体系接入 | +1KB |
| 二 | 6 | 调度类框架 | -2KB（移除EDF） |
| 二 | 7 | 架构分层清理 | ±0 |
| 二 | 8 | 地址空间管理统一 | ±0 |
| 三 | 9 | 系统调用安全加固 | +0.5KB |
| 三 | 10 | 线程生命周期+资源回收 | +1KB |
| 三 | 11 | 内存管理层次修正 | ±0 |
| 四 | 12 | 驱动框架清理 | -5KB |
| 四 | 13 | kernel_main拆分+entry.c瘦身 | -10KB |
| 四 | 14 | 注释规范+代码风格 | ±0 |
| 五 | 15 | 进程抽象 | +3KB |
| 五 | 16 | POSIX信号 | +2KB |
| 五 | 17 | 用户定时器 | +2KB |
| 五 | 18 | init启动链 | +1KB |

**预期最终 text**：35~40KB（从当前 52KB）

---

## 2. 各模块详细分解

### 模块 1：标准日志接口（klog）

**改动文件**：
- 新增 `include/kernel/klog.h`：klog_level_t + klog_error/warn/info/debug 宏
- 新增 `kernel/klog.c`：实现（内部固定 UART base，vsnprintf 格式化）
- 修改 `kernel/arch/arm64/hal.h`：保留 hal_uart_init/putc/puts 供 klog 内部用

**替换清单**（280+ 处）：
- `hal_uart_puts((uint64_t)QEMU_UART0_BASE, "...")` → `klog_info("...")`
- `hal_uart_putc((uint64_t)QEMU_UART0_BASE, c)` → 内部使用
- `uart_print_hex((uint64_t)QEMU_UART0_BASE, v)` → klog 内部提供

**验证**：编译通过 + QEMU 单核引导到 Start sched + text < 50KB

---

### 模块 2：死代码大清理

**删除文件**：
- `kernel/verify/formal_verify.c` + `evidence.c`（死代码+概念错位）
- `include/kernel/formal_verify.h` + `evidence.h`
- `kernel/ipi/ipi_coalesce.c` + `include/kernel/ipi_coalesce.h`（死代码+bug）
- `kernel/mm/buddy_system.c` + `include/kernel/mm/buddy_system.h`（重复实现）
- `kernel/driver/driver_module.c`（内核态模块加载器，安全风险）
- `kernel/kernel/`（空目录）

**修改文件**：
- `kernel/CMakeLists.txt`：移除上述源文件
- `kernel/cap/capability.c`：删除 fv_register_cap_invariants 调用
- `kernel/cap/cspace.c`：删除 s_cap_table_pool（未用静态池）
- `kernel/arch/arm64/gic.c`：删除 gic_register_handler（死代码）
- `kernel/arch/arm64/hal.h`：删除重复声明段

**验证**：编译通过 + QEMU 单核正常 + text 减少 5KB+

---

### 模块 3：链表操作统一

**修改文件**：
- `include/kernel/list.h`：确保 list_add_tail/list_del_init/list_for_each_safe 完整
- `kernel/sched/scheduler.c`：30+ 处手动 ->next/->prev 改为宏
- `kernel/sched/smp.c`：同上
- `kernel/sched/timer.c`：删除 list_remove_self，用 list_del_init
- `kernel/sched/scheduler.c`：提取 __sched_dequeue_locked/__sched_enqueue_locked

**验证**：编译通过 + QEMU 单核正常

---

### 模块 4：中断控制器 HAL 抽象

**新增文件**：
- `include/kernel/hal_intc.h`：hal_intc_* 接口声明

**修改文件**：
- `kernel/arch/arm64/hal.h`：添加 hal_intc_* 声明（或引用 hal_intc.h）
- `kernel/arch/arm64/gic.c`：实现 hal_intc_* 接口（包装现有 gic_* 函数）
- `kernel/irq/interrupt.c`→重命名 `kernel/irq/irq.c`：所有 gic_* 调用改为 hal_intc_*
- `include/kernel/interrupt.h`→重命名 `include/kernel/irq.h`：irq_ 前缀统一
- `kernel/arch/arm64/entry.c`：irq_handler 用 hal_intc_* 替代 gic_*

**接口映射**：
```
gic_init           → hal_intc_init
gic_enable_irq     → hal_intc_enable
gic_disable_irq    → hal_intc_disable
gic_set_priority   → hal_intc_set_priority
gic_set_trigger_mode → hal_intc_set_trigger
gic_get_irq_id     → hal_intc_acknowledge
gic_end_of_interrupt → hal_intc_eoi
gic_send_sgi       → hal_intc_send_ipi
```

**验证**：编译通过 + QEMU 单核正常（中断+定时器工作）

---

### 模块 5：kobject 体系接入

**重命名**：
- `KObjHeader_t` → `kobj_header_t`（全代码库替换，43 处）

**修改文件**：
- `include/kernel/ipc_types.h`：endpoint/notification/channel/connection 添加 kobj_header_t 首成员
- `include/kernel/vmspace.h`：vm_space_t 添加 kobj_header_t
- `kernel/sched/thread.h`：KThread_t 添加 kobj_header_t
- `kernel/mm/kobject.c`：确保 kobj_alloc/ref_inc/ref_dec/destroy 完整可用
- `kernel/cap/capability.c`：cap_copy 时 ref_inc，cap_revoke/delete 时 ref_dec
- `kernel/ipc/endpoint.c`：endpoint_create 调 kobj_alloc，destroy 调 kobj_destroy
- `kernel/mm/vmspace.c`：同上
- `kernel/mm/shm.c`：修复调用不存在的 kobject_register/get/unregister
- `kernel/elf_loader.c` → 移到 `kernel/mm/elf_loader.c`

**启动序列修复**（entry.c kernel_main）：
- 添加 notification_subsys_init()
- 添加 channel_subsys_init()
- 添加 kobject_subsys_init()
- 添加 shm_subsys_init()

**验证**：编译通过 + QEMU 单核正常 + kobj_ref_inc/dec 符号在二进制中

---

### 模块 6：调度类框架（sched_class）

**新增文件**：
- `kernel/sched/sched_class.h`：struct sched_class + 注册/遍历接口
- `kernel/sched/sched_rr.c`：256级位图+RR（从 scheduler.c 提取）
- `kernel/sched/sched_fifo.c`：严格优先级

**删除文件**：
- `kernel/sched/edf.c` + `kernel/sched/edf.h`（移除EDF）

**修改文件**：
- `kernel/sched/scheduler.c`：schedule() 遍历 sched_class 链表
- `kernel/sched/scheduler.c`：删除 #include <kernel/mmu.h>
- `kernel/sched/scheduler.c`：删除 TTBR0/MMU 操作
- `kernel/sched/thread.h`：KThread_t 添加 sched_class 指针
- `kernel/arch/arm64/entry.c`：删除 edf_init 调用

**验证**：编译通过 + QEMU 单核正常 + 无 EDF 符号

---

### 模块 7：架构分层清理

**新增接口**（hal.h 或新头文件）：
```c
void arch_context_switch(uint64_t *prev_ctx, uint64_t *next_ctx);
void arch_switch_to_user(uint64_t user_pgd, uint64_t *ctx);
```

**修改文件**：
- `kernel/sched/scheduler.c`：ELR/SPSR/MMU 操作改为调 arch_context_switch
- `kernel/sched/scheduler.c`：删除 extern mmu_switch_to_user_asm
- `kernel/arch/arm64/context.S`：实现 arch_context_switch

**验证**：编译通过 + QEMU 单核正常

---

### 模块 8：地址空间管理统一

**修改文件**：
- `kernel/sched/scheduler.c`：用户线程切换调 vmspace_switch（非 mmu_switch_to_user）
- `kernel/mm/vmspace.c`：vmspace_switch 用 virt_to_phys + ASID
- `kernel/arch/arm64/mmu.c`：实现 hal_tlb_flush_asid（tlbi asides1）
- `kernel/arch/arm64/mmu.c`：删除 s_user_pgds 静态池
- `kernel/arch/arm64/mmu.c`：PGD 位图分配加锁
- `kernel/mm/vmspace.c`：vmspace_destroy 完整释放 4 级页表

**验证**：编译通过 + QEMU 单核正常

---

### 模块 9：系统调用安全加固

**修改文件**：
- `kernel/irq/syscall_dispatch.c`：
  - SYS_VM_MAP：添加 MMIO 白名单检查
  - SYS_INTERRUPT_ATTACH：添加 IRQ 能力校验
  - thread_check_cap：无 CSpace 时返回 EACCES（非放行）
  - 统一系统调用编号
- `kernel/irq/syscall_dispatch.c`：s_user_mmap_ptr 改为 per-thread VMA 分配

**验证**：编译通过 + QEMU 单核正常

---

### 模块 10：线程生命周期 + 资源回收

**修改文件**：
- `kernel/sched/thread.c`：
  - alloc_thread_id 改用位图+自旋锁
  - kthread_exit 添加资源释放（CSpace ref_dec、vmspace ref_dec）
- `kernel/sched/scheduler.c`：
  - idle_task_entry 中周期调 kthread_cleanup_dead_stacks
  - 跨 CPU 唤醒时发 IPI 抢占
- `kernel/sched/thread.c`：栈分配器支持回收

**验证**：编译通过 + QEMU 单核正常

---

### 模块 11：内存管理层次修正

**修改文件**：
- `kernel/mm/slab.c`：slab 从 phys_mem 分配页（非 kmalloc）
- `kernel/mm/kmalloc.c`：从 slab 获取对象
- `kernel/mm/phys_mem.c`：扩大 MAX_PHYS_PAGES

**验证**：编译通过 + QEMU 单核正常

---

### 模块 12：驱动框架清理

**CMakeLists 修改**：
- 移除 drv_virtio_blk.c（1400行）
- 移除 drv_uart.c
- 保留 driver_core.c（最小设备发现框架）
- 保留 drv_boot_blk.c（引导期 ELF 读取）

**修改文件**：
- `kernel/driver/driver_core.c`：删除 device_read/write（改为IPC）
- `kernel/arch/arm64/entry.c`：删除 drv_virtio_blk_register 调用
- 统一 virtio 寄存器定义到 `include/kernel/virtio_mmio.h`

**验证**：编译通过 + QEMU 单核正常 + text 减少 5KB+

---

### 模块 13：kernel_main 拆分 + entry.c 瘦身

**目标**：entry.c 从 3386 行 → < 300 行

**删除**：
- 所有 #if CONFIG_DEBUG 测试/bench 代码（~2800 行）
- g_service_eps / s_pm_table 等内核全局
- 假 EL0 服务函数（fs/proc/mem/path_service_entry）
- 静态服务栈数组
- cap_runtime_test / smp_e2e_test / driver_e2e_test

**新增**：
- `kernel/init/version.c`：版本打印

**修改**：
- `kernel/arch/arm64/entry.c`：kernel_main < 100 行（仅子系统init编排）

**验证**：编译通过 + QEMU 单核正常 + text 减少 10KB+

---

### 模块 14：注释规范 + 代码风格

**全量处理**：
- 所有公共函数添加 Doxygen 头注释
- 文件头添加修订记录
- 删除内联修订注释（如"P0-4 修复"）
- clang-format 全量格式化
- 变量声明统一到块头（C89 风格）

**验证**：编译通过 + clang-format 检查通过

---

### 模块 15：进程抽象

**新增文件**：
- `kernel/process/process.c` + `include/kernel/process.h`
- Process_t 结构 + 创建/退出/等待

**新增 syscall**：
- SYS_PROCESS_CREATE / EXIT / WAIT / GETPID

**验证**：编译通过 + QEMU 单核正常

---

### 模块 16：POSIX 信号

**新增文件**：
- `kernel/process/signal.c` + `include/kernel/signal.h`

**新增 syscall**：
- SYS_SIGNAL_ACTION / KILL / PROCMASK / RETURN

**验证**：编译通过 + QEMU 单核正常

---

### 模块 17：用户定时器

**新增文件**：
- `kernel/sched/user_timer.c` + `include/kernel/user_timer.h`

**新增 syscall**：
- SYS_TIMER_CREATE / SETTIME / DELETE
- SYS_NANOSLEEP / CLOCK_GETTIME

**验证**：编译通过 + QEMU 单核正常

---

### 模块 18：init 启动链

**修改文件**：
- `kernel/arch/arm64/entry.c`：kernel_main 末尾加载 init.elf
- `kernel/mm/elf_loader.c`：加载 init.elf 创建 init 进程

**新增**：
- `services/init/main.c`：init 服务（注册名称服务 + 启动其他服务）

**验证**：编译通过 + QEMU 单核引导到 init 服务运行

---

## 3. 里程碑

| 里程碑 | 模块 | 目标 | text |
|--------|------|------|------|
| M1 | 1-4 | 基础设施完成 | <45KB |
| M2 | 5-8 | 核心架构重构 | <45KB |
| M3 | 9-11 | 安全+资源完整 | <45KB |
| M4 | 12-14 | 内核瘦身 | <40KB |
| M5 | 15-18 | 核心机制补齐 | <45KB |

## 4. 验收标准

每个模块必须满足：
- ✅ `make -j4` 零错误零警告
- ✅ QEMU 单核引导到 Start sched
- ✅ text < 50KB
- ✅ 遵循 AGENTS.md 15 条规则
- ✅ clang-format 格式检查通过
- ✅ Conventional Commits 提交规范
