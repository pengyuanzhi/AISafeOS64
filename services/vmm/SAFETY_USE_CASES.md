# 安全用例（Safety Use Cases）

**项目**: AISafeOS64 虚拟机监控器 (VMM)
**版本**: 1.0
**日期**: 2026-05-04
**安全等级**: ASIL-D

---

## 📊 安全用例清单

| ID | 安全用例 | ASIL | 状态 |
|----|---------|------|------|
| SC-01 | VM 创建安全性 | ASIL-D | ✅ |
| SC-02 | VM 启动安全性 | ASIL-D | ✅ |
| SC-03 | VM 停止安全性 | ASIL-B | ✅ |
| SC-04 | VM 暂停/恢复安全性 | ASIL-B | ✅ |
| SC-05 | vCPU 调度安全性 | ASIL-D | ✅ |
| SC-06 | vCPU 上下文切换安全性 | ASIL-D | ✅ |
| SC-07 | 中断注入安全性 | ASIL-D | ✅ |
| SC-08 | 资源竞争安全性 | ASIL-D | ✅ |

---

## 🎯 安全用例详情

### SC-01: VM 创建安全性 ✅

**安全目标**: VM 创建时必须验证资源分配

**ASIL**: ASIL-D

**前置条件**:
- VM 表不为空
- 内存管理器已初始化

**输入**:
- `vm_desc_t *vm`: VM 描述符指针
- `kernel_config_t *config`: VM 配置

**预期输出**:
- 返回 `KERNEL_OK`: VM 创建成功
- 返回 `-(int32_t)ENOBUFS`: 资源不足

**测试步骤**:
1. 调用 `vmm_vm_create()`
2. 验证 VM 创建成功
3. 验证资源分配正确
4. 验证 VM 状态为 `VM_STATE_STOPPED`

**测试覆盖**: test_integration_vm.c: test_vm_create

---

### SC-02: VM 启动安全性 ✅

**安全目标**: VM 启动时必须验证配置

**ASIL**: ASIL-D

**前置条件**:
- VM 已创建
- VM 已配置

**输入**:
- `vm_desc_t *vm`: VM 描述符指针

**预期输出**:
- 返回 `KERNEL_OK`: VM 启动成功
- 返回 `-(int32_t)EINVAL`: 配置无效

**测试步骤**:
1. 调用 `vmm_vm_start()`
2. 验证 VM 启动成功
3. 验证配置正确
4. 验证 VM 状态为 `VM_STATE_RUNNING`

**测试覆盖**: test_integration_vm.c: test_vm_start

---

### SC-03: VM 停止安全性 ✅

**安全目标**: VM 停止时必须释放所有资源

**ASIL**: ASIL-B

**前置条件**:
- VM 已创建
- VM 已启动

**输入**:
- `vm_desc_t *vm`: VM 描述符指针

**预期输出**:
- 返回 `KERNEL_OK`: VM 停止成功
- 返回 `-(int32_t)EINVAL`: 参数无效

**测试步骤**:
1. 调用 `vmm_vm_stop()`
2. 验证 VM 停止成功
3. 验证所有资源已释放
4. 验证 VM 状态为 `VM_STATE_STOPPED`

**测试覆盖**: test_integration_vm.c: test_vm_stop

---

### SC-04: VM 暂停/恢复安全性 ✅

**安全目标**: VM 暂停时必须保存状态

**ASIL**: ASIL-B

**前置条件**:
- VM 已创建
- VM 已启动

**输入**:
- `vm_desc_t *vm`: VM 描述符指针

**预期输出**:
- 返回 `KERNEL_OK`: VM 暂停/恢复成功
- 返回 `-(int32_t)EINVAL`: 参数无效

**测试步骤**:
1. 调用 `vmm_vm_pause()`
2. 验证 VM 暂停成功
3. 验证状态已保存
4. 验证 VM 状态为 `VM_STATE_PAUSED`
5. 调用 `vmm_vm_resume()`
6. 验证 VM 恢复成功
7. 验证状态已恢复
8. 验证 VM 状态为 `VM_STATE_RUNNING`

**测试覆盖**: test_integration_vm.c: test_vm_pause, test_vm_resume

---

### SC-05: vCPU 调度安全性 ✅

**安全目标**: vCPU 调度必须避免饥饿

**ASIL**: ASIL-D

**前置条件**:
- VM 已创建
- vCPU 已创建

**输入**:
- `vm_desc_t *vm`: VM 描述符指针
- `vcpu_desc_t *vcpu`: vCPU 描述符指针

**预期输出**:
- 返回 `KERNEL_OK`: vCPU 调度成功
- 返回 `-(int32_t)EINVAL`: 参数无效

**测试步骤**:
1. 调用 `vmm_vcpu_schedule()`
2. 验证 vCPU 调度成功
3. 验证调度器公平性
4. 验证 vCPU 状态为 `VCPU_STATE_RUNNING`
5. 多次调度多个 vCPU，验证无饥饿

**测试覆盖**: test_integration_vcpu.c: test_vcpu_schedule

---

