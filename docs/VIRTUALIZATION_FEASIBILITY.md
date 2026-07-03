# 虚拟化（EL2）可行性评估

**评估日期**: 2026-07-04
**结论**: 真 EL2 虚拟化可行但工作量大（4-8 周），建议延后到内核核心稳固后

## 现状

### 内核 EL 状态
- 内核启动时检测 CurrentEL，如果 EL2/EL3 则**主动降级到 EL1**
- boot.S 的 `.Ldrop_from_el2` 清除 HCR_EL2，eret 到 EL1
- QEMU virt 提供了 EL2（HVC 被正确捕获处理为 PSCI）

### vmm 模块现状
- 11K 行用户态代码 + 5K 行测试
- **纯用户态模拟**，无真 EL2 操作（vttbr/hcr_el2/vcpu el2 切换）
- 未被编译进任何运行时目标（vmm.elf 不存在）
- 代码结构完整（vm/vcpu/npt/vgic/device/events）但需移植到内核 EL2

## 真 EL2 虚拟化所需工作

### 架构变更（高难度）
1. **内核保持在 EL2**：修改 boot.S，不降级到 EL1，启用 VHE（HCR_EL2.E2H=1）
2. **系统寄存器重定向**：VHE 模式下系统寄存器访问需配置（HCR_EL2.TGE 等）
3. **双阶段页表**：
   - Stage-1（EL2→物理）：现有内核页表
   - Stage-2（Guest 物理→物理）：VTTBR_EL2，需实现 VTCR_EL2 配置
4. **vCPU 上下文切换**：EL1 guest↔EL2 hypervisor 的 eret 切换

### 具体实现项（中等工作量）
1. NPT（Nested Page Table）/ Stage-2 页表管理（services/vmm/npt/ 已有骨架）
2. vGIC（虚拟 GIC）（services/vmm/vgic/ 已有骨架）
3. VirtIO 设备模拟（services/vmm/device/ 已有骨架）
4. VM 生命周期管理（services/vmm/core/ 已有骨架）
5. Guest OS 加载和启动

### 测试验证
- QEMU `-machine virt,virtualization=on` 启用 EL2
- 需要一个 Guest OS 镜像（可以是简单的 bare-metal hello world）

## 推荐策略

### 短期（不建议现在做）
真 EL2 虚拟化风险高、工作量大，且当前内核核心子系统（IPC、调度、内存）
仍在稳固中。建议延后。

### 中期（内核核心完善后）
1. 先做 VHE 模式启动验证（内核在 EL2 运行的 hello world）
2. 再实现 Stage-2 页表和 vCPU 切换
3. 最后移植 vmm 用户态代码到内核 EL2

### 替代方案
如果虚拟化不是硬需求，可以：
- 用容器/命名空间隔离（用户态进程隔离）
- 用 VMM 作为用户态服务（Type-2 hypervisor 模拟，当前 vmm 代码方向）

## 工作量估算

| 阶段 | 内容 | 估算 |
|------|------|------|
| 1 | VHE 启动 + EL2 内核运行 | 1-2 周 |
| 2 | Stage-2 页表 + vCPU 切换 | 2-3 周 |
| 3 | vGIC + 设备模拟 | 2-3 周 |
| 4 | 测试和文档 | 1 周 |
| **总计** | | **6-9 周** |
