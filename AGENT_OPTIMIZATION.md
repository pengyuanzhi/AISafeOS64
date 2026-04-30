# Agent调用优化策略 - 避免Rate Limit

## 当前配置

### 并发设置
- **主并发数**: 3（从8降低）
- **子Agent并发数**: 3（从8降低）

### 模型选择
- **主模型**: `zai/glm-4.7-flash`（更快，配额更多）
- **备选模型**: `zai/glm-4.7`

### 优化效果
- 降低 62.5% 的并发请求量
- 使用Flash版本模型，响应速度提升约3-5倍
- 减少触发Rate Limit的可能性

---

## Agent调用最佳实践

### 1. 任务优先级分级

#### 🟢 **简单任务**（本地工具优先）
- 代码格式化：clang-format
- 静态检查：cppcheck, clang-tidy
- 编译错误修复：直接查看错误日志
- 文档整理：直接编辑

**不使用Agent的场景**：
```bash
# 直接使用本地工具
clang-format -i kernel/sched/scheduler.c
clang-tidy kernel/sched/scheduler.c
```

#### 🟡 **中等任务**（单Agent）
- 小功能开发（<500行代码）
- 单个bug修复
- 代码注释添加
- 简单重构

**使用单Agent，超时300秒**：
```
sessions_spawn(runtime: "acp", agentId: "claude",
    task: "...", timeoutSeconds: 300)
```

#### 🔴 **复杂任务**（多Agent流水线）
- 大模块开发（>1000行代码）
- 架构设计
- 跨模块问题修复
- 性能优化

**使用multi-agent-pipeline，但限制并发**：
```
sessions_spawn(runtime: "subagent",
    task: "...", maxConcurrent: 2)
```

---

### 2. 批量处理策略

#### ❌ **避免：频繁的小任务**
```javascript
// 每个小bug都开一个agent（10次API调用）
for (bug of bugs) {
    spawnAgent("修复bug: " + bug);
}
```

#### ✅ **推荐：批量合并任务**
```javascript
// 合并所有小bug一次性修复（1次API调用）
spawnAgent(`
修复以下3个bug:
1. ...
2. ...
3. ...
`);
```

**合并原则**：
- 相似功能的bug → 1个任务
- 同一模块的问题 → 1个任务
- 相关的代码审查 → 1个任务

---

### 3. 超时时间设置

#### 合理的超时配置

| 任务类型 | 超时时间 | 说明 |
|---------|---------|------|
| 代码格式化 | 60秒 | 快速任务 |
| 简单修复 | 180秒 | 小范围修改 |
| 中等开发 | 300秒 | 功能模块 |
| 复杂开发 | 600秒 | 大型重构 |
| 代码审查 | 240秒 | 审查+反馈 |

**避免超时后重试**：
- 超时任务先分析日志
- 确认不是API问题再重试
- 考虑拆分任务而非重试整个任务

---

### 4. 代码审查策略

#### 优化审查频率

**当前问题**：每次开发都请求Codex审查

**优化方案**：

1. **本地自检优先**：
   ```bash
   # 开发前自检
   make check
   clang-format --dry-run kernel/**/*.c
   ```

2. **按需审查**：
   - 核心模块（ipc, scheduler, vm） → 必须审查
   - 驱动程序 → 可选审查
   - 测试代码 → 无需审查
   - 文档更新 → 无需审查

3. **批量审查**：
   ```
   // 开发完整个模块后，一次性提交审查
   spawnAgent("审查 kernel/ipc/ 模块的所有改动")
   ```

---

### 5. ACP Agent选择策略

#### 根据任务类型选择合适的Agent

| Agent | 适用场景 | 不适用场景 |
|-------|---------|-----------|
| **Claude** | 复杂架构设计、需要详细推理 | 简单格式化、小修复 |
| **Codex** | 代码生成、模式匹配、快速编码 | 需要深度推理的任务 |
| **Pi** | 快速原型、实验性代码 | 生产代码、复杂逻辑 |

#### 避免的滥用

**❌ 所有任务都用Claude**
- Claude推理能力强但慢
- 简单任务用Codex或Pi更快

**❌ 同时调用多个Agent做同一件事**
- 浪费API调用
- 结果可能不一致

