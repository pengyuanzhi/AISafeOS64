# MEMORY.md - AISafeOS64 微内核编程助手长期记忆

## 2026-04-05 P0/P1 开发完成 ✅ (09:45)

### P0 全部完成 ✅

**commit**: `7143a47` feat(kernel): P0-4 MMU暂时禁用 + P0-5 QEMU端到端验证通过

#### P0-1: ARM64 交叉编译验证 ✅
- 工具链: aarch64-linux-gnu-gcc (GCC 13)
- 零错误编译通过

#### P0-2/3: 系统调用分发器 ✅
**commit**: `367a797` feat(syscall): P0-2/3 系统调用分发器实现
- 新增 kernel/irq/syscall_dispatch.c (470 行)
- 修改 exception.S: SVC handler 构造 syscall_frame_t
- 32 个系统调用号全覆盖，6 类分发
- IPC/能力子系统直接调用已有 API

#### P0-4: 构建优化 ✅
**commit**: `c7e7d30` feat(kernel): P0-4 构建优化
- gc-sections 裁剪: text 从 36KB → 24.1KB
- -mno-outline-atomics 消除 libgcc 依赖
- MMU 暂用物理地址模式（用户态创建时启用完整映射）

#### P0-5: QEMU 端到端验证 ✅
- 4 核全部在线 (Online CPUs: 0x4)
- GIC、定时器、调度器、SMP 全部初始化成功
- text = 24.1KB < 30KB ✅

### P1 部分完成 ✅

#### P1-9: IRQ→IPC 通知集成 ✅
**commit**: `2f6f091` feat(irq): P1-9 IRQ→IPC 通知集成
- interrupt_dispatch() 调用 ipc_notification_signal()
- 中断事件通过 IPC 通知投递到用户态等待线程
- 信号位掩码: bit[irq & 63]

### 内核代码量
- **text**: 24.7KB (目标 < 30KB ✅)

### 当前项目进度: ~90%

#### 已完成 ✅
- ✅ 调度器 (256 级位图 + EDF + ARINC653)
- ✅ IPC 子系统 (channel + endpoint + notification + IC2)
- ✅ 虚拟内存管理
- ✅ 能力系统 (撤销/降权/移动/复制)
- ✅ SMP 多核 (4核启动 + 每 CPU 调度 + IPI + 负载均衡 + 亲和性)
- ✅ 同步原语 (Ticket Lock + 优先级继承互斥锁)
- ✅ 上下文切换 (ARM64 汇编)
- ✅ 形式化验证框架 + 认证证据收集
- ✅ 用户态服务 (FS/Proc/Mem/Net/Security/VMM/Path/Init/Dev)
- ✅ virtio 驱动框架 + 性能基准测试
- ✅ 多核负载测试验证（4核并行运行）
- ✅ **系统调用分发器 (32 个系统调用号全覆盖)**
- ✅ **ARM64 交叉编译验证**
- ✅ **QEMU 端到端验证 (4核)**
- ✅ **IRQ→IPC 通知集成**

#### 待完成 ⏳
- [ ] MISRA C:2012 静态分析全量扫描
- [ ] 安全认证文档 (ISO 26262, IEC 61508)
- [ ] 驱动完善 (virtio-blk 实际读写、virtio-net 收发包)
- [ ] MMU 页表映射启用（从物理地址切换到虚拟地址）
- [ ] 用户态→内核态系统调用 QEMU 验证
- [ ] 性能基准细化（IPC 延迟、调度延迟、中断延迟精确测量）
- [ ] cspace_from_root 性能优化 (O(n)→O(1))

## 2026-04-04 P0 多核验证完成 ✅ (19:59)

**commit**: `eb42c40` feat(sched,smp): 多核负载测试+负载均衡+tick心跳验证

### 多核负载测试
- **worker 线程**: 4 个工作线程绑定到 4 个 CPU（亲和性设置）
- **QEMU 输出**: A B 0 1 2 3 — 4 核全部独立运行线程 ✅
- **UART 自旋锁**: TicketLock 保护多核串行化打印，防止乱序

### 负载均衡验证
- **smp_sched_init()**: 初始化 SMP 调度器（每 CPU 就绪队列）
- **smp_set_affinity()**: 工作线程绑定到指定 CPU
- **memset 消除**: smp.c 中所有 memset 改为 volatile 手动清零

