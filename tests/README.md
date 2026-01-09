# AISafe64 RTOS - 单元测试

## 测试框架概述

本目录包含AISafe64 RTOS的单元测试和集成测试。测试框架是轻量级的，专为嵌入式环境设计，不依赖任何外部测试库。

## 测试结构

```
tests/
├── test_framework.h         # 测试框架头文件
├── test_framework.c         # 测试框架实现
├── test_atomic.c            # 原子操作单元测试
├── test_sched.c             # 调度器单元测试
├── test_task_integration.c  # 任务管理集成测试
├── CMakeLists.txt           # CMake构建文件
└── README.md                # 本文件
```

## 测试框架功能

### 断言宏

| 宏 | 描述 |
|---|---|
| `TEST_ASSERT_EQ(actual, expected)` | 相等断言 |
| `TEST_ASSERT_NE(actual, unexpected)` | 不等断言 |
| `TEST_ASSERT_GT(actual, min)` | 大于断言 |
| `TEST_ASSERT_LT(actual, max)` | 小于断言 |
| `TEST_ASSERT_TRUE(condition)` | 真值断言 |
| `TEST_ASSERT_FALSE(condition)` | 假值断言 |
| `TEST_ASSERT_NULL(ptr)` | NULL指针断言 |
| `TEST_ASSERT_NOT_NULL(ptr)` | 非NULL指针断言 |
| `TEST_ASSERT_EQ_PTR(actual, expected)` | 指针相等断言 |

### 测试套件宏

```c
// 定义测试用例
TEST_CASE(test_name) {
    // 测试代码
    TEST_ASSERT_EQ(value, expected);
}

// 测试套件
TEST_SUITE_START(suite_name)
{
    TEST_RUN(test_name1);
    TEST_RUN(test_name2);
}
TEST_SUITE_END()
```

## 如何运行测试

### 方法1：使用Makefile（推荐）

```bash
cd tests
make
make run
```

### 方法2：使用CMake

```bash
mkdir build && cd build
cmake ..
make
./test_atomic
```

### 方法3：手动编译

```bash
cd tests
gcc -Wall -Wextra -Werror -O2 -g \
    -I../src/include -I../src/arch/arm64/include -I. \
    -DTEST_MODE=1 \
    test_framework.c test_atomic.c -o test_atomic
./test_atomic
```

## 测试覆盖范围

### 当前测试模块

#### 1. 原子操作测试 (`test_atomic.c`)

完整的原子操作单元测试。

- **32位原子操作**
  - `atomic_add_u32` - 原子加法
  - `atomic_sub_u32` - 原子减法
  - `atomic_inc_u32` - 原子自增
  - `atomic_dec_u32` - 原子自减
  - `atomic_xchg_u32` - 原子交换
  - `atomic_cas_u32` - 原子CAS操作
  - `atomic_compare_exchange_strong_u32` - 强CAS

- **64位原子操作**
  - `atomic_add_u64` - 64位原子加法
  - `atomic_inc_u64` - 64位原子自增
  - `atomic_cas_u64` - 64位CAS操作

- **Fetch操作**
  - `atomic_inc_fetch_u32` - 自增并返回新值
  - `atomic_dec_fetch_u32` - 自减并返回新值
  - `atomic_add_fetch_u32` - 加法并返回新值
  - `atomic_sub_fetch_u32` - 减法并返回新值

- **位操作**
  - `atomic_test_and_set_u32` - 测试并设置位
  - `atomic_test_and_clear_u32` - 测试并清除位
  - `atomic_test_and_toggle_u32` - 测试并翻转位

- **锁操作**
  - `atomic_acquire_lock` - 获取锁
  - `atomic_release_lock` - 释放锁
  - `atomic_set_flag` - 设置标志
  - `atomic_clear_flag` - 清除标志

- **内存屏障**
  - `MEMORY_BARRIER` - 完整内存屏障
  - `COMPILER_BARRIER` - 编译器屏障
  - `WMB` / `RMB` / `SMB` - 写/读/系统屏障
  - `WFE` / `SEVL` - ARMv8-A WFE/SEV指令

#### 2. 调度器单元测试 (`test_sched.c`)

调度器各调度类的单元测试：

- **FIFO调度器测试**
  - 入队/出队操作
  - 优先级排序（256个优先级级别）
  - 抢占测试

- **RR（Round Robin）调度器测试**
  - 时间片切片（10ms）
  - 任务轮转
  - 时间片过期处理

- **CFS（完全公平调度器）测试**
  - vruntime跟踪
  - 基于vruntime的任务选择
  - 抢占条件测试

- **EDF（最早截止时间优先）调度器测试**
  - 基于截止时间的调度
  - 截止时间错过检测
  - 动态截止时间处理

- **IDLE调度器测试**
  - 空闲任务基本操作
  - 最低优先级处理

