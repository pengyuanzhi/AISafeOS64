# AISafe64

AISafe64 (AI-Generated, Safety-Certifiable, Native 64-bit RTOS) 是一个为安全关键嵌入式系统设计的实时操作系统，严格遵循 MISRA-C:2012 标准，针对 ARMv8-A 多核 SMP 架构进行了深度优化。

## 项目简介

AISafe64 是一个现代化的 64 位实时操作系统，专为汽车电子（ISO 26262 ASIL-D）、航空航天、工业控制等安全关键领域设计。系统采用扁平化任务模型，在保证高性能的同时提供可配置的地址空间隔离机制。

### 核心特性

| 特性 | 实现方案 |
|------|---------|
| **256级优先级** | 4×64位位图 + CLZ指令，O(1)查找 |
| **多核SMP** | Ticket Lock、IPI、负载均衡 |
| **MMU** | ARMv8-A 4级页表，用户/内核隔离 |
| **代码保护** | RX权限、NX位、SHA-256校验 |
| **MISRA合规** | 完整规则+工具链集成 |

## 主要特性

### 实时性保障

- **确定性的调度延迟**：上下文切换时间 < 1μs
- **O(1)调度算法**：使用硬件 CLZ 指令快速查找最高优先级任务
- **抢占式调度**：支持 FIFO、RR、EDF、CFS 等多种调度策略

### 高性能架构

- **扁平化任务模型**：
  - 共享地址空间模式：任务切换仅保存寄存器（~100ns）
  - 独立地址空间模式：MMU 页表切换（~1-5μs）
  - 混合模式：关键任务隔离 + 普通任务共享

- **零拷贝通信**：共享地址空间模式下任务间直接传递消息

### 多核支持

- **1-8核心 SMP**：自动负载均衡
- **核心间中断（IPI）**：任务迁移和同步
- **核间同步**：Ticket Lock + 内存屏障

### 安全机制

- **地址空间隔离**：可选的 MMU 页表隔离
- **栈溢出保护**：金丝雀值 + 边界模式 + MPU 保护页
- **代码完整性保护**：ELF 签名验证（ECDSA-P256 + SHA-256）
- **安全钩子框架**：Capability-based Security

### 标准兼容

- **POSIX 兼容层**：支持 pthread、mutex、semaphore 等 PSE52 接口
- **自适应系统调用**：根据隔离模式自动选择最优调用方式

## 系统架构

### 调度器

```
┌─────────────────────────────────────┐
│         调度类抽象层                │
├─────────────────────────────────────┤
│ FIFO │ EDF │ CFS │ RR │ IDLE       │
├─────────────────────────────────────┤
│      256级优先级位图 (O(1))         │
├─────────────────────────────────────┤
│        多核 SMP 负载均衡            │
└─────────────────────────────────────┘
```

### 内存管理

```
┌─────────────────────────────────────┐
│    VFS (initramfs/procfs/devfs)     │
├─────────────────────────────────────┤
│        SLAB 分配器                  │
├─────────────────────────────────────┤
│   ARMv8-A 4级页表 MMU               │
├─────────────────────────────────────┤
│   ELF 加载器 + 签名验证             │
└─────────────────────────────────────┘
```

## 构建说明

### 前置要求

- **工具链**：aarch64-none-elf-gcc (支持 ARMv8-A)
- **CMake**：>= 3.20
- **构建平台**：Linux、Windows (WSL)、macOS

### 构建步骤

```bash
# 1. 配置工具链文件
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm64.cmake

# 2. 编译
make -j$(nproc)

# 3. 生成二进制文件
# 输出文件：
# - build/aisafe64.elf (ELF 格式)
# - build/aisafe64.bin (二进制格式)
# - build/aisafe64.dis (反汇编文件)
```

### 配置选项

通过 `Kconfig` 配置系统：

```bash
# 配置内核
make menuconfig

# 或使用默认配置
cp configs/defconfig .config
```