### 每 CPU Tick 心跳
- **timer_interrupt_handler**: 每 1000 tick 打印 [CPU_ID] 心跳
- 从核独立定时器中断，每核独立调度

### 内核代码量
- **text**: 26,158 bytes (25.5KB) — 目标 < 30KB ✅

### 当前项目进度: ~85%

#### 已完成 ✅
- ✅ 调度器 (256 级位图 + EDF + ARINC653)
- ✅ IPC 子系统 (channel + endpoint + notification + IC2)
- ✅ 虚拟内存管理
- ✅ 能力系统 (撤销/降权/移动/复制)
- ✅ SMP 多核 (4核启动 + 每 CPU 调度 + IPI + 负载均衡 + 亲和性)
- ✅ 同步原语 (Ticket Lock + 优先级继承互斥锁)
- ✅ 上下文切换 (ARM64 汇编)
- ✅ 形式化验证框架 + 认证证据收集
- ✅ 用户态服务 (FS/Proc/Mem/Net/Security/VMM/Path/Init/Dev)
- ✅ virtio 驱动框架 + 性能基准测试
- ✅ **多核负载测试验证（4核并行运行）**

#### 待完成 ⏳
- [ ] MISRA C:2012 静态分析全量扫描
- [ ] 安全认证文档 (ISO 26262, IEC 61508)
- [ ] 驱动完善 (virtio-blk 实际读写、virtio-net 收发包)
- [ ] MMU 页表映射启用（从物理地址切换到虚拟地址）
- [ ] 用户态→内核态系统调用接口
- [ ] 性能基准细化（IPC 延迟、调度延迟、中断延迟精确测量）

---

## 2026-04-04 每 CPU 调度器 + IPI Reschedule ✅ (19:05)

**commit**: `f1fbf1d` feat(sched,ipi): 每CPU调度器集成+IPI reschedule实现

### 每 CPU 调度器
- **scheduler_start_secondary()**: 从核进入完整调度循环（非 WFE idle）
- 从核初始化链: FP/SIMD → GIC → 定时器 → IRQ → 调度器 → 第一个线程
- 每 CPU 独立就绪队列 + 独立定时器中断 → 真正并行调度

### IPI Reschedule
- **ipi_send()**: 改用 `gic_send_sgi()` (GICv2 MMIO)，替换 ICC_SGI1R_EL1 (GICv3)
- **ipi_handler(RESCHEDULE)**: 调用 `schedule()` 触发跨核调度
- **irq_handler**: SGI 0-15 分发给 `ipi_handler()` 处理
- **ipi_broadcast()**: 支持 exclude_self 的广播 reschedule

### 内核代码量
- **text**: 23,294 bytes (22.7KB) — 目标 < 30KB ✅

---

## 2026-04-04 SMP 多核支持完成 ✅ (18:40)

**commit**: `dca900f` feat(smp): SMP多核支持 - 4核启动+从核初始化+PSCI+SGI处理

### 从核启动机制
- **PSCI 调用**: 使用 `hvc #0`（非 `smc`）从 EL1 调用 EL2 PSCI 服务
- **从核入口**: boot.S 中 `secondary_entry` 汇编（设置向量表+每CPU栈）
- **从核初始化**: FP/SIMD 使能 → GIC CPU interface → 独立定时器 → IRQ 使能
- **QEMU 验证**: `-smp 4` 下 4 核全部在线（Online CPUs: 0x4）

### 关键技术决策
1. **HVC 而非 SMC**: QEMU virt EL1 下 SMC 触发同步异常，改用 HVC 调用 EL2 PSCI
2. **volatile 清零**: 裸机无 libc，GCC -Os 会把循环优化为 memset 调用，需 volatile 阻止
3. **每 CPU 独立定时器**: 从核各自初始化 CNTP_CTL/CVAL，产生独立 tick 中断
4. **SGI 中断处理**: irq_handler 添加 SGI 0-15 分支，预留 IPI 扩展

### 内核代码量
- **text**: 22,470 bytes (21.9KB) — 目标 < 30KB ✅
- **bss**: 93,680 bytes（percpu + 调度器数据结构）

### 当前项目进度: ~80%

