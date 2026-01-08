# AISafe64 RTOS - 单元测试

## 测试框架概述

本目录包含AISafe64 RTOS的单元测试。测试框架是轻量级的，专为嵌入式环境设计，不依赖任何外部测试库。

## 测试结构

```
tests/
├── test_framework.h      # 测试框架头文件
├── test_framework.c      # 测试框架实现
├── test_atomic.c         # 原子操作单元测试
├── CMakeLists.txt        # CMake构建文件
├── Makefile              # Makefile构建文件
└── README.md             # 本文件
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

2. **并发测试**
   - 当前测试是单线程的
   - 原子操作的并发正确性需要实际多核环境验证

3. **实时性测试**
   - 嵌入式RTOS特性需要硬件在环测试

## 未来改进

- [ ] 添加模拟器支持（QEMU）
- [ ] 添加性能基准测试
- [ ] 添加压力测试
- [ ] 添加多线程并发测试
- [ ] 集成CI/CD自动化测试

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
