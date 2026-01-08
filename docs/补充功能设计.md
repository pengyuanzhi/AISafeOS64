# AISafe64 补充功能设计文档

## 文档说明

本文档是对 `plan.md` 的补充，详细设计了以下4个在原plan中未涉及但对安全关键系统重要的功能：

1. **电源管理 (Power Management)**
2. **看门狗定时器 (Watchdog Timer)**
3. **错误恢复机制 (Error Recovery)**
4. **日志系统 (Logging System)**

---

## 1. 电源管理 (Power Management)

### 1.1 功能需求

#### 1.1.1 核心需求

**PM1. CPU频率调节**
- 支持动态电压频率调节 (DVFS)
- 支持多级频率档位（至少4档）
- 频率切换延迟<100μs
- 支持用户空间配置策略

**PM2. CPU空闲状态管理**
- 支持 WFI (Wait For Interrupt) 指令
- 支持深度睡眠状态 (WFE)
- 空闲唤醒延迟<10μs
- 支持多级空闲状态（至少3级）

**PM3. 外设电源控制**
- 支持外设独立电源域
- 支持动态开关外设电源
- 外设初始化时间<1ms

**PM4. 系统级电源管理**
- 支持系统休眠 (Suspend to RAM)
- 支持系统关机
- 唤醒源配置
- 唤醒延迟<100ms

#### 1.1.2 非功能需求

**性能要求**
- 频率调节开销<1% CPU
- 空闲状态进入延迟<5μs
- 电源管理决策延迟<50μs

**可靠性要求**
- 电源状态切换成功率>99.99%
- 看门喂狗必须在状态切换期间继续
- 电压调节失败不得导致系统崩溃

**安全性要求**
- 频率调节必须考虑实时任务约束
- 禁止在关键任务执行时进入深度睡眠
- 符合ISO 26262 ASIL-D要求

### 1.2 架构设计

#### 1.2.1 系统架构

```
┌─────────────────────────────────────────────────────┐
│                 应用层                              │
│  (电源策略配置 / 性能模式选择)                       │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│              电源管理框架                            │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐          │
│  │ 策略管理器 │  │ 频率调节器 │  │ 空闲管理器 │          │
│  └──────────┘  └──────────┘  └──────────┘          │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│              硬件抽象层 (HAL)                        │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐          │
│  │CPU频率控制│  │电源域控制 │  │唤醒源管理 │          │
│  └──────────┘  └──────────┘  └──────────┘          │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│              ARMv8-A 硬件                            │
│  (PMU / GIC / Timer / 电源控制器)                    │
└─────────────────────────────────────────────────────┘
```

#### 1.2.2 数据结构设计

**CPU操作点 (Operating Performance Point, OPP)**

```c
/**
 * @brief CPU操作点结构
 * @note MISRA-C:2012合规
 */
typedef struct {
    uint32_t freq_hz;        /**< CPU频率 (Hz) */
    uint32_t voltage_mv;      /**< 电压 (mV) */
    uint32_t power_mw;       /**< 功耗 (mW) */
} OPP_t;

/* 编译时断言 */
_Static_assert(sizeof(OPP_t) == 12U,
               "OPP_t size must be 12 bytes");
```

**电源策略结构**

```c
/**
 * @brief 电源策略结构
 * @note MISRA-C:2012合规
 */
typedef struct {
    const char *name;        /**< 策略名称 */
    uint32_t flags;          /**< 策略标志 */

    /**
     * @brief 选择下一个操作点
     * @param current_load 当前CPU负载 (0-100)
     * @param opps 操作点数组
     * @param opp_count 操作点数量
     * @return 选中的操作点索引
     */
    uint32_t (*select_opp)(uint32_t current_load,
                           const OPP_t *opps,
                           uint32_t opp_count);
} PowerPolicy_t;

/* 电源策略标志 */
#define POWER_POLICY_FLAG_PERFORMANCE  (0x01U)  /**< 性能优先 */
#define POWER_POLICY_FLAG_POWERSAVE    (0x02U)  /**< 省电优先 */
#define POWER_POLICY_FLAG_BALANCED     (0x04U)  /**< 平衡模式 */
```

**CPU空闲状态结构**

```c
/**
 * @brief CPU空闲状态
 * @note MISRA-C:2012合规
 */
typedef struct {
    const char *name;        /**< 状态名称 */
    uint32_t flags;          /**< 状态标志 */
    uint32_t latency_us;     /**< 唤醒延迟 (μs) */
    uint32_t power_save;     /**< 省电百分比 (0-100) */

    /**
     * @brief 进入空闲状态
     * @return 成功返回0，失败返回错误码
     */
    int (*enter)(void);

    /**
     * @brief 退出空闲状态
     * @return 成功返回0，失败返回错误码
     */
    int (*exit)(void);
} IdleState_t;

/* 空闲状态标志 */
#define IDLE_FLAG_WFI           (0x01U)  /**< 使用WFI指令 */
#define IDLE_FLAG_WFE           (0x02U)  /**< 使用WFE指令 */
#define IDLE_FLAG_DEEP_SLEEP    (0x04U)  /**< 深度睡眠 */
```

#### 1.2.3 API设计

**电源管理初始化**

```c
/**
 * @brief 初始化电源管理子系统
 * @return 成功返回0，失败返回负错误码
 *
 * @note 必须在调度器启动前调用
 * @note 初始化CPU频率调节器、空闲管理器等
 */
int pm_init(void);
```

**CPU频率调节**

```c
/**
 * @brief 设置CPU频率
 * @param freq_hz 目标频率 (Hz)
 * @return 成功返回0，失败返回负错误码
 *
 * @note 频率必须是支持的操作点之一
 * @note 自动调节电压以匹配频率
 */
int pm_set_cpu_frequency(uint32_t freq_hz);

/**
 * @brief 获取CPU频率
 * @return 当前CPU频率 (Hz)
 */
uint32_t pm_get_cpu_frequency(void);
```

**空闲状态管理**