#### 已完成 ✅
- ✅ 调度器 (256 级位图 + EDF + ARINC653)
- ✅ IPC 子系统 (channel + endpoint + notification + IC2)
- ✅ 虚拟内存管理
- ✅ 能力系统 (撤销/降权/移动/复制)
- ✅ **SMP 多核 (4核启动 + 从核初始化 + PSCI + SGI)**
- ✅ 同步原语 (Ticket Lock + 优先级继承互斥锁)
- ✅ 上下文切换 (ARM64 汇编)
- ✅ 形式化验证框架 + 认证证据收集
- ✅ 用户态服务 (FS/Proc/Mem/Net/Security/VMM/Path/Init/Dev)
- ✅ virtio 驱动框架 + 性能基准测试

#### 待完成 ⏳
- [ ] 每 CPU 调度器集成（从核进入调度循环而非 WFE idle）
- [ ] IPI reschedule 实现（SGI 驱动线程迁移）
- [ ] MISRA C:2012 静态分析全量扫描
- [ ] 安全认证文档 (ISO 26262, IEC 61508)

---

## 2026-04-04 三阶段开发完成 ✅ (07:10-08:11)

### Phase 1: 能力系统完善 + SMP 负载均衡 ✅ (07:42)
**commit**: `ecb4558` feat(cap,smp): 完善能力系统撤销/降权 + SMP负载均衡与CPU亲和性 + 单元测试

#### 能力系统增强
- **capability.c** (809 行): 完善核心操作
  - `cap_copy`: 降权复制，建立父子关系
  - `cap_revoke`: 级联撤销，使用显式栈替代递归（MISRA-C 合规）
  - `cap_move`: 原子性移动，转移子能力关系
  - `cap_delete`: 非级联删除
  - `cap_validate`: 查找并验证权限
- **cspace.c** (750 行): CSpace 管理

#### SMP 多核调度
- **smp.c** (891 行): 完整多核调度实现
  - 每 CPU 优先级位图就绪队列（256 级）
  - 基于优先级的负载迁移（多线程批量迁移，单次最多 4 线程）
  - CPU 亲和性跟踪与强制执行
  - 工作窃取算法
  - 负载均衡间隔: 100 次调度，不均衡阈值: 150%
- **test_smp.c** (1080 行): SMP 单元测试
- **test_capability_revoke.c** (1404 行): 能力撤销测试

### Phase 2: 用户态服务完善 ✅ (07:52)
**commit**: `1215edb` feat(services): Phase 2 用户态服务完善 - 四大核心服务增强

#### 文件系统服务 (fs)
- **main.c** (1387 行): VFS 核心服务
  - 文件描述符表管理（每进程）
  - 挂载点管理
  - 完整路径解析算法
  - inode LRU 缓存管理
  - 文件操作: open/read/write/close/lseek/stat/mkdir/unlink
  - 建议性文件锁 (advisory locking)

#### 进程管理器 (proc)
- **main.c** (1027 行): 完整进程生命周期管理
  - fork/exec 语义的进程创建
  - 进程状态机 (ready/running/blocked/zombie)
  - 信号处理框架
  - 资源限制 (rlimit)
  - waitpid/exit 机制

#### 内存管理器 (mem)
- **main.c** (1018 行): 用户态内存管理服务
  - Buddy 分配器（物理页管理）
  - Slab 分配器（小对象管理）
  - 共享内存管理
  - 内存映射 (mmap/munmap)
  - 内存统计和限制

#### 驱动框架 (dev)
- **driver_framework.c** (806 行): 用户态驱动框架

### Phase 3: 网络栈 + 安全服务 + VMM + 路径服务 + Init ✅ (08:11)
**commit**: `ca4353c` feat(services): Phase 3 网络栈+安全服务+VMM+路径服务完善
**commit**: `52be225` feat(services): Phase 3 Init服务+路径服务完善

#### 网络协议栈 (net)
- **main.c** (2074 行): 完整用户态网络协议栈
  - IPv4 数据包封装/解析
  - ICMP 回显请求/应答
  - UDP 套接字 (bind/sendto/recvfrom)
  - TCP 状态机 (CLOSED→SYN_SENT→ESTABLISHED→FIN_WAIT 等)
  - TCP 滑动窗口和重传机制
  - ARP 解析缓存
  - 网络接口抽象层（以太网帧收发）