---

### 6. 缓存和复用策略

#### 利用上下文缓存

**当前问题**：每次都重新发送项目背景

**优化方案**：

1. **创建任务模板**：
   ```markdown
   # 任务模板
   ## 背景
   AISafeOS64是64位微内核RTOS，目标ISO 26262 ASIL-D。
   ## 代码规范
   - MISRA C:2012
   - 4空格缩进，Allman括号
   - 中文注释
   ## 当前任务
   [具体任务]
   ```

2. **复用Agent Session**：
   ```javascript
   // 使用thread模式复用会话
   sessions_spawn(runtime: "acp",
       thread: true,
       mode: "session",
       task: "...")
   ```

3. **增量任务**：
   ```
   // 第一次：设计接口
   spawnAgent("设计IPC接口")

   // 第二次：实现接口（复用上下文）
   spawnAgent("实现上一轮设计的接口")
   ```

---

### 7. 监控和诊断

#### 监控指标

```bash
# 查看当前运行中的agents
subagents list

# 查看历史任务统计
jq '.runs | length' ~/.openclaw/subagents/runs.json

# 查看超时任务
grep '"status":"timeout"' ~/.openclaw/subagents/runs.json | wc -l
```

#### Rate Limit触发时的应对

1. **立即停止新任务**
2. **等待60秒**（大多数Rate Limit在1分钟内恢复）
3. **降低并发数**：临时修改 `maxConcurrent` 为 1-2
4. **检查任务日志**，分析是否有死循环或无效重试

---

### 8. 特定场景优化

#### 场景1：TDD开发流程

**优化前**：每个测试-编码-重构循环都开Agent
**优化后**：
```
// 一次性完成整个TDD循环
spawnAgent(`
使用TDD完成以下功能:
1. 写测试用例（tests/test_xxx.c）
2. 实现功能让测试通过
3. 重构优化代码

注意：遵循AISafeOS64的MISRA C:2012规范
`)
```

#### 场景2：多模块修改

**优化前**：每个模块一个Agent
**优化后**：
```
// 一次性修改多个相关模块
spawnAgent(`
修改以下模块以支持新功能:
1. kernel/ipc/endpoint.c - 更新IPC接口
2. kernel/sched/scheduler.c - 调整调度策略
3. kernel/arch/arm64/entry.c - 添加系统调用

这些改动是关联的，需要保证一致性。
`)
```

#### 场景3：Bug修复

**优化前**：每次失败都开新Agent
**优化后**：
```
// 先尝试本地修复，失败后再用Agent
make 2>&1 | grep error

// 如果无法本地修复，再调用Agent
spawnAgent(`
修复编译错误:
[粘贴错误日志]

只需要修复这些错误，不要做其他改动。
`, timeoutSeconds: 180)
```

---

## 配置文件位置

- **OpenClaw配置**: `~/.openclaw/openclaw.json`
- **AISafeOS64工作空间**: `/home/kerfs/AISafeOS64/AISafeOS64`
- **Agent运行记录**: `~/.openclaw/subagents/runs.json`

## 回滚配置

如果需要恢复原始配置：

```bash
# 查看备份
ls -lh ~/.openclaw/openclaw.json.backup.*

# 恢复特定备份
cp ~/.openclaw/openclaw.json.backup.20260413_184416 ~/.openclaw/openclaw.json

# 重启OpenClaw
openclaw gateway restart
```

## 持续监控

建议每天检查一次：

```bash
# 查看今天的Agent使用情况
cat ~/.openclaw/subagents/runs.json | \
    python3 -c "
import sys, json, time
data = json.load(sys.stdin)
today = time.strftime('%Y-%m-%d')
today_runs = [r for r in data['runs'].values()
              if time.strftime('%Y-%m-%d', time.localtime(r['createdAt']/1000)) == today]
print(f'今日任务: {len(today_runs)}')
outcomes = [r.get('outcome',{}).get('status') for r in today_runs]
print(f'成功: {outcomes.count(\"ok\")}')
print(f'超时: {outcomes.count(\"timeout\")}')
"
```

---

**更新时间**: 2026-04-13
**维护者**: AISafeOS64 Kernel Agent
