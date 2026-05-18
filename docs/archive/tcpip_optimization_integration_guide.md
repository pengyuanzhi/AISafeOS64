# TCP/IP 协议栈新功能集成指南

## 文档说明

本文档提供了如何在 `tcp_process_segment()` 函数中集成所有新功能的详细指南。

---

## 集成概述

需要在 `tcp_process_segment()` 函数中集成以下新功能：

1. **TCP 选项解析**
   - MSS 选项
   - 窗口缩放选项
   - SACK 选项
   - TCP 时间戳选项

2. **CUBIC 拥塞控制算法**
   - 新 ACK 处理
   - 重复 ACK 处理
   - 窗口增长算法

3. **TCP 时间戳**
   - RTT 测量
   - 时间戳回显

4. **TCP Keepalive**
   - 最后活跃时间更新
   - 探测逻辑

5. **TCP 选项构造**
   - 在发送 TCP 段时添加选项

---

## 集成步骤

### Step 1：修改 tcp_tcb_t 结构体（已完成）

已经在 `main.c` 中完成，添加了以下字段：

```c
/* 新增：CUBIC 算法字段 */
uint32_t cubic_cwnd;      /**< CUBIC 窗口 */
uint32_t cubic_ssthresh;  /**< CUBIC 慢启动阈值 */
uint32_t cubic_w_max;     /**< CUBIC 峰值窗口 */
uint32_t cubic_epoch;     /**< CUBIC 时代开始时间 */
uint32_t cubic_k;         /**< CUBIC 参数 K */
uint8_t cubic_state;     /**< CUBIC 状态 */

/* 新增：TCP 时间戳字段 */
uint32_t ts_val;          /**< 时间戳值 */
uint32_t ts_echo_rpl;     /**< 时间戳回显 */
uint32_t recent_ts;       /**< 最近接收时间戳 */
bool ts_enabled;          /**< 时间戳启用 */

/* 新增：TCP Keepalive 字段 */
uint32_t keepalive_last_active;   /**< 最后活跃时间 */
uint32_t keepalive_probe_count;   /**< 当前探测次数 */
uint32_t keepalive_next_probe;    /**< 下一次探测时间 */
bool keepalive_enabled;          /**< Keepalive 启用 */
bool keepalive_timeout;          /**< Keepalive 超时 */

/* 新增：TCP 选项字段 */
uint16_t mss;             /**< 最大段大小 */
uint8_t window_scale;    /**< 窗口缩放因子 */
uint8_t mss_negotiated;  /**< MSS 已协商 */
uint8_t window_scale_negotiated; /**< 窗口缩放已协商 */
```

### Step 2：在 tcp_process_segment() 函数中添加 TCP 选项解析

在 `tcp_process_segment()` 函数开始处，添加 TCP 选项解析代码：