```c
/**
 * @brief 进入CPU空闲状态
 * @param expected_idle_us 预期空闲时间 (μs)
 * @return 成功返回0，失败返回负错误码
 *
 * @note 根据预期空闲时间选择合适的空闲状态
 * @note 短时间空闲使用WFI，长时间使用WFE或深度睡眠
 */
int pm_cpu_idle(uint32_t expected_idle_us);
```

**电源策略管理**

```c
/**
 * @brief 设置电源策略
 * @param policy 电源策略指针
 * @return 成功返回0，失败返回负错误码
 */
int pm_set_policy(const PowerPolicy_t *policy);

/**
 * @brief 获取当前电源策略
 * @return 当前电源策略指针
 */
const PowerPolicy_t *pm_get_policy(void);
```

### 1.3 实现方案

#### 1.3.1 CPU频率调节实现

```c
/**
 * @brief DVFS: 动态电压频率调节
 * @param target_freq 目标频率
 * @return 成功返回0，失败返回错误码
 *
 * @note MISRA-C:2012合规实现
 */
static int dvfs_set_frequency(uint32_t target_freq) {
    const OPP_t *opp;
    uint32_t i;
    int ret;

    /* 查找匹配的操作点 */
    opp = NULL;
    for (i = 0U; i < opp_count; i++) {
        if (opps[i].freq_hz == target_freq) {
            opp = &opps[i];
            break;
        }
    }

    if (opp == NULL) {
        return -EINVAL;
    }

    /* 步骤1: 增加电压（如果频率提升） */
    if (target_freq > current_freq) {
        ret = regulator_set_voltage(opp->voltage_mv);
        if (ret != 0) {
            return ret;
        }

        /* 等待电压稳定 */
        udelay(10);
    }

    /* 步骤2: 设置CPU频率 */
    ret = clk_set_rate(cpu_clk, target_freq);
    if (ret != 0) {
        /* 回滚：恢复电压 */
        if (target_freq > current_freq) {
            (void)regulator_set_voltage(current_opp->voltage_mv);
        }
        return ret;
    }

    /* 步骤3: 降低电压（如果频率降低） */
    if (target_freq < current_freq) {
        ret = regulator_set_voltage(opp->voltage_mv);
        if (ret != 0) {
            /* 频率已改变，但电压调整失败，记录警告 */
            printk("Warning: Failed to adjust voltage\n");
        }
    }

    /* 更新当前状态 */
    current_freq = target_freq;
    current_opp = opp;

    return 0;
}
```

#### 1.3.2 空闲状态选择算法

```c
/**
 * @brief 选择空闲状态
 * @param expected_idle_us 预期空闲时间
 * @return 选中的空闲状态索引
 *
 * @note 预期空闲时间越长，选择越深的空闲状态
 * @note 考虑唤醒延迟和省电效果
 */
static uint32_t select_idle_state(uint32_t expected_idle_us) {
    const IdleState_t *state;
    uint32_t i;

    /* 遍历空闲状态（从浅到深） */
    for (i = 0U; i < idle_state_count; i++) {
        state = &idle_states[i];

        /* 检查唤醒延迟 */
        if (state->latency_us > expected_idle_us) {
            /* 预期空闲时间不足以进入此状态 */
            continue;
        }

        /* 检查是否有实时任务 */
        if (has_realtime_task_on_cpu()) {
            /* 实时任务：只允许WFI */
            if ((state->flags & IDLE_FLAG_WFI) == 0U) {
                continue;
            }
        }

        /* 找到合适的空闲状态 */
        return i;
    }

    /* 默认：最浅的空闲状态（WFI） */
    return 0U;
}
```

#### 1.3.3 CPU空闲实现

```c
/**
 * @brief CPU空闲入口
 * @param expected_idle_us 预期空闲时间
 * @return 成功返回0
 *
 * @note MISRA-C:2012合规
 * @note 在调度器没有可运行任务时调用
 */
int pm_cpu_idle(uint32_t expected_idle_us) {
    const IdleState_t *state;
    uint32_t state_idx;
    int ret;

    /* 步骤1: 选择空闲状态 */
    state_idx = select_idle_state(expected_idle_us);
    state = &idle_states[state_idx];

    /* 步骤2: 进入空闲状态 */
    ret = state->enter();
    if (ret != 0) {
        /* 进入失败，使用WFI作为后备 */
        __asm__ volatile("wfi");
        return 0;
    }

    /* 步骤3: 记录空闲时间（统计用） */
    cpu_idle_time += expected_idle_us;

    return 0;
}

/**
 * @brief WFI空闲状态实现
 * @return 成功返回0
 */
static int idle_state_wfi_enter(void) {
    /* 确保内存操作完成 */
    __asm__ volatile("dsb sy");

    /* 等待中断（WFI） */
    __asm__ volatile("wfi");

    /* 指令同步屏障 */
    __asm__ volatile("isb");

    return 0;
}

/**
 * @brief WFE空闲状态实现
 * @return 成功返回0
 */
static int idle_state_wfe_enter(void) {
    /* 确保内存操作完成 */
    __asm__ volatile("dsb sy");

    /* 等待事件（WFE） */
    __asm__ volatile("wfe");

    /* 指令同步屏障 */
    __asm__ volatile("isb");

    return 0;
}
```

### 1.4 配置示例

#### 1.4.1 操作点配置

```c
/* 支持的操作点（Raspberry Pi 4示例） */
static const OPP_t cpu_opps[] = {
    { .freq_hz = 600000000U,  .voltage_mv = 800,  .power_mw = 500   },
    { .freq_hz = 1200000000U, .voltage_mv = 900,  .power_mw = 1200  },
    { .freq_hz = 1500000000U, .voltage_mv = 1000, .power_mw = 1800  },
    { .freq_hz = 1800000000U, .voltage_mv = 1100, .power_mw = 2500  },
};

#define OPP_COUNT (sizeof(cpu_opps) / sizeof(cpu_opps[0]))
```

#### 1.4.2 空闲状态配置