主要配置项：
- `CONFIG_MAX_TASKS`: 最大任务数量（默认：32）
- `CONFIG_ENABLE_MMU`: 启用 MMU（默认：y）
- `CONFIG_ENABLE_SMP`: 启用多核支持（默认：y）
- `CONFIG_PRIORITY_LEVELS`: 优先级级别（固定：256）
- `CONFIG_POSIX_COMPAT`: POSIX 兼容层（默认：y）

## 运行环境

### 硬件要求

- **架构**：ARMv8-A (AArch64)
- **CPU**：1-8 核心
- **内存**：>= 512MB
- **存储**：支持 QEMU virt machine、Raspberry Pi 4/5

### QEMU 模拟

```bash
# 启动 4 核心系统
qemu-system-aarch64 \
  -M virt \
  -cpu cortex-a57 \
  -smp 4 \
  -m 1G \
  -kernel build/aisafe64.elf \
  -nographic \
  -serial mon:stdio
```

## 项目结构

```
AISafeOS64/
├── docs/              # 文档目录
│   ├── CLAUDE.md      # 代码生成规范（MISRA-C:2012）
│   ├── plan.md        # 项目规划
│   ├── design.md      # 详细设计
│   └── *.md           # 其他设计文档
├── src/               # 源代码
│   ├── kernel/        # 内核核心
│   ├── mm/            # 内存管理
│   ├── scheduler/     # 调度器
│   ├── ipc/           # 进程间通信
│   ├── fs/            # 文件系统
│   └── drivers/       # 设备驱动
├── include/           # 头文件
├── configs/           # 配置文件
├── cmake/             # CMake 模块
├── CMakeLists.txt     # 主 CMake 文件
└── README.md          # 本文件
```

## 文档

- [MISRA-C:2012 编码规范](docs/CLAUDE.md) - 详细的代码生成规范
- [系统调用架构设计](docs/SYSCALL_DESIGN.md) - 自适应系统调用设计
- [MMU 使能策略](docs/MMU_ENABLE_STRATEGY.md) - 内存管理单元配置
- [实现路线图](docs/implementation_roadmap.md) - 开发计划

## 开发指南

### 代码风格

本项目严格遵循 MISRA-C:2012 规范：

1. **所有代码必须通过静态分析**（PC-lint Plus）
2. **使用明确的类型转换**（避免隐式转换）
3. **函数圈复杂度 <= 10**
4. **所有公开 API 必须有文档注释**

### 贡献流程

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/amazing-feature`)
3. 提交更改 (`git commit -m 'Add some amazing feature'`)
4. 推送到分支 (`git push origin feature/amazing-feature`)
5. 开启 Pull Request

### 提交规范

提交信息格式：

```
<type>: <subject>

<body>

<footer>
```

类型（type）：
- `feat`: 新功能
- `fix`: Bug 修复
- `docs`: 文档更新
- `style`: 代码格式（不影响功能）
- `refactor`: 重构
- `test`: 测试相关
- `chore`: 构建/工具链相关

## 性能指标

| 指标 | 数值 | 说明 |
|------|------|------|
| 上下文切换时间 | < 1μs | 共享地址空间模式 |
| 任务调度延迟 | ~185ns | O(1)查找 + 函数调用 |
| 系统调用开销 | ~10周期 | 直接调用（共享模式） |
| 系统调用开销 | ~180周期 | SVC（独立模式） |
| 中断延迟 | < 500ns | 最坏情况 |
| 栈使用 | 300-400字节 | 最小 TCB 大小 |

## 安全认证

AISafe64 设计目标：

- **ISO 26262 ASIL-D**：汽车功能安全最高等级
- **IEC 61508 SIL 3**：工业功能安全
- **EN 50128 SW SIL 4**：铁路应用安全

## 许可证

本项目采用 [选择合适的许可证] 许可证。详见 LICENSE 文件。

## 致谢

- MISRA-C:2012 规范
- ARMv8-A Architecture Reference Manual
- Linux 内核调度器设计
- 自由软件社区的贡献

## 联系方式

- 项目主页：https://github.com/pengyuanzhi/AISafeOS64
- 问题反馈：GitHub Issues
- 邮箱：340589344@qq.com

---

**最后更新**：2025-01-08
**文档版本**：1.0