```c
/* 计算 TCP 选项偏移 */
uint16_t opt_offset = (uint16_t)((tcp->data_offset >> 4U) - 5U) * 4U;
uint16_t opt_len = data_offset - (uint32_t)TCP_HDR_SIZE;

/* 处理 TCP 选项（MSS、窗口缩放、SACK、时间戳） */
if (opt_len > 0U)
{
    const uint8_t *opt_data = &data[TCP_HDR_SIZE];
    uint16_t opt_offset_temp = 0U;

    while (opt_offset_temp < opt_len)
    {
        uint8_t opt_kind = opt_data[opt_offset_temp];

        if (opt_kind == 0U)  /* End of options */
        {
            break;
        }

        if (opt_kind == 1U)  /* NOP */
        {
            opt_offset_temp++;
            continue;
        }

        if ((opt_offset_temp + 1U) >= opt_len)
        {
            break;
        }

        uint8_t opt_length = opt_data[opt_offset_temp + 1U];

        /* 处理 MSS 选项 */
        if (opt_kind == 2U)  /* MSS */
        {
            if (opt_length == 4U)
            {
                uint16_t mss = (opt_data[opt_offset_temp + 2U] << 8U) |
                                opt_data[opt_offset_temp + 3U];
                /* 更新 tcb->mss */
                if (tcb != NULL)
                {
                    tcb->mss = mss;
                    tcb->mss_negotiated = 1U;
                }
            }
        }
        /* 处理窗口缩放选项 */
        else if (opt_kind == 3U)  /* Window Scale */
        {
            if (opt_length == 3U)
            {
                uint8_t scale = opt_data[opt_offset_temp + 2U];
                /* 更新 tcb->window_scale */
                if (tcb != NULL)
                {
                    tcb->window_scale = scale;
                    tcb->window_scale_negotiated = 1U;
                }
            }
        }
        /* 处理 SACK 选项 */
        else if (opt_kind == 4U)  /* SACK Permitted */
        {
            if (opt_length == 2U)
            {
                /* 更新 tcb->sack_permitted */
                if (tcb != NULL)
                {
                    tcb->sack_permitted = 1U;
                }
            }
        }
        /* 处理时间戳选项 */
        else if (opt_kind == 8U)  /* Timestamp */
        {
            if (opt_length == 10U)
            {
                uint32_t ts_val = (opt_data[opt_offset_temp + 2U] << 24U) |
                                (opt_data[opt_offset_temp + 3U] << 16U) |
                                (opt_data[opt_offset_temp + 4U] << 8U) |
                                opt_data[opt_offset_temp + 5U];
                uint32_t ts_echo_rpl = (opt_data[opt_offset_temp + 6U] << 24U) |
                                    (opt_data[opt_offset_temp + 7U] << 16U) |
                                    (opt_data[opt_offset_temp + 8U] << 8U) |
                                    opt_data[opt_offset_temp + 9U];
                /* 更新 tcb 时间戳 */
                if (tcb != NULL)
                {
                    tcb->ts_val = ts_val;
                    tcb->ts_echo_rpl = ts_echo_rpl;
                    tcb->recent_ts = ts_val;

                    /* RTT 测量 */
                    if (ts_echo_rpl > 0U)
                    {
                        uint32_t current_time = get_current_time_ms();
                        uint32_t rtt = current_time - ts_echo_rpl;
                        if (rtt < tcb->cong_ctrl.rtt.rtt_min)
                        {
                            tcb->cong_ctrl.rtt.rtt_min = rtt;
                        }
                    }
                }
            }
        }

        if (opt_length == 0U)
        {
            opt_offset_temp++;
        }
        else
        {
            opt_offset_temp += opt_length;
        }
    }
}
```

### Step 3：在 tcp_process_segment() 函数中添加 CUBIC 拥塞控制处理

在 TCP 状态机处理之前，添加 CUBIC 拥塞控制处理代码：

```c
/* 处理 ACK：集成 CUBIC 拥塞控制 */
if (((flags & TCP_FLAG_ACK) != 0U) && (tcb != NULL))
{
    /* 检查重复 ACK */
    if (ack_num == tcb->cong_ctrl.last_ack)
    {
        tcb->cong_ctrl.dup_acks++;
    }
    else
    {
        /* 新 ACK */
        tcb->cong_ctrl.last_ack = ack_num;
        tcb->cong_ctrl.dup_acks = 0U;

        /* CUBIC 处理新 ACK */
        if (ack_num > tcb->snd_una)
        {
            tcb->snd_una = ack_num;
            
            /* 调用 CUBIC 拥塞控制 */
            if (tcb->cubic_state == 0U)  /* 慢启动 */
            {
                tcb->cubic_cwnd += TCP_MSS;
                if (tcb->cubic_cwnd >= tcb->cubic_ssthresh)
                {
                    tcb->cubic_state = 1U;  /* 拥塞避免 */
                }
            }
            else if (tcb->cubic_state == 1U)  /* 拥塞避免 */
            {
                /* 简化的 CUBIC 算法 */
                tcb->cubic_cwnd += (TCP_MSS * TCP_MSS) / tcb->cubic_cwnd;
            }
        }

        /* 检查重复 ACK（CUBIC 快速重传） */
        if (tcb->cong_ctrl.dup_acks >= 3U)
        {
            /* 快速重传 */
            tcb->cubic_w_max = tcb->cubic_cwnd;
            tcb->cubic_cwnd = (uint32_t)((float)tcb->cubic_cwnd * 0.7f);
            tcb->cubic_ssthresh = tcb->cubic_cwnd;
            tcb->cubic_state = 2U;  /* 快速恢复 */
        }
    }
}
```