```c
/* 支持的CPU空闲状态 */
static const IdleState_t cpu_idle_states[] = {
    {
        .name = "WFI",
        .flags = IDLE_FLAG_WFI,
        .latency_us = 1U,
        .power_save = 5U,
        .enter = idle_state_wfi_enter,
        .exit = NULL
    },
    {
        .name = "WFE",
        .flags = IDLE_FLAG_WFE,
        .latency_us = 2U,
        .power_save = 10U,
        .enter = idle_state_wfe_enter,
        .exit = NULL
    },
    {
        .name = "DEEP_SLEEP",
        .flags = IDLE_FLAG_DEEP_SLEEP,
        .latency_us = 50U,
        .power_save = 80U,
        .enter = idle_state_deep_sleep_enter,
        .exit = idle_state_deep_sleep_exit
    }
};

#define IDLE_STATE_COUNT (sizeof(cpu_idle_states) / sizeof(cpu_idle_states[0]))
```

---

## 2. 看门狗定时器 (Watchdog Timer)

### 2.1 功能需求

#### 2.1.1 核心需求

**WD1. 硬件看门狗**
- 支持ARMv8-A架构的通用看门狗定时器
- 可配置超时时间（1ms-60s）
- 支持窗口看门狗模式（可选）
- 超时自动复位系统

**WD2. 软件看门狗**
- 基于定时器实现
- 每个任务可选配独立的软件看门狗
- 任务级故障隔离
- 超时触发任务重启或系统重启

**WD3. 看门喂狗策略**
- 自动喂狗（在任务正常运行时）
- 手动喂狗（API调用）
- 死锁检测（防止喂狗线程自身死锁）
- 喂狗超时记录

#### 2.1.2 非功能需求

**可靠性要求**
- 硬件看门狗故障覆盖率>99%
- 软件看门狗故障检测时间<100ms
- 喂狗失败不得导致误报

**安全性要求**
- 符合ISO 26262 ASIL-D要求
- 看门狗配置受安全保护
- 防止恶意禁用看门狗

### 2.2 架构设计

#### 2.2.1 系统架构

```
┌─────────────────────────────────────────────────────┐
│                 应用层                              │
│  (看门喂狗调用 / 故障恢复)                           │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│              看门狗管理框架                          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐          │
│  │ 硬件看门狗 │  │ 软件看门狗 │  │ 故障恢复器 │          │
│  └──────────┘  └──────────┘  └──────────┘          │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│              硬件抽象层 (HAL)                        │
│  ┌──────────┐  ┌──────────┐                        │
│  │ WDT驱动   │  │ 定时器驱动 │                         │
│  └──────────┘  └──────────┘                        │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│              ARMv8-A 硬件                            │
│  (ARM Generic Watchdog / SP805 WDT)                │
└─────────────────────────────────────────────────────┘
```

#### 2.2.2 数据结构设计

**看门狗配置结构**

```c
/**
 * @brief 看门狗配置结构
 * @note MISRA-C:2012合规
 */
typedef struct {
    uint32_t timeout_ms;     /**< 超时时间 (ms) */
    uint32_t window_ms;      /**< 窗口时间 (ms, 0表示无窗口) */
    uint32_t flags;          /**< 配置标志 */

    /**
     * @brief 超时回调函数
     * @param wdt_id 看门狗ID
     */
    void (*timeout_callback)(uint32_t wdt_id);

    /**
     * @brief 复位动作
     * @param wdt_id 看门狗ID
     * @return 成功返回0，失败返回错误码
     */
    int (*reset_action)(uint32_t wdt_id);
} WatchdogConfig_t;

/* 看门狗标志 */
#define WDT_FLAG_HARDWARE      (0x01U)  /**< 硬件看门狗 */
#define WDT_FLAG_SOFTWARE      (0x02U)  /**< 软件看门狗 */
#define WDT_FLAG_ENABLED       (0x04U)  /**< 已启用 */
#define WDT_FLAG_AUTO_FEED     (0x08U)  /**< 自动喂狗 */
#define WDT_FLAG_WINDOW_MODE   (0x10U)  /**< 窗口模式 */
```

**软件看门狗结构**

```c
/**
 * @brief 软件看门狗结构
 * @note MISRA-C:2012合规
 */
typedef struct {
    uint32_t task_id;        /**< 关联的任务ID */
    uint32_t timeout_ms;     /**< 超时时间 */
    uint64_t last_feed_ns;   /**< 上次喂狗时间 */
    uint32_t flags;          /**< 标志 */
    uint32_t fault_count;    /**< 故障计数 */
    WatchdogConfig_t config; /**< 配置 */
} SoftwareWatchdog_t;

/* 软件看门狗标志 */
#define SWDT_FLAG_ACTIVE       (0x01U)  /**< 活跃 */
#define SWDT_FLAG_SUSPENDED    (0x02U)  /**< 已暂停 */
```

#### 2.2.3 API设计

**硬件看门狗**

```c
/**
 * @brief 初始化硬件看门狗
 * @param config 看门狗配置
 * @return 成功返回看门狗ID，失败返回负错误码
 */
int wdt_hardware_init(const WatchdogConfig_t *config);

/**
 * @brief 启动硬件看门狗
 * @param wdt_id 看门狗ID
 * @return 成功返回0，失败返回负错误码
 */
int wdt_hardware_start(uint32_t wdt_id);

/**
 * @brief 喂狗（重置计数器）
 * @param wdt_id 看门狗ID
 * @return 成功返回0，失败返回负错误码
 */
int wdt_hardware_feed(uint32_t wdt_id);

/**
 * @brief 停止硬件看门狗
 * @param wdt_id 看门狗ID
 * @return 成功返回0，失败返回负错误码
 *
 * @warning 停止硬件看门狗会降低系统安全性
 */
int wdt_hardware_stop(uint32_t wdt_id);
```

**软件看门狗**

```c
/**
 * @brief 为任务创建软件看门狗
 * @param task_id 任务ID
 * @param config 看门狗配置
 * @return 成功返回看门狗ID，失败返回负错误码
 */
int wdt_software_create(uint32_t task_id,
                        const WatchdogConfig_t *config);

/**
 * @brief 喂软件看门狗
 * @param wdt_id 看门狗ID
 * @return 成功返回0，失败返回负错误码
 */
int wdt_software_feed(uint32_t wdt_id);

/**
 * @brief 销毁软件看门狗
 * @param wdt_id 看门狗ID
 * @return 成功返回0，失败返回负错误码
 */
int wdt_software_destroy(uint32_t wdt_id);
```

