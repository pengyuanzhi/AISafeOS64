# AISafeOS64 商业系统路线图

**版本**: 1.0
**日期**: 2026-07-07
**状态**: 评审通过，开始执行

---

## 定位

安全关键实时微内核操作系统，对标 QNX Neutrino + seL4 安全模型。
两大核心能力：完整 POSIX 生态 + 硬件虚拟化（ARM EL2 Hypervisor）。

---

## 当前基线

| 维度 | 评分 | 说明 |
|------|------|------|
| 微内核核心 | 7/10 | 调度/IPC/内存/中断/能力/进程/信号/定时器已实现，用户态 SVC 端到端验证通过 |
| POSIX 覆盖 | 2/10 | 88 个 syscall 桩，43 个返回 ENOSYS，实用覆盖率 <20% |
| 用户态服务 | 3/10 | 8 个服务骨架（init/fs/net/proc/mem/dev/path/security），与内核断链 |
| 虚拟化 | 0/10 | 纯 mock，无 EL2 硬件代码，boot.S 主动降级到 EL1 |
| 硬件平台 | 1/10 | 仅 QEMU virt，无设备树解析，无真实 SoC |

---

## 四大阶段

### 阶段 A：POSIX 生态闭环（优先级最高）

目标：能运行标准 musl libc 编译的 POSIX 程序。

#### A1. 进程管理完整实现
- 内核实现 clone（地址空间复制，写时复制）
- 内核实现 execve（ELF 加载替换现有进程映像）
- 内核实现 wait4/waitid（等待子进程）
- 内核实现 brk/mremap（堆管理）
- musl syscall 桩接入内核实现

#### A2. 文件系统服务闭合
- fs 服务补全：mkdir/unlink/readdir/rename/symlink/readlink
- RAMFS 完整实现
- DEVFS 实现（/dev 设备文件）
- proc 服务通过 IPC 提供 fork/exec
- mount/umount 接口

#### A3. 信号完整实现
- rt_sigreturn 实现
- kill/tkill/tgkill 接入内核 signal_kill
- sigaltstack 实现
- 信号 handler 跳转

#### A4. 网络栈接入 POSIX
- socket 系列 syscall 接入 net 服务
- 标准 POSIX socket API

#### A5. 同步与时钟
- futex 实现
- clock_gettime 接入真实硬件计数器
- getrandom 实现
- POSIX 定时器

验收：busybox shell 在 AISafeOS64 上可执行 ls/cat/echo/ps。

---

### 阶段 B：微内核服务化

目标：内核只保留最小可信计算基，策略移到用户态。

#### B1. 内核瘦身
- 删除 driver_core.c/drv_boot_blk.c
- 内核仅保留：调度+IPC+页表+中断+异常+能力

#### B2. init 启动链完善
- 从磁盘加载 init.elf
- init 通过 IPC 启动全部服务
- 服务依赖图管理

#### B3. 设备树解析
- 引入 libfdt
- 解析 DTB 替代硬编码地址

#### B4. 用户态驱动框架
- 驱动通过 IPC+能力访问硬件
- 中断通过 notification 投递

---

### 阶段 C：ARM EL2 硬件虚拟化

目标：在 EL2 运行 hypervisor，支持 Guest OS。

#### C1. EL2 Hypervisor 启动
- boot.S 保持在 EL2
- 配置 HCR_EL2 / VTCR_EL2 / VHE

#### C2. Stage-2 页表
- VTTBR_EL2 管理
- Stage-2 页表创建/销毁/切换

#### C3. vCPU 上下文管理
- VCPU 状态机
- EL2↔EL1 上下文保存/恢复
- HVC trap 处理

#### C4. 虚拟中断（VGIC）
- VGICv3 硬件虚拟化
- 物理中断→虚拟中断映射

#### C5. 虚拟定时器
- 虚拟定时器管理
- Guest 时间管理

#### C6. 虚拟设备
- virtio 后端
- 设备直通

#### C7. Guest OS 加载
- 从磁盘加载 Guest 内核
- 创建 stage-2 地址空间

验收：在 AISafeOS64 Hypervisor 上启动最小 Linux Guest。

---

### 阶段 D：商业化加固

目标：ISO 26262 ASIL-D / IEC 61508 SIL-4 认证级。

#### D1. 形式化验证
#### D2. 安全认证（WCET/MC/DC/追溯）
#### D3. 真实硬件适配（Cortex-R82AE）
#### D4. 开发者生态（SDK/工具链/调试器）

---

## 阶段依赖

```
阶段A（POSIX）→ 阶段B（服务化）→ 阶段C（虚拟化）
                                        ↓
                                    阶段D（认证）
```

## 优先级

| 优先级 | 阶段 | 原因 |
|--------|------|------|
| P0 | A1 进程管理 | 无 fork/exec 无法运行 POSIX 程序 |
| P0 | A2 文件系统 | 无 fs 无法持久化 |
| P1 | A3 信号 | POSIX 程序依赖信号 |
| P1 | A5 时钟/futex | 线程同步基础 |
| P2 | A4 网络 | 后续需要 |
| P2 | B1-B2 内核瘦身+init | 架构完善 |
| P3 | B3-B4 设备树+驱动 | 多平台支持 |
| P4 | C1-C7 虚拟化 | 需要完整 POSIX 支撑 |