### Step 4：在 TCP_ESTABLISHED 状态中添加 Keepalive 处理

在 `TCP_ESTABLISHED` 状态的开始处，添加 Keepalive 处理代码：

```c
case TCP_ESTABLISHED:
    /* 更新 Keepalive 最后活跃时间 */
    tcb->keepalive_last_active = get_current_time_ms() / 1000U;
    tcb->keepalive_probe_count = 0U;
    tcb->keepalive_timeout = false;

    /* 处理数据 */
    if (payload_len > 0U)
    {
        /* ... 原有代码 ... */
    }
    break;
```

### Step 5：修改 tcp_send_segment() 函数以支持 TCP 选项

在 `tcp_send_segment()` 函数中，添加 TCP 选项构造代码：

```c
static kernel_status_t tcp_send_segment(tcp_tcb_t *tcb, uint8_t flags,
                                          const void *data, uint32_t len)
{
    /* ... 原有代码 ... */

    /* TCP 选项构造 */
    uint8_t options[40];  /* 最大 TCP 选项长度 */
    uint16_t opt_len = 0U;

    /* 添加 MSS 选项（仅在 SYN 段中） */
    if ((flags & TCP_FLAG_SYN) != 0U)
    {
        options[opt_len++] = 2U;  /* MSS kind */
        options[opt_len++] = 4U;  /* length */
        options[opt_len++] = (tcb->mss >> 8U) & 0xFFU;
        options[opt_len++] = tcb->mss & 0xFFU;
    }

    /* 添加窗口缩放选项（仅在 SYN 段中） */
    if ((flags & TCP_FLAG_SYN) != 0U)
    {
        options[opt_len++] = 3U;  /* Window Scale kind */
        options[opt_len++] = 3U;  /* length */
        options[opt_len++] = tcb->window_scale;
    }

    /* 添加 SACK 选项（仅在 SYN 段中） */
    if ((flags & TCP_FLAG_SYN) != 0U)
    {
        options[opt_len++] = 4U;  /* SACK Permitted kind */
        options[opt_len++] = 2U;  /* length */
    }

    /* 添加时间戳选项（所有段） */
    if (tcb->ts_enabled)
    {
        options[opt_len++] = 8U;  /* Timestamp kind */
        options[opt_len++] = 10U; /* length */
        options[opt_len++] = (tcb->ts_val >> 24U) & 0xFFU;
        options[opt_len++] = (tcb->ts_val >> 16U) & 0xFFU;
        options[opt_len++] = (tcb->ts_val >> 8U) & 0xFFU;
        options[opt_len++] = tcb->ts_val & 0xFFU;
        options[opt_len++] = (tcb->ts_echo_rpl >> 24U) & 0xFFU;
        options[opt_len++] = (tcb->ts_echo_rpl >> 16U) & 0xFFU;
        options[opt_len++] = (tcb->ts_echo_rpl >> 8U) & 0xFFU;
        options[opt_len++] = tcb->ts_echo_rpl & 0xFFU;
    }

    /* 添加选项结束标记 */
    if (opt_len > 0U)
    {
        options[opt_len++] = 0U;  /* End of options */
    }

    /* 更新 data_offset 以包含选项 */
    if (opt_len > 0U)
    {
        tcp->data_offset = (5U + (opt_len / 4U)) << 4U;
        
        /* 复制选项到 TCP 头部 */
        (void)memcpy(&segment[TCP_HDR_SIZE], options, opt_len);
    }

    /* ... 原有代码 ... */
}
```