### 2.3 实现方案

#### 2.3.1 硬件看门狗驱动

```c
/**
 * @brief ARM Generic Watchdog初始化
 * @param config 看门狗配置
 * @return 成功返回0，失败返回负错误码
 *
 * @note MISRA-C:2012合规
 */
static int arm_wdt_init(const WatchdogConfig_t *config) {
    uint64_t wdt_freq_hz;
    uint64_t max_timeout_ms;

    /* 参数验证 */
    if (config == NULL) {
        return -EINVAL;
    }

    if (config->timeout_ms == 0U) {
        return -EINVAL;
    }

    /* 计算看门狗频率 */
    wdt_freq_hz = get_system_clock_freq();

    /* 计算最大超时时间 */
    max_timeout_ms = (0xFFFFFFFFULL * 1000ULL) / wdt_freq_hz;

    /* 检查超时时间是否超出范围 */
    if (config->timeout_ms > (uint32_t)max_timeout_ms) {
        return -EINVAL;
    }

    /* 计算比较值 (Watchdog Offset Register) */
    uint64_t ticks = (wdt_freq_hz * config->timeout_ms) / 1000ULL;
    uint32_t wor = (uint32_t)(0xFFFFFFFFULL - ticks);

    /* 写入WOR (Watchdog Offset Register) */
    __asm__ volatile("msr wor_el1, %0" :: "r"((uint64_t)wor));

    /* 配置控制寄存器 */
    uint32_t wcr = 0U;

    /* 设置窗口模式（如果启用） */
    if ((config->flags & WDT_FLAG_WINDOW_MODE) != 0U) {
        wcr |= (1U << 1);  /* WDT_WCR_WS */
    }

    /* 写入WCR (Watchdog Control Register) */
    __asm__ volatile("msr wcr_el1, %0" :: "r"(wcr));

    /* 同步 */
    __asm__ volatile("isb");

    return 0;
}

/**
 * @brief 启动硬件看门狗
 * @return 成功返回0，失败返回负错误码
 */
static int arm_wdt_start(void) {
    /* 使能看门狗 */
    uint32_t wcr = 0U;

    __asm__ volatile("mrs %0, wcr_el1" : "=r"(wcr));
    wcr |= (1U << 0);  /* WDT_WCR_EN */
    __asm__ volatile("msr wcr_el1, %0" :: "r"(wcr));

    /* 同步 */
    __asm__ volatile("isb");

    return 0;
}

/**
 * @brief 喂狗
 * @return 成功返回0
 */
static int arm_wdt_feed(void) {
    /* 写入任意值到WRR (Watchdog Refresh Register) */
    __asm__ volatile("msr wrr_el1, %0" :: "r"(0U));

    /* 数据同步屏障 */
    __asm__ volatile("dsb sy");

    return 0;
}
```

#### 2.3.2 软件看门狗监控

```c
/**
 * @brief 软件看门狗监控线程
 * @param arg 参数（未使用）
 *
 * @note MISRA-C:2012合规
 * @note 周期性检查所有软件看门狗
 */
static void software_wdt_monitor(void *arg) {
    (void)arg;

    while (1) {
        uint64_t current_ns;
        uint32_t i;

        /* 获取当前时间 */
        current_ns = get_system_time_ns();

        /* 遍历所有软件看门狗 */
        for (i = 0U; i < MAX_SOFTWARE_WDT; i++) {
            SoftwareWatchdog_t *swdt = &software_wdts[i];

            /* 检查是否活跃 */
            if ((swdt->flags & SWDT_FLAG_ACTIVE) == 0U) {
                continue;
            }

            /* 检查是否暂停 */
            if ((swdt->flags & SWDT_FLAG_SUSPENDED) != 0U) {
                continue;
            }

            /* 计算经过时间 */
            uint64_t elapsed_ns = current_ns - swdt->last_feed_ns;
            uint32_t elapsed_ms = (uint32_t)(elapsed_ns / 1000000ULL);

            /* 检查是否超时 */
            if (elapsed_ms >= swdt->timeout_ms) {
                /* 超时：触发故障处理 */
                swdt->fault_count++;

                /* 调用超时回调 */
                if (swdt->config.timeout_callback != NULL) {
                    swdt->config.timeout_callback(i);
                }

                /* 执行复位动作 */
                if (swdt->config.reset_action != NULL) {
                    (void)swdt->config.reset_action(i);
                }
            }
        }

        /* 延迟100ms再检查 */
        task_sleep(100);
    }
}
```

#### 2.3.3 自动喂狗实现

```c
/**
 * @brief 自动喂狗函数
 * @param wdt_id 看门狗ID
 * @return 成功返回0，失败返回负错误码
 */
static int auto_feed_watchdog(uint32_t wdt_id) {
    SoftwareWatchdog_t *swdt;

    /* 参数验证 */
    if (wdt_id >= MAX_SOFTWARE_WDT) {
        return -EINVAL;
    }

    swdt = &software_wdts[wdt_id];

    /* 检查是否活跃 */
    if ((swdt->flags & SWDT_FLAG_ACTIVE) == 0U) {
        return -EINVAL;
    }

    /* 检查是否启用自动喂狗 */
    if ((swdt->config.flags & WDT_FLAG_AUTO_FEED) == 0U) {
        return -EINVAL;
    }

    /* 更新喂狗时间 */
    swdt->last_feed_ns = get_system_time_ns();

    /* 如果是硬件看门狗，也喂硬件看门狗 */
    if ((swdt->config.flags & WDT_FLAG_HARDWARE) != 0U) {
        return arm_wdt_feed();
    }

    return 0;
}
```

### 2.4 故障恢复策略

#### 2.4.1 故障等级定义