- **统计测试**
  - 调度器统计信息获取
  - 运行时统计验证

#### 3. 任务管理集成测试 (`test_task_integration.c`)

任务管理API的集成测试：

- **调度器初始化**
  - 调度器基本初始化
  - 空闲任务创建

- **任务创建**
  - 基本任务创建
  - 多任务创建
  - 不同优先级任务
  - 不同调度类任务

- **任务管理**
  - 获取当前任务
  - 任务优先级设置
  - 任务统计信息
  - 统计一致性验证

- **运行队列操作**
  - CPU运行队列访问
  - 运行队列锁定
  - 队列状态查询

- **工具函数测试**
  - 优先级位图查找
  - 优先级到权重转换

## 测试输出示例

```
========================================
     AISafe64 RTOS Unit Tests
========================================

----------------------------------------
Test Suite: atomic_u32
----------------------------------------
  [RUN] atomic_add_u32_basic
  [PASS] atomic_add_u32_basic
  [RUN] atomic_sub_u32_basic
  [PASS] atomic_sub_u32_basic
  ...

========================================
           Test Summary
========================================
Total Tests:  30
Passed:       30
Failed:       0
Skipped:      0

*** ALL TESTS PASSED ***
========================================
```

## 添加新测试

### 步骤1：创建测试文件

```c
// test_module.c
#include "test_framework.h"
#include "../path/to/module.h"

TEST_CASE(test_feature_basic)
{
    // 测试代码
    TEST_ASSERT_EQ(actual, expected);
}

TEST_CASE(test_feature_edge_case)
{
    // 测试边界情况
    TEST_ASSERT_TRUE(condition);
}
```

### 步骤2：更新Makefile或CMakeLists.txt

在相应的构建文件中添加新的测试源文件。

### 步骤3：运行测试

```bash
make run
```

## 测试最佳实践

1. **命名规范**
   - 测试用例命名：`test_<module>_<feature>`
   - 测试套件命名：使用描述性名称

2. **测试组织**
   - 每个测试用例应该独立
   - 使用TEST_SUITE_START/END组织相关测试

3. **断言使用**
   - 每个测试用例至少包含一个断言
   - 使用最合适的断言宏

4. **边界测试**
   - 测试0值、最大值、最小值
   - 测试错误情况

5. **代码覆盖率**
   - 目标：核心模块100%覆盖
   - 使用gcov/lcov生成覆盖率报告

## 已知限制

1. **主机测试**
   - 当前测试在主机上运行
   - 某些ARMv8-A特定指令在主机上无法完全测试
   - 交叉编译的测试无法在主机上直接运行

2. **并发测试**
   - 当前测试是单线程的
   - 原子操作的并发正确性需要实际多核环境验证

3. **实时性测试**
   - 嵌入式RTOS特性需要硬件在环测试
   - 调度器实时性需要QEMU或实际硬件验证

## 测试覆盖率

| 模块 | 单元测试 | 集成测试 | 状态 |
|------|----------|----------|------|
| 原子操作 | ✅ | ✅ | 完成 |
| FIFO调度器 | ✅ | ✅ | 完成 |
| RR调度器 | ✅ | ✅ | 完成 |
| CFS调度器 | ✅ | ✅ | 完成 |
| EDF调度器 | ✅ | ✅ | 完成 |
| IDLE调度器 | ✅ | ✅ | 完成 |
| 任务管理 | ✅ | ✅ | 完成 |
| 内存管理 | ⏳ | ⏳ | 待添加 |
| 同步原语 | ⏳ | ⏳ | 待添加 |
| 中断处理 | ⏳ | ⏳ | 待添加 |
| 定时器 | ⏳ | ⏳ | 待添加 |
| 系统调用 | ⏳ | ⏳ | 待添加 |
| 文件系统 | ⏳ | ⏳ | 待添加 |
| 网络栈 | ⏳ | ⏳ | 待添加 |

## 未来改进

- [ ] 添加QEMU模拟器支持
- [ ] 添加性能基准测试框架
- [ ] 添加压力测试
- [ ] 添加内存管理单元测试
- [ ] 添加同步原语测试（spinlock、mutex、semaphore）
- [ ] 添加中断处理测试
- [ ] 添加定时器管理测试
- [ ] 集成CI/CD自动化测试
- [ ] 添加代码覆盖率报告（gcov/lcov）

## 参考文档

- [AISafe64 RTOS 文档](../docs/)
- [ARMv8-A 架构参考手册](https://developer.arm.com/documentation/ddi0487/latest)
- [MISRA-C:2012 规范](https://www.misra.org.uk/MISRAHome/MISRAC2012/tabid/196/Default.aspx)

## 贡献指南

欢迎提交新的测试用例！请遵循以下指南：

1. 遵循MISRA-C:2012规范
2. 使用Allman括号风格
3. 为每个测试添加清晰的注释
4. 更新本README文档
