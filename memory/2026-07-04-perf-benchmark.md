# 2026-07-04 内核性能基准测试（QEMU 实测）✅

## 完成工作

### P1-3: 内核性能 QEMU 实测

新增内核微基准测试（`kern_bench`），在 QEMU virt (cortex-a57, 62.5MHz) 实测
内核基础操作延迟，为 IPC 和调度优化提供基准。

### QEMU 实测性能数据

| 操作 | 延迟 (cycles) | 延迟 (ns @62.5MHz) |
|------|--------------|-------------------|
| hal_timer_get_count | 3 | ~48 |
| ticket_lock acquire+release (无竞争) | 3 | ~48 |
| kmalloc+kfree(64) | 5 | ~80 |
| IPC send+recv+reply | 跳过（IPC 子系统 slab 初始化失败） |

### 关键发现

1. **内核启动序列缺少内存子系统初始化**：
   - kernel_main 未调用 kmalloc_init()
   - 已修复：在 timer_init 后添加 kmalloc_init()
   - IPC 子系统初始化（ipc_endpoint_subsys_init）仍失败（slab_create 内部问题）
   - kmalloc(4096) 和 slab_create(4096) 单独测试均成功，但 IPC 的多 slab 创建失败
   - 根因待查（可能是连续 kmalloc 的碎片化或 slab 子系统初始化顺序问题）

2. **性能数据质量**：
   - QEMU 模拟的 cortex-a57 @62.5MHz，数据反映指令数级别开销
   - 真实硬件数据会不同，但相对比例有参考价值
   - timer_get_count 和 ticket_lock 都是 3 cycles，说明 QEMU 的开销建模一致

### 实现细节

新增代码（entry.c）：
- `kern_bench_client`：基准测试线程（timer/lock/kmalloc/IPC 计时）
- `ipc_bench_server`：IPC server 线程（receive+reply 循环）
- `kern_bench_start`：创建 bench 线程
- kernel_main 新增 kmalloc_init 和 IPC 子系统初始化调用

### 后续任务

1. **修复 IPC 子系统 slab 初始化**（独立 bug）
2. **添加上下文切换延迟测试**（需要完善从核 schedule 路径）
3. **在真实硬件验证性能数据**（QEMU 数据仅供参考）