```c
/**
 * @brief 看门狗故障等级
 */
typedef enum {
    WDT_FAULT_LEVEL_WARNING = 0,  /**< 警告级：记录日志 */
    WDT_FAULT_LEVEL_TASK,        /**< 任务级：重启任务 */
    WDT_FAULT_LEVEL_SYSTEM       /**< 系统级：重启系统 */
} WatchdogFaultLevel_t;
```

#### 2.4.2 故障恢复动作

```c
/**
 * @brief 看门狗故障恢复
 * @param wdt_id 看门狗ID
 * @param level 故障等级
 * @return 成功返回0，失败返回负错误码
 */
static int watchdog_fault_recovery(uint32_t wdt_id,
                                  WatchdogFaultLevel_t level) {
    switch (level) {
        case WDT_FAULT_LEVEL_WARNING:
            /* 记录警告日志 */
            printk("Warning: Watchdog %u timeout\n", wdt_id);
            break;

        case WDT_FAULT_LEVEL_TASK:
            /* 重启关联的任务 */
            {
                SoftwareWatchdog_t *swdt = &software_wdts[wdt_id];
                task_restart(swdt->task_id);
            }
            break;

        case WDT_FAULT_LEVEL_SYSTEM:
            /* 系统重启 */
            printk("Fatal: Watchdog timeout, rebooting system\n");
            system_reset();
            break;

        default:
            return -EINVAL;
    }

    return 0;
}
```

---

## 3. 错误恢复机制 (Error Recovery)

### 3.1 功能需求

#### 3.1.1 核心需求

**ER1. 异常处理**
- 支持同步异常（数据中止、预取中止）
- 支持异步异常（IRQ、FIQ）
- 支持系统错误异常（SError）
- 异常分类和统计

**ER2. 错误码系统**
- 统一的错误码定义
- 错误码到字符串转换
- 错误码严重性分级
- 错误传播机制

**ER3. 故障隔离**
- 任务级故障隔离
- 内存隔离（通过MPU/MMU）
- 资源隔离（文件描述符、句柄）
- 故障不传播到其他任务

**ER4. 故障恢复**
- 任务级故障恢复（任务重启）
- 系统级故障恢复（系统重启）
- 应用级故障恢复（应用重启）
- 安全状态恢复

#### 3.1.2 非功能需求

**可靠性要求**
- 错误检测覆盖率>95%
- 恢复成功率>90%
- 恢复时间<100ms

**安全性要求**
- 符合ISO 26262 ASIL-D要求
- 错误处理路径经过形式化验证
- 无未定义行为

### 3.2 架构设计

#### 3.2.1 系统架构

```
┌─────────────────────────────────────────────────────┐
│                 异常处理                            │
│  (同步异常 / 异步异常 / SError)                     │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│              错误恢复管理器                          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐          │
│  │错误分类器 │  │错误处理器 │  │恢复执行器 │          │
│  └──────────┘  └──────────┘  └──────────┘          │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│              恢复策略                               │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐          │
│  │忽略     │  │任务重启  │  │系统重启  │          │
│  └──────────┘  └──────────┘  └──────────┘          │
└─────────────────────────────────────────────────────┘
```

#### 3.2.2 数据结构设计

**错误码定义**

```c
/**
 * @brief 错误码类型
 * @note MISRA-C:2012合规
 */
typedef int32_t ErrorCode_t;

/* 错误码范围定义 */
#define ERROR_SUCCESS           0    /**< 成功 */
#define ERROR_BASE              0x1000 /**< 错误码基址 */

/* 通用错误 */
#define ERROR_FAIL              (ERROR_BASE + 0x01)
#define ERROR_INVALID_PARAM     (ERROR_BASE + 0x02)
#define ERROR_OUT_OF_MEMORY     (ERROR_BASE + 0x03)
#define ERROR_TIMEOUT           (ERROR_BASE + 0x04)
#define ERROR_BUSY              (ERROR_BASE + 0x05)

/* 内存错误 */
#define ERROR_MEM_INVALID       (ERROR_BASE + 0x10)
#define ERROR_MEM_ALIGN         (ERROR_BASE + 0x11)
#define ERROR_MEM_OVERFLOW      (ERROR_BASE + 0x12)

/* 任务错误 */
#define ERROR_TASK_INVALID      (ERROR_BASE + 0x20)
#define ERROR_TASK_CREATE_FAIL  (ERROR_BASE + 0x21)
#define ERROR_TASK_DELETE_FAIL  (ERROR_BASE + 0x22)
#define ERROR_TASK_DEADLOCK     (ERROR_BASE + 0x23)

/* 系统错误 */
#define ERROR_SYS_PANIC         (ERROR_BASE + 0xF0)
#define ERROR_SYS_BUG           (ERROR_BASE + 0xF1)
```

**错误信息结构**

```c
/**
 * @brief 错误信息结构
 * @note MISRA-C:2012合规
 */
typedef struct {
    ErrorCode_t code;          /**< 错误码 */
    const char *msg;           /**< 错误消息 */
    const char *file;          /**< 发生错误的文件 */
    uint32_t line;             /**< 发生错误的行号 */
    uint64_t timestamp;        /**< 时间戳 */
} ErrorInfo_t;

/* 编译时断言 */
_Static_assert(sizeof(ErrorInfo_t) <= 64U,
               "ErrorInfo_t size must not exceed 64 bytes");
```

**恢复策略结构**

```c
/**
 * @brief 恢复策略结构
 * @note MISRA-C:2012合规
 */
typedef struct {
    const char *name;         /**< 策略名称 */
    uint32_t flags;           /**< 策略标志 */

    /**
     * @brief 执行恢复策略
     * @param error 错误信息
     * @return 成功返回0，失败返回负错误码
     */
    int (*execute)(const ErrorInfo_t *error);
} RecoveryStrategy_t;

/* 恢复策略标志 */
#define RECOVERY_FLAG_IGNORE      (0x01U)  /**< 忽略错误 */
#define RECOVERY_FLAG_RETRY       (0x02U)  /**< 重试操作 */
#define RECOVERY_FLAG_RESTART_TASK (0x04U)  /**< 重启任务 */
#define RECOVERY_FLAG_RESTART_APP  (0x08U)  /**< 重启应用 */
#define RECOVERY_FLAG_REBOOT       (0x10U)  /**< 系统重启 */
```

