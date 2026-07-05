# AGENTS.md - AISafeOS64 微内核开发规则

**项目**: AISafeOS64 — 64 位微内核实时操作系统（ISO 26262 ASIL-D / IEC 61508 SIL-4）
**构建**: `cd build && make -j4`
**验证**: `qemu-system-aarch64 -M virt -cpu cortex-a57 -nographic -kernel build/kernel/aisafe64.elf.elf -m 512M -smp 1`

---

## 一、代码规范

- 语言: C11（freestanding），MISRA C:2012
- 缩进: 4 空格，Allman 括号
- 行宽: ≤120 字符
- 圈复杂度: ≤10
- 递归: 禁止（MISRA Rule 17.2）
- VLA: 禁止（MISRA Rule 18.8）
- 命名: `snake_case`（函数/变量），`UPPER_SNAKE`（宏），`PascalCase_t`（typedef）
- 前缀: `s_`（文件静态），`g_`（全局），`hal_`（HAL 接口）
- 错误码: 统一 `-(int32_t)EXXX`（如 `-(int32_t)EINVAL`），禁止裸负数
- 内核 text 段: < 50KB

## 二、架构分层

内核核心（`kernel/` 非 `arch/`）必须体系架构无关：
- 禁止 `__asm__`、`msr`、`mrs`、`wfe`、`tlbi`、`dmb` 等指令
- 所有硬件操作通过 HAL 接口（`hal.h`、`hal_irq.h`）
- 中断控制器通过 `hal_irq_*` 接口，不直接调 GIC
- 控制台通过 `hal_console_putc/puts`，不暴露 UART base

## 三、TDD

- RED → GREEN → REFACTOR
- 每个 `feat`/`fix` 提交必须包含对应测试
- 核心模块覆盖率 > 80%，新增代码 > 90%
- 提交前: 宿主机 `gcc` 测试全通过 + QEMU 验证

## 四、自动提交

- 每个任务完成立即提交（不累积）
- Conventional Commits: `<type>(<scope>): <中文描述>`
- scope: `mm`/`scheduler`/`ipc`/`arch`/`kernel` 等
- 禁止提交构建产物（`build/`、`*.dis`、`*.bin`）
- 提交前验证: 编译无警告 + QEMU 正常 + text < 50KB

## 五、禁止折中方案

- 每个设计决策必须是最优解，禁止过渡/临时方案
- 遇到问题深入调试到根因，禁止打补丁
- 禁止 TODO/后续完善/暂时回退
- 禁止 demand paging（实时系统需 WCET 可分析）
- 禁止硬编码物理地址/魔法数字

## 六、实时性保证

- 关中断区间 ≤ 10μs
- 临界区 ≤ 50 行代码
- 调度延迟 ≤ 1 tick（1ms）
- 中断中禁止: kmalloc、schedule、klog、UART 打印
- 实时路径禁止动态内存分配
- 禁止无界循环（必须有超时/计数器退出）
- 禁止优先级反转（必须用优先级继承）
- preempt_disable 保护 per-CPU 数据（不关中断）

## 七、确定性约束

- 相同输入产生相同执行路径
- 禁止 ASLR/随机化
- 初始化顺序固定
- 禁止依赖时序的并发逻辑

## 八、可靠性与容错

- EL0 异常: 终止线程，系统继续运行
- EL1 异常: 内核 panic（打印诊断 + 死循环）
- 线程终止: 必须释放全部资源（栈/CSpace/vmspace/端点）
- 内存不足: 返回 ENOMEM，不 crash
- panic 后禁止继续执行

## 九、并发与锁正确性

锁优先级（从高到低）:
1. per-CPU 就绪队列锁
2. CSpace 锁
3. 端点锁
4. 通道锁
5. 定时器/睡眠队列锁
6. kmalloc/物理内存锁

- 获取多锁必须按上述顺序
- 持锁时禁止: kmalloc、schedule、context_switch、UART
- 自旋锁持锁 < 1μs
- 中断上下文的锁用 irqsave 版本
- per-CPU 数据本核访问用 preempt_disable（不用锁）

## 十、内存安全

- 所有用户指针必须 `access_ok` 验证
- 禁止 use-after-free（释放后置 NULL）
- 禁止 double-free
- 禁止 strcpy/sprintf/gets（用有界版本）
- 禁止裸指针算术跨边界
- capability/endpoint 用 generation 防 UAF

## 十一、多核性能设计

优先级: per-CPU 独占数据 > per-CPU freelist > RCU > 细粒度锁 > 全局锁

- per-CPU 数据本核访问不加锁（用 preempt_disable）
- 热点路径禁止全局锁
- per-CPU 数组必须 CACHE_ALIGN(64)
- 频繁修改与只读字段分到不同 cache 行

## 十二、日志规范

- 接口: `klog_error/klog_warn/klog_info/klog_debug`（异步环形缓冲）
- **禁止在中断中调用任何 klog 函数**（唯一例外: klog_panic）
- 生产模式: 只输出 ERROR + WARN
- panic 路径: klog_panic 直接同步输出

## 十三、注释规范

- **所有函数（含 static 内部）必须有完整 Doxygen 头注释**
- 公共函数: @brief/@details/@param/@return/@note
- 内部函数: @brief/@details（含前置条件）
- 文件头: @revision history
- 注释描述"为什么"，不只描述"做了什么"
- 禁止内联修订注释（放文件头 @revision history）

## 十四、硬件资源管理

- 所有 MMIO 地址/IRQ 号集中在 HAL 层（单一真源）
- 禁止驱动/服务代码硬编码物理地址
- 内核核心不感知 UART base/GIC 地址