### Step 6：添加 Keepalive 定时器处理函数

在 `tcp_retransmit_check()` 函数附近，添加 Keepalive 定时器处理函数：

```c
/**
 * @brief TCP Keepalive 定时器处理
 *
 * @details 检查所有活跃的 TCB 中是否有需要发送 Keepalive 探测的连接
 */
static void tcp_keepalive_check(void)
{
    uint32_t i;
    uint32_t current_time;

    current_time = get_current_time_ms() / 1000U;  /* 转换为秒 */

    for (i = 0U; i < NET_MAX_SOCKETS; i++)
    {
        tcp_tcb_t *tcb = &s_tcp_tcbs[i];

        if (!tcb->in_use || (tcb->state == TCP_CLOSED))
        {
            continue;
        }

        if (!tcb->keepalive_enabled)
        {
            continue;
        }

        /* 检查是否应该发送 Keepalive 探测 */
        if ((current_time - tcb->keepalive_last_active) >= 7200U)  /* 2 小时 */
        {
            /* 检查探测间隔 */
            if ((current_time >= tcb->keepalive_next_probe) &&
                (tcb->keepalive_probe_count < 9U))
            {
                /* 发送 Keepalive 探测 */
                (void)tcp_send_segment(tcb, TCP_FLAG_ACK, NULL, 0U);
                tcb->keepalive_probe_count++;
                tcb->keepalive_next_probe = current_time + 75U;  /* 75 秒 */

                /* 检查是否超时 */
                if (tcb->keepalive_probe_count >= 9U)
                {
                    tcb->keepalive_timeout = true;
                    /* 关闭连接 */
                    tcb->state = TCP_CLOSED;
                    tcb->in_use = false;
                }
            }
        }
    }
}
```

### Step 7：在主循环中调用 Keepalive 定时器检查

在主网络处理循环中，添加 Keepalive 定时器检查调用：

```c
while (1)
{
    /* 处理接收到的数据包 */
    eth_process_frame(...);

    /* 检查重传定时器 */
    tcp_retransmit_check();

    /* 检查 Keepalive 定时器 */
    tcp_keepalive_check();
}
```

---

## 集成验证

### 编译验证

```bash
cd build
make net.elf -j4
```

### 功能验证

1. **TCP 选项解析验证**
   - 连接建立时检查 MSS 协商
   - 检查窗口缩放协商
   - 检查 SACK 协商
   - 检查时间戳协商

2. **CUBIC 拥塞控制验证**
   - 检查窗口增长
   - 检查快速重传
   - 检查快速恢复

3. **TCP 时间戳验证**
   - 检查时间戳回显
   - 检查 RTT 测量

4. **TCP Keepalive 验证**
   - 检查 Keepalive 探测发送
   - 检查 Keepalive 超时处理

---

## 注意事项

1. **MISRA C:2012 合规**
   - 确保所有代码符合 MISRA C:2012 规范
   - 检查类型转换
   - 检查未使用的变量

2. **错误处理**
   - 添加足够的错误检查
   - 处理边界条件

3. **性能考虑**
   - TCP 选项处理不应成为性能瓶颈
   - Keepalive 探测不应影响正常连接

4. **安全性**
   - 验证时间戳选项的合法性
   - 验证 TCP 选项的长度和类型
   - 防止缓冲区溢出

---

## 总结

本文档提供了在 `tcp_process_segment()` 函数中集成所有新功能的详细指南。按照本文档的步骤进行集成，可以确保所有新功能正确集成到网络协议栈中。

---

**文档版本**：1.0
**创建日期**：2026-04-17
**创建人员**：AISafe64 Team
**项目名称**：AISafeOS64 微内核操作系统