#### 3.2.3 API设计

```c
/**
 * @brief 报告错误
 * @param code 错误码
 * @param file 文件名
 * @param line 行号
 * @return 成功返回0，失败返回负错误码
 *
 * @note 通常使用宏定义调用
 */
int error_report(ErrorCode_t code,
                const char *file,
                uint32_t line);

/**
 * @brief 注册恢复策略
 * @param error_code 错误码
 * @param strategy 恢复策略
 * @return 成功返回0，失败返回负错误码
 */
int error_recovery_register(ErrorCode_t error_code,
                           const RecoveryStrategy_t *strategy);

/**
 * @brief 执行恢复
 * @param error 错误信息
 * @return 成功返回0，失败返回负错误码
 */
int error_recovery_execute(const ErrorInfo_t *error);
```

### 3.3 实现方案

#### 3.3.1 异常处理框架

```c
/**
 * @brief 同步异常处理入口
 * @note MISRA-C:2012合规
 */
void sync_exception_handler(void) {
    uint64_t esr;
    uint64_t elr;
    uint64_t far;
    uint32_t ec;
    ErrorCode_t error_code;

    /* 读取异常综合征寄存器 */
    __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
    __asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
    __asm__ volatile("mrs %0, far_el1" : "=r"(far));

    /* 提取异常类 (Exception Class) */
    ec = (uint32_t)((esr >> 26U) & 0x3FU);

    /* 根据异常类分类错误 */
    switch (ec) {
        case 0x20:  /* Instruction Abort */
        case 0x21:  /* Instruction Abort */
            error_code = ERROR_MEM_INVALID;
            break;

        case 0x24:  /* Data Abort */
        case 0x25:  /* Data Abort */
            error_code = ERROR_MEM_INVALID;
            break;

        case 0x00:  /* Unknown reason */
            error_code = ERROR_SYS_BUG;
            break;

        default:
            error_code = ERROR_SYS_BUG;
            break;
    }

    /* 构造错误信息 */
    ErrorInfo_t error = {
        .code = error_code,
        .msg = NULL,
        .file = __FILE__,
        .line = __LINE__,
        .timestamp = get_system_time_ns()
    };

    /* 执行恢复 */
    (void)error_recovery_execute(&error);
}
```

#### 3.3.2 错误恢复实现

```c
/**
 * @brief 执行错误恢复
 * @param error 错误信息
 * @return 成功返回0，失败返回负错误码
 */
int error_recovery_execute(const ErrorInfo_t *error) {
    const RecoveryStrategy_t *strategy;
    int ret;

    /* 参数验证 */
    if (error == NULL) {
        return -EINVAL;
    }

    /* 记录错误日志 */
    error_log_write(error);

    /* 查找恢复策略 */
    strategy = find_recovery_strategy(error->code);

    if (strategy == NULL) {
        /* 没有注册的策略，使用默认策略 */
        strategy = &default_recovery_strategy;
    }

    /* 执行恢复策略 */
    ret = strategy->execute(error);

    if (ret != 0) {
        /* 恢复失败，系统重启 */
        printk("Recovery failed, rebooting system\n");
        system_reset();
    }

    return ret;
}
```

#### 3.3.3 任务级恢复实现

```c
/**
 * @brief 任务重启策略
 * @param error 错误信息
 * @return 成功返回0，失败返回负错误码
 */
static int recovery_restart_task(const ErrorInfo_t *error) {
    TCB_t *fault_task;

    /* 获取当前任务 */
    fault_task = get_current_task();

    if (fault_task == NULL) {
        return -EINVAL;
    }

    printk("Restarting task %u (%s) due to error %d\n",
           fault_task->tid, fault_task->name, error->code);

    /* 检查任务是否支持自动重启 */
    if (fault_task->auto_restart == 0U) {
        /* 不支持自动重启，删除任务 */
        task_delete(fault_task->tid);
        return 0;
    }

    /* 重启任务 */
    return task_restart(fault_task->tid);
}
```

---

## 4. 日志系统 (Logging System)

### 4.1 功能需求

#### 4.1.1 核心需求

**LOG1. 日志级别**
- 支持至少5个日志级别（DEBUG/INFO/WARN/ERROR/FATAL）
- 可配置全局日志级别
- 运行时动态调整日志级别

**LOG2. 日志格式**
- 时间戳（高精度）
- 日志级别
- 任务ID
- 文件名和行号
- 日志消息

**LOG3. 日志输出**
- 支持串口输出（UART）
- 支持网络输出（UDP/TCP，可选）
- 支持文件系统输出（可选）
- 支持循环缓冲区（内存中）

**LOG4. 日志过滤**
- 基于日志级别过滤
- 基于任务ID过滤
- 基于模块过滤
- 运行时动态配置过滤规则

**LOG5. 性能要求**
- 日志记录延迟<10μs
- 异步日志输出
- 不阻塞调用线程

#### 4.1.2 非功能需求

**性能要求**
- 日志系统开销<2% CPU
- 内存占用<100KB
- 日志丢失率<1%

**可靠性要求**
- 日志系统故障不影响核心功能
- 日志输出失败不导致系统崩溃
- 关键日志（FATAL）强制输出

### 4.2 架构设计

#### 4.2.1 系统架构

```
┌─────────────────────────────────────────────────────┐
│                 应用层                              │
│  (日志API调用)                                       │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│              日志前端 (Front-end)                    │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐          │
│  │日志过滤  │  │格式化器 │  │时间戳   │          │
│  └──────────┘  └──────────┘  └──────────┘          │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│              日志后端 (Back-end)                    │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐          │
│  │内存缓冲区 │  │串口输出 │  │网络输出 │          │
│  └──────────┘  └──────────┘  └──────────┘          │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│              输出目标                                │
│  (UART / 网络 / 文件系统)                            │
└─────────────────────────────────────────────────────┘
```

#### 4.2.2 数据结构设计

**日志级别定义**