#### 安全认证服务 (security)
- **main.c** (938 行): 安全认证服务
  - 安全启动链验证
  - 审计日志管理（最多 64 条）
  - SHA-256 完整性校验
  - 强制访问控制 (MAC) 策略引擎
  - 基于能力的访问控制决策
  - 安全上下文管理
- **certification.c** (1072 行): 认证证据收集

#### 虚拟机监视器 (vmm)
- **vmm.c** (923 行) + **vmm.h** (336 行) + **main.c** (50 行)
  - VMM 子系统初始化
  - IPC 消息循环
  - VM 生命周期管理

#### 路径管理器 (path)
- **main.c** (606 行): 服务注册/发现
  - 名字解析（服务名→IPC 端点映射）
  - 服务健康检查（超时 5000ms）
  - 设备路径命名空间
  - 挂载点管理

#### Init 服务 (init)
- **main.c** (719 行): 系统启动编排
  - 服务启动顺序管理（依赖图）
  - 服务监控和自动重启（最多 3 次）
  - 系统状态报告

### 代码统计更新 (Phase 1-3 后)

| 模块 | 行数 | 变化 |
|------|------|------|
| kernel/ | 16,032 | +3,365 |
| services/ | 10,956 | +10,956 (全新) |
| tests/ | 16,540 (21 文件) | +2,484 |
| drivers/ | 2,354 | — |
| include/ | 9,773 | — |
| lib/ | 678 | — |
| **总计** | **~57,033** | **+37,033** |

### 技术决策记录

1. **能力撤销使用显式栈而非递归**: 避免栈溢出风险，符合 MISRA-C:2012 禁止递归的规则
2. **SMP 每 CPU 就绪队列**: 避免 CPU 间锁竞争，使用 TicketLock 保护
3. **负载均衡批量迁移**: 单次最多迁移 4 线程，避免瞬时抖动
4. **网络栈全用户态**: 通过 IPC 与驱动交互，符合微内核设计哲学
5. **TCP 完整状态机**: 实现 CLOSED→SYN_SENT→ESTABLISHED→FIN_WAIT 全状态
6. **Init 服务依赖图**: 编排服务启动顺序，支持自动重启

### 当前项目进度: ~70%

#### 已完成 ✅
- ✅ 调度器 (256 级位图 + EDF + ARINC653)
- ✅ IPC 子系统 (channel + endpoint + notification + IC2)
- ✅ 虚拟内存管理
- ✅ 能力系统 (撤销/降权/移动/复制)
- ✅ SMP 多核 (负载均衡 + 亲和性 + 工作窃取)
- ✅ 同步原语 (Ticket Lock + 优先级继承互斥锁)
- ✅ 形式化验证框架
- ✅ 认证证据收集
- ✅ 用户态服务 (FS/Proc/Mem/Net/Security/VMM/Path/Init/Dev)

### Phase 4: Init 服务+头文件完善 ✅ (08:22)
**commit**: `ab4a6df` feat(init,headers): Phase 3补充+Phase 4 Init服务+头文件完善
**commit**: `26b56df` fix(init): 修复 init_print_report 中 for 循环变量声明的 MISRA 违规

### Phase 5-6: 测试编译修复+架构文档 ✅ (08:39)
**commit**: `dfaf653` fix(tests): 修复测试编译兼容性
**commit**: `34a0f32` docs(architecture): Phase 6 架构文档更新+测试验证
**commit**: `953c400` docs: 更新Phase 5-6完成记录和测试结果

### Phase 7: 代码审计+virtio驱动+内核工具函数 ✅ (09:10)
**commit**: `b18baad` feat(drivers): 添加 virtio 驱动框架
**commit**: `0ecb3de` feat(drivers+lib): 添加内核字符串操作库

#### virtio 驱动框架
- **virtio_pci.c** (928 行): virtio PCI 设备探测和配置
- **virtio_ring.c** (979 行): virtqueue 环形缓冲区管理
- **virtio_blk.c** (542 行): virtio 块设备驱动
- **virtio_net.c** (721 行): virtio 网络设备驱动

#### 内核工具函数
- **kernel_string.c** (926 行): 内核安全字符串操作库

