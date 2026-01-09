# AISafe64 - AI生成的可安全认证的原生64位实时操作系统

## 项目概述

**AISafe64** 是一个可安全认证的、原生的64位实时操作系统，专为 ARM64 多核 SMP 架构设计。本项目强制执行严格的 MISRA-C:2012 合规性，并致力于工业安全认证（IEC 61508、ISO 26262）。

### 关键特性

- **架构**：原生 ARMv8-A (AArch64)，纯64位
- **多处理**：SMP（对称多处理），最多8核
- **调度**：256级优先级位图调度器，O(1)操作
- **内存管理**：启用 MMU，支持 4KB/2MB 页面
- **安全合规**：MISRA-C:2012 零偏差
- **认证目标**：SIL 4 (IEC 61508)、ASIL D (ISO 26262)

---

## 快速导航

### 核心标准

**[核心原则](rules/core-principles.md)**
- 安全关键编码原则
- 多核安全指南
- 功能安全要求

**[MISRA-C:2012 规则](rules/misra-c2012.md)**
- 强制规则（1.1-21.1）
- 建议规则及理由
- 执行工具和脚本

### 编码标准

**[ARM64 编码规范](rules/arm64-encoding.md)**
- 数据类型定义
- 对齐要求（16/64/4096 字节）
- 内联汇编模式
- 原子操作（C11 和 ARM64）
- 异常处理（EL0-EL3）
- 缓存操作

**[代码风格](rules/code-style.md)**
- 命名约定（函数、变量、类型）
- 格式规则（Allman 风格，4空格缩进）
- 注释标准（Doxygen 风格）
- 文件组织模式

### 内存与并发

**[内存管理](rules/memory-management.md)**
- 动态分配安全
- 栈使用限制
- 堆保护（双重释放检测）
- 内存池管理

**[并发编程](rules/concurrency.md)**
- 锁使用（自旋锁、互斥锁）
- 无锁编程（SPSC 队列、栈）
- 死锁预防
- 内存屏障使用

### 工程实践

**[错误处理](rules/error-handling.md)**
- POSIX 错误码约定
- 错误处理模式
- 断言（编译时和运行时）
- 诊断要求

**[性能优化](rules/performance.md)**
- 内联函数指南
- 分支预测提示
- 缓存优化
- 数据结构布局

**[测试规范](rules/testing.md)**
- 单元测试结构（Unity 框架）
- 硬件模拟模式
- MC/DC 覆盖率要求
- 测试驱动开发工作流

### 构建与配置

**[CMake 构建系统](rules/build-system.md)**
- CMake 文件组织
- 编译器标志（MISRA 合规）
- 交叉编译设置
- 链接器脚本集成
- 测试和覆盖率目标

**[MenuConfig 配置系统](rules/configuration.md)**
- Kconfig 语法（bool/tristate/int/hex/string）
- 依赖管理（depends on/select）
- .config 和 defconfig 格式
- 自动生成的 config.h

**[Git 工作流](rules/git-workflow.md)**
- Conventional Commits 规范
- 提交类型（feat/fix/docs/refactor/perf/test/chore/ci）
- 提交消息格式
- 分支命名约定

**[CI/CD](rules/ci-cd.md)**
- 提交前检查脚本
- MISRA 合规验证
- 自动化测试流水线
- 代码审查清单

---

## 代码生成约束

### 强制要求

1. **MISRA-C:2012 合规**
   - 所有代码必须通过 PC-lint Plus，零偏差
   - 仅使用 C11 标准特性（无编译器扩展）
   - 仅使用显式类型转换

2. **ARM64 架构**
   - 纯64位（不支持 AArch32）
   - 16字节栈对齐
   - 共享数据使用原子操作
   - 正确的内存屏障（DMB/DSB/ISB）

3. **错误处理**
   - 使用 POSIX 错误码（EINVAL、ENOMEM 等）
   - 系统调用返回负错误码
   - 所有错误路径必须测试

4. **代码风格**
   - Allman 括号风格
   - 4空格缩进（不使用 Tab）
   - 每行最多120字符
   - 所有公共 API 使用 Doxygen 注释

### 禁止模式

- 禁止递归（MISRA 16.1）
- 禁止 goto 语句（MISRA 15.1）
- 禁止变长数组（MISRA 9.1）
- 禁止隐式类型转换（MISRA 10.1-10.8）
- 禁止联合体类型双关（MISRA 19.1）
- 禁止八进制常量（除 0 外）（MISRA 7.1）
- 禁止使用 `while(1)` 无限循环 - 应使用 `for(;;)`

### 优先级顺序

当存在冲突时，遵循以下优先级顺序：

1. **安全**（无未定义行为）
2. **MISRA-C:2012**（合规性）
3. **功能需求**（正确性）
4. **性能**（优化）
5. **代码风格**（可读性）

---

## 文件结构参考

```
AISafeOS64/
├── .claude/
│   ├── CLAUDE.md              # 本文件
│   └── rules/                 # 详细标准
├── src/
│   ├── kernel/                # 内核核心
│   ├── mm/                    # 内存管理
│   ├── ipc/                   # 进程间通信
│   ├── fs/                    # 文件系统
│   ├── drivers/               # 设备驱动
│   └── lib/                   # 工具库
├── include/                   # 公共头文件
├── arch/arm64/                # 架构相关代码
├── scripts/                   # 构建和测试脚本
├── tests/                     # 单元测试
├── docs/                      # 额外文档
├── kconfig/                   # MenuConfig 文件
├── cmake/                     # CMake 模块
└── lds/                       # 链接器脚本
```

---

## 快速参考

### 常见 MISRA 违规避免

```c
/* ❌ 错误 */
int x = 010;                    // 八进制常量
uint32_t mask = 0xFF;           // 缺少 U 后缀
if (x = y) { }                  // 条件中的赋值
while (1) { }                   // 魔法数字

/* ✅ 正确 */
int x = 10;                     // 十进制
uint32_t mask = 0xFFU;          // U 后缀
if (x == y) { }                 // 比较
for (;;) { }                    // 显式无限循环
```

### ARM64 内存屏障

```c
barrier();          // DMB ISH - 数据内存屏障
barrier_load();     // DMB ISHLD - 加载屏障
barrier_store();    // DMB ISHST - 存储屏障
barrier_inst();     // ISB - 指令同步
full_barrier();     // DMB + ISB - 完整屏障
```

### POSIX 错误码

```c
// 系统调用模式
long sys_read(int fd, void *buf, size_t count) {
    if (buf == NULL) {
        return -EINVAL;  // 返回负错误码
    }
    // ...
    return bytes_read;   // 成功时返回字节数
}
```

---

## 版本历史

- **v1.0** (2025-01-09): 初始文档结构
  - 从单体 CLAUDE.md 提取
  - 模块化为 16 个专门的规则文件

---

## 联系与贡献

如有问题或贡献意向，请参考 `docs/` 中的项目文档。

**记住**：安全第一。MISRA 合规没有捷径。每行代码都必须可追溯、可测试、可验证。

---

## 语言规范

**所有代码注释、文档、规则文件必须使用中文。**

详细要求请参阅 [核心原则 - 语言规范要求](rules/core-principles.md#73-语言规范要求)。