```c
/**
 * @brief 日志级别枚举
 * @note MISRA-C:2012合规
 */
typedef enum {
    LOG_LEVEL_DEBUG = 0,     /**< 调试信息 */
    LOG_LEVEL_INFO,          /**< 一般信息 */
    LOG_LEVEL_WARN,          /**< 警告信息 */
    LOG_LEVEL_ERROR,         /**< 错误信息 */
    LOG_LEVEL_FATAL          /**< 致命错误 */
} LogLevel_t;
```

**日志消息结构**

```c
/**
 * @brief 日志消息结构
 * @note MISRA-C:2012合规
 */
typedef struct {
    uint64_t timestamp_ns;   /**< 时间戳 (ns) */
    uint32_t task_id;        /**< 任务ID */
    LogLevel_t level;        /**< 日志级别 */
    const char *file;        /**< 文件名 */
    uint32_t line;           /**< 行号 */
    const char *msg;         /**< 日志消息 */
    uint32_t msg_len;        /**< 消息长度 */
} LogMessage_t;

/* 编译时断言 */
_Static_assert(sizeof(LogMessage_t) <= 64U,
               "LogMessage_t size must not exceed 64 bytes");
```

**日志配置结构**

```c
/**
 * @brief 日志配置结构
 * @note MISRA-C:2012合规
 */
typedef struct {
    LogLevel_t global_level; /**< 全局日志级别 */
    uint32_t flags;          /**< 配置标志 */

    /**
     * @brief 日志过滤函数
     * @param msg 日志消息
     *return true表示输出，false表示过滤
     */
    bool (*filter)(const LogMessage_t *msg);
} LogConfig_t;

/* 日志配置标志 */
#define LOG_FLAG_ASYNC         (0x01U)  /**< 异步输出 */
#define LOG_FLAG_TIMESTAMP     (0x02U)  /**< 包含时间戳 */
#define LOG_FLAG_TASK_ID       (0x04U)  /**< 包含任务ID */
#define LOG_FLAG_FILE_LINE     (0x08U)  /**< 包含文件和行号 */
```

#### 4.2.3 API设计

```c
/**
 * @brief 输出日志
 * @param level 日志级别
 * @param file 文件名
 * @param line 行号
 * @param fmt 格式化字符串
 * @param ... 可变参数
 *
 * @note 通常使用宏定义调用
 */
void log_output(LogLevel_t level,
               const char *file,
               uint32_t line,
               const char *fmt,
               ...);

/**
 * @brief 设置全局日志级别
 * @param level 日志级别
 */
void log_set_level(LogLevel_t level);

/**
 * @brief 设置日志过滤函数
 * @param filter 过滤函数指针
 */
void log_set_filter(bool (*filter)(const LogMessage_t *msg));
```

### 4.3 实现方案

#### 4.3.1 日志宏定义

```c
/**
 * @brief 日志宏定义
 */
#define LOG_DEBUG(fmt, ...) \
    log_output(LOG_LEVEL_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_INFO(fmt, ...) \
    log_output(LOG_LEVEL_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) \
    log_output(LOG_LEVEL_WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
    log_output(LOG_LEVEL_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LOG_FATAL(fmt, ...) \
    log_output(LOG_LEVEL_FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
```

#### 4.3.2 日志输出实现

```c
/**
 * @brief 日志输出实现
 * @param level 日志级别
 * @param file 文件名
 * @param line 行号
 * @param fmt 格式化字符串
 * @return 成功返回0，失败返回负错误码
 */
void log_output(LogLevel_t level,
               const char *file,
               uint32_t line,
               const char *fmt,
               ...) {
    LogMessage_t msg;
    va_list args;
    char buffer[256];
    int ret;

    /* 检查日志级别 */
    if (level < g_log_config.global_level) {
        return;
    }

    /* 构造日志消息 */
    msg.timestamp_ns = get_system_time_ns();
    msg.task_id = get_current_task_id();
    msg.level = level;
    msg.file = file;
    msg.line = line;

    /* 格式化消息 */
    va_start(args, fmt);
    ret = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (ret < 0) {
        return;
    }

    msg.msg = buffer;
    msg.msg_len = (uint32_t)ret;

    /* 应用过滤 */
    if (g_log_config.filter != NULL) {
        if (!g_log_config.filter(&msg)) {
            return;
        }
    }

    /* 输出到后端 */
    log_backend_write(&msg);
}
```

#### 4.3.3 串口输出实现

```c
/**
 * @brief 串口日志输出
 * @param msg 日志消息
 * @return 成功返回0，失败返回负错误码
 */
static int log_backend_uart(const LogMessage_t *msg) {
    char buffer[512];
    int ret;

    /* 格式化日志输出 */
    ret = snprintf(buffer, sizeof(buffer),
                  "[%llu] [%u] [%c] %s:%u: %s\n",
                  msg->timestamp_ns,
                  msg->task_id,
                  log_level_to_char(msg->level),
                  msg->file,
                  msg->line,
                  msg->msg);

    if ((ret < 0) || ((uint32_t)ret >= sizeof(buffer))) {
        return -EINVAL;
    }

    /* 输出到串口 */
    uart_write(buffer, (uint32_t)ret);

    return 0;
}

/**
 * @brief 日志级别到字符转换
 * @param level 日志级别
 * @return 级别字符
 */
static char log_level_to_char(LogLevel_t level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return 'D';
        case LOG_LEVEL_INFO:  return 'I';
        case LOG_LEVEL_WARN:  return 'W';
        case LOG_LEVEL_ERROR: return 'E';
        case LOG_LEVEL_FATAL: return 'F';
        default: return '?';
    }
}
```

#### 4.3.4 循环缓冲区实现