### 飞书通知配置 ✅ (08:50)
- 配置 cron 简报任务推送到飞书（每 30 分钟）
- 修复飞书 WebSocket 连接问题（Gateway 重启后未重连）
- 解决飞书 session 被编码任务 lock 导致消息无法响应的问题
- maxConcurrent 从 4 提高到 8
- 飞书简报已于 09:00 成功推送第一份进度报告

### 代码统计更新 (Phase 7 后)

| 模块 | 行数 | 变化 |
|------|------|------|
| kernel/ | 16,032 | — |
| services/ | 11,462 | +506 |
| tests/ | 17,255 | +715 |
| drivers/ | 5,524 | +3,170 (virtio) |
| include/ | 10,082 | +309 |
| lib/ | 1,604 | +926 (kernel_string) |
| **总计** | **~61,959** | **+4,926** |

### 当前项目进度: ~75%

#### 已完成 ✅
- ✅ 调度器 (256 级位图 + EDF + ARINC653)
- ✅ IPC 子系统 (channel + endpoint + notification + IC2)
- ✅ 虚拟内存管理
- ✅ 能力系统 (撤销/降权/移动/复制)
- ✅ SMP 多核 (负载均衡 + 亲和性 + 工作窃取)
- ✅ 同步原语 (Ticket Lock + 优先级继承互斥锁)
- ✅ 形式化验证框架
- ✅ 认证证据收集
- ✅ 用户态服务 (FS/Proc/Mem/Net/Security/VMM/Path/Init/Dev)
- ✅ virtio 驱动框架 (PCI/Ring/Blk/Net)
- ✅ 内核工具函数库
- ✅ 架构文档更新
- ✅ 飞书进度通知

#### 待完成 ⏳
- [ ] 安装 ARM64 交叉编译器，完整编译验证
- [ ] QEMU 实机运行测试
- [ ] 内核代码量审计（目标 < 50KB 代码段）
- [ ] 形式化验证关键路径证明
- [ ] 安全认证文档准备 (ISO 26262 ASIL-D, IEC 61508 SIL-4)
- [ ] 性能基准测试 (IPC 延迟、调度延迟、中断延迟)
- [ ] MISRA C:2012 静态分析全量扫描
- [ ] 用户态服务 Rust 重写

---

## 2026-04-03 模型升级 (22:59)

### GLM-5.1 升级
- **操作**: 添加 GLM-5.1 模型配置到 OpenClaw
- **更新**: aisafeos agent 模型从 `zai/glm-5` 升级为 `zai/glm-5.1`
- **Gateway**: 自动重启 (pid: 41405)
- **优势**: 更强的推理能力、更好的代码理解、更准确的技术分析
- **配置**: 204800 tokens 上下文窗口，131072 tokens 最大输出

## 2026-04-03 Agent 创建

### 创建工作空间
- **事件**: 在项目目录创建 OpenClaw agent 工作空间
- **工作空间**: `/home/kerfs/AISafeOS64/AISafeOS64`
- **开发者**: 方成 (babydoge)
- **目的**: 在代码目录中直接工作，方便快速访问代码

---

这里记录 AISafeOS64 开发过程中的重要决策、发现和待办事项。

### 测试验证结果 ✅ (08:39)
- **19/19 测试集全部通过，38,863 个断言零失败**
- 所有宿主机单元测试编译并运行成功
- 关键测试: scheduler(13,672), phys_mem(9,108), mutex(4,382), channel(2,410)

### 构建系统完成 ✅
- CMake 构建系统支持内核/服务/测试
- ARM64 链接脚本 (MMU 4KB 页对齐)
- 静态分析脚本 (check_misra.sh, check_format.sh, code_stats.sh)
- 架构文档 docs/design/ARCHITECTURE.md (351行)

### 定时器+中断调试完成 ✅ (13:58)
- GIC GICv2 初始化完全成功 (7步诊断通过)
- Physical Timer IRQ 30 使能确认 (ISENABLER0=0x4000FFFF)
- 定时器轮询模式工作正常 (tick #1000+)
- freq=62.5MHz, delta=625000 (~10ms per tick)
- IRQ 中断分发待完善 (轮询模式已可用)
- text=30.4KB, 需精简诊断打印到30KB以内

### 内核代码目标更新
- **从 50KB 更新为 30KB** (更严格的微内核目标)
- 当前 text=30.4KB, 需优化
