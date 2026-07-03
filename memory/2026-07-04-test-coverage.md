# 2026-07-04 测试覆盖率核实 ✅

## P1-5: 测试覆盖率现状

### 覆盖率实测结果（gcov）

| 模块 | 测试方式 | 覆盖率 | 备注 |
|------|---------|--------|------|
| **kmalloc.c** | test_kmalloc_kernel（链接真实代码） | **89.8%** (106/118 行) | 首个实测覆盖率 |
| slab.c | test_slab（自包含 mock） | N/A | 不链接真实代码 |
| scheduler.c | test_scheduler（mock_kernel.h） | N/A | 不链接真实代码 |
| ipc/endpoint.c | test_endpoint（mock） | N/A | 不链接真实代码 |
| 其他内核模块 | 各种 mock 测试 | N/A | 无法 gcov |

### kmalloc.c 未覆盖行分析（12 行）

未覆盖的 12 行都是边界条件/防御性代码：
- NULL 参数 early return（行 129, 172）
- 空闲块合并的特定路径（行 148, 206）
- 循环 break 条件（行 191, 197, 226, 232）

这些是低频路径，89.8% 覆盖率对于核心分配器是可接受的。

### 测试架构现状

项目有两种测试：
1. **宿主机自包含测试**（20 个）：用 mock_kernel.h 或自带实现，
   不链接真实内核代码，无法测覆盖率
2. **内核实现测试**（1 个：test_kmalloc_kernel）：链接真实 kmalloc.c，
   可用 gcov 测覆盖率

### 提升覆盖率的建议

1. **新增内核实现测试**：参考 test_kmalloc_kernel 模式，
   为 slab、scheduler、ipc 等核心模块创建链接真实代码的测试
2. **关键模块目标覆盖率**：
   - kmalloc/slab（内存）：>90%（当前 89.8%）
   - scheduler（调度）：>85%
   - ipc（通信）：>85%
3. **CI 集成**：在 run_all_tests.sh 中添加 gcov 编译选项，
   自动生成覆盖率报告

### gcov 使用方法

```bash
# 编译（带覆盖率）
gcc -std=c11 -fprofile-arcs -ftest-coverage -DTEST_HOST_MODE \
    -Iinclude -Itests -o build/gcov/test_kmalloc_kernel \
    tests/test_kmalloc_kernel.c kernel/mm/kmalloc.c lib/kernel_string.c

# 运行
./build/gcov/test_kmalloc_kernel

# 生成报告（从项目根目录）
gcov --object-directory build/gcov kernel/mm/kmalloc.c

# 查看
cat kmalloc.c.gcov  # 逐行覆盖率
```