```c
/**
 * @brief 日志循环缓冲区
 */
static struct {
    uint8_t buffer[LOG_BUFFER_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    TicketLock_t lock;
} log_buffer = {0};

/**
 * @brief 写入循环缓冲区
 * @param data 数据指针
 * @param size 数据大小
 * @return 成功返回写入的字节数
 */
static uint32_t log_buffer_write(const uint8_t *data, uint32_t size) {
    uint32_t written;
    uint32_t remaining;

    /* 获取锁 */
    ticket_lock_acquire(&log_buffer.lock);

    /* 计算可写入空间 */
    remaining = LOG_BUFFER_SIZE - log_buffer.count;

    if (size > remaining) {
        /* 缓冲区空间不足，丢弃旧数据 */
        uint32_t drop = size - remaining;
        log_buffer.tail = (log_buffer.tail + drop) % LOG_BUFFER_SIZE;
        log_buffer.count = 0;
    }

    /* 写入数据 */
    written = 0U;
    while (written < size) {
        uint32_t space = LOG_BUFFER_SIZE - log_buffer.count;
        uint32_t contiguous = LOG_BUFFER_SIZE - log_buffer.head;
        uint32_t to_write = (size - written) < contiguous ?
                             (size - written) : contiguous;

        if (to_write > space) {
            break;
        }

        (void)memcpy(&log_buffer.buffer[log_buffer.head],
                     data + written,
                     to_write);

        log_buffer.head = (log_buffer.head + to_write) % LOG_BUFFER_SIZE;
        log_buffer.count += to_write;
        written += to_write;
    }

    /* 释放锁 */
    ticket_lock_release(&log_buffer.lock);

    return written;
}
```

---

## 5. 集成和配置

### 5.1 MenuConfig配置

```kconfig
# Power Management configuration
config POWER_MANAGEMENT
    bool "Power Management Support"
    default y
    help
      Enable CPU frequency scaling, idle states, and power saving.

config PM_CPU_FREQ
    bool "CPU Frequency Scaling"
    depends on POWER_MANAGEMENT
    default y
    help
      Enable dynamic voltage and frequency scaling (DVFS).

config PM_CPU_IDLE
    bool "CPU Idle States"
    depends on POWER_MANAGEMENT
    default y
    help
      Enable CPU idle states (WFI, WFE, deep sleep).

# Watchdog configuration
config WATCHDOG
    bool "Watchdog Timer Support"
    default y
    help
      Enable hardware and software watchdog timers.

config WATCHDOG_HARDWARE
    bool "Hardware Watchdog"
    depends on WATCHDOG
    default y
    help
      Enable ARM Generic Watchdog timer.

config WATCHDOG_SOFTWARE
    bool "Software Watchdog"
    depends on WATCHDOG
    default y
    help
      Enable per-task software watchdogs.

# Error Recovery configuration
config ERROR_RECOVERY
    bool "Error Recovery Support"
    default y
    help
      Enable automatic error detection and recovery.

config ERROR_RECOVERY_TASK_RESTART
    bool "Task Restart on Error"
    depends on ERROR_RECOVERY
    default y
    help
      Automatically restart tasks when faults are detected.

# Logging configuration
config LOG
    bool "Logging Support"
    default y
    help
      Enable kernel logging system.

config LOG_LEVEL
    int "Default Log Level"
    depends on LOG
    range 0 4
    default 2
    help
      0: DEBUG, 1: INFO, 2: WARN, 3: ERROR, 4: FATAL

config LOG_UART
    bool "UART Log Output"
    depends on LOG
    default y
    help
      Output log messages to UART.
```

### 5.2 设备树配置

```dts
/ {
    cpus {
        cpu@0 {
            operating-points = <600000 800000>,
                                 <1200000 900000>,
                                 <1500000 1000000>,
                                 <1800000 1100000>;
        };
    };

    watchdog@0 {
        compatible = "arm,arm-generic-watchdog";
        reg = <0x0 0x1000>;
        interrupts = <0 25 IRQ_TYPE_LEVEL_HIGH>;
        timeout-ms = <30000>;
    };

    pmu@0 {
        compatible = "arm,armv8-pmuv3";
        events = <0x08>; /* CPU_CYCLES */
    };
};
```

---

## 6. 实施计划

### 6.1 实施优先级

#### P0 - 必须实现（0-3个月）

1. **电源管理基础（2周）**
   - CPU频率调节
   - WFI/WFE空闲状态
   - 电源策略管理

2. **硬件看门狗（2周）**
   - ARM Generic Watchdog驱动
   - 看门狗初始化和喂狗
   - 超时处理

3. **日志系统基础（2周）**
   - 日志级别和格式
   - 串口输出
   - 循环缓冲区

#### P1 - 重要功能（3-6个月）

4. **软件看门狗（3周）**
   - 软件看门狗监控
   - 任务级故障隔离
   - 自动喂狗

5. **错误恢复（4周）**
   - 异常处理框架
   - 错误码系统
   - 任务重启策略

#### P2 - 可选功能（6-12个月）

6. **高级电源管理（6周）**
   - 深度睡眠状态
   - 外设电源控制
   - 系统休眠和唤醒

7. **高级日志功能（4周）**
   - 网络日志输出
   - 文件系统日志
   - 日志过滤和分析

### 6.2 测试计划

**电源管理测试**
- CPU频率切换测试
- 空闲状态进入/退出测试
- 功耗测量

**看门狗测试**
- 看门狗超时测试
- 喂狗测试
- 恢复策略测试

**错误恢复测试**
- 异常注入测试
- 错误传播测试
- 恢复成功率测试

**日志系统测试**
- 日志输出完整性测试
- 性能测试
- 缓冲区溢出测试

---

## 7. MISRA-C:2012合规性

所有代码实现必须遵循MISRA-C:2012规范，包括：

- 规则1.3: 定义严格的signedness
- 规则10.1: 防止整数溢出
- 规则13.5: 检查指针参数
- 规则21.1: 内联汇编最小化
- 规则21.3: 边界检查

---

## 8. 附录

### 8.1 参考标准

- ARMv8-A Architecture Reference Manual
- ARM Generic Watchdog Specification
- ISO 26262:2018 - Functional Safety
- MISRA-C:2012 - C Coding Guidelines

### 8.2 相关文档

- `plan.md` - AISafe64主设计文档
- `CLAUDE.md` - C代码生成规范
- `功能完整性分析报告.md` - 功能分析报告

---

**文档版本**: 1.0
**创建日期**: 2026-01-08
**作者**: AISafe64 Team
**适用标准**: MISRA-C:2012, ISO 26262 ASIL-D, ARMv8-A