### SC-06: vCPU 上下文切换安全性 ✅

**安全目标**: 上下文切换必须保存/恢复所有寄存器

**ASIL**: ASIL-D

**前置条件**:
- VM 已创建
- 多个 vCPU 已创建

**输入**:
- `vm_desc_t *vm`: VM 描述符指针
- `vcpu_desc_t *vcpu_from`: 源 vCPU
- `vcpu_desc_t *vcpu_to`: 目标 vCPU

**预期输出**:
- 返回 `KERNEL_OK`: 上下文切换成功
- 返回 `-(int32_t)EINVAL`: 参数无效

**测试步骤**:
1. 调度 vCPU 0
2. 验证 vCPU 0 状态为 `VCPU_STATE_RUNNING`
3. 切换到 vCPU 1
4. 验证 vCPU 1 状态为 `VCPU_STATE_RUNNING`
5. 验证 vCPU 0 状态为 `VCPU_STATE_READY`
6. 切换回 vCPU 0
7. 验证 vCPU 0 状态为 `VCPU_STATE_RUNNING`
8. 验证 vCPU 1 状态为 `VCPU_STATE_READY`
9. 验证所有寄存器已保存/恢复

**测试覆盖**: test_integration_vcpu.c: test_vcpu_context_switch

---

### SC-07: 中断注入安全性 ✅

**安全目标**: 中断注入必须保证投递

**ASIL**: ASIL-D

**前置条件**:
- VM 已创建
- vCPU 已创建
- VGIC 已初始化

**输入**:
- `uint32_t vm_id`: VM ID
- `uint32_t vcpu_id`: vCPU ID
- `uint32_t irq`: 中断号

**预期输出**:
- 返回 `KERNEL_OK`: 中断注入成功
- 返回 `-(int32_t)EINVAL`: 参数无效

**测试步骤**:
1. 调用 `vmm_inject_irq()`
2. 验证中断注入成功
3. 验证中断已投递到目标 vCPU
4. 验证中断状态正确
5. 多次注入多个中断，验证无丢失

**测试覆盖**: test_vgic_dist.c: test_gicd_sgir_sgi_inject

---

### SC-08: 资源竞争安全性 ✅

**安全目标**: 资源竞争必须通过锁机制保护

**ASIL**: ASIL-D

**前置条件**:
- VM 已创建
- 多个 vCPU 已创建

**输入**:
- 多个 vCPU 并发访问共享资源

**预期输出**:
- 无死锁
- 无数据损坏
- 无竞态条件

**测试步骤**:
1. 创建多个 vCPU
2. 并发调度多个 vCPU
3. 多个 vCPU 并发访问共享资源
4. 验证无死锁
5. 验证无数据损坏
6. 验证无竞态条件
7. 验证性能统计正确

**测试覆盖**: test_stress_multiple_vms.c: test_stress_resource_competition

---

## 📊 安全用例统计

### 按模块统计

| 模块 | 安全用例数 | ASIL-D | ASIL-B | ASIL-A | QM |
|------|-----------|--------|--------|--------|-----|
| VM 管理 | 4 | 2 | 2 | 0 | 0 |
| vCPU 管理 | 2 | 2 | 0 | 0 | 0 |
| 中断管理 | 1 | 1 | 0 | 0 | 0 |
| 资源管理 | 1 | 1 | 0 | 0 | 0 |
| **总计** | **8** | **6** | **2** | **0** | **0** |

---

## ✅ 验收标准

| 标准 | 状态 | 说明 |
|------|------|------|
| 安全用例编写 | ✅ | 8 个安全用例编写完成 |
| 安全目标覆盖 | ✅ | 所有安全目标都有对应用例 |
| ASIL 分配 | ✅ | ASIL-D 分配完成 |
| 测试覆盖 | ✅ | 所有安全用例都有对应测试 |

---

## 📊 总结

### 总体情况

| 项目 | 结果 | 说明 |
|------|------|------|
| 安全用例编写 | ✅ 完成 | 8 个安全用例编写完成（6 个 ASIL-D，2 个 ASIL-B） |
| 安全目标覆盖 | ✅ 完成 | 所有安全目标都有对应用例 |
| ASIL 分配 | ✅ 完成 | ASIL-D 分配完成 |
| 测试覆盖 | ✅ 完成 | 所有安全用例都有对应测试 |
| **总体状态** | **✅** | **安全用例编写完成** |

---

## 🎉 安全用例编写完成

**安全用例编写** 已全部完成！

**完成情况**:
- ✅ 8 个安全用例编写完成（6 个 ASIL-D，2 个 ASIL-B）
- ✅ 所有安全目标都有对应用例
- ✅ 所有安全用例都有对应测试

**总计**: 8 个安全用例，安全用例编写完成。

---

**报告生成时间**: 2026-05-04 12:35 (GMT+8)
**作者**: AISafe64 编程助手 (Kernel)
**项目**: AISafeOS64 虚拟机监控器 (VMM)
**版本**: 1.0
**安全等级**: ASIL-D
**状态**: ✅ 安全用例编写完成
