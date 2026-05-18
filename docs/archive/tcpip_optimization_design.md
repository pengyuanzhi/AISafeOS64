# TCP/IP 协议栈高级优化设计文档

## 文档信息
- **作者**: AISafe64 Team
- **日期**: 2026-04-17
- **版本**: 1.0
- **状态**: 设计中

---

## 概述

本文档描述 AISafeOS64 TCP/IP 协议栈的高级优化功能实现方案。

---

## Phase 1: TCP 拥塞控制（CUBIC 算法）

### 1.1 拥塞控制状态机

**拥塞控制状态枚举**：
```c
typedef enum
{
    CONG_OPEN = 0,           /**< 开启状态 */
    CONG_SLOW_START,         /**< 慢启动 */
    CONG_CONGESTION_AVOIDANCE, /**< 拥塞避免 */
    CONG_FAST_RECOVERY,      /**< 快速恢复 */
    CONG_TIMEOUT             /**< 超时 */
} congestion_state_t;
```

### 1.2 CUBIC 拥塞窗口调整

**CUBIC 算法核心公式**：
```
cwnd = C(t) + w_max

其中：
- C(t) = β*(t - K)^3 + 1，当 t ≥ K
- C(t) = 1，当 t < K

K = cbrt((3*(1-β))/(4*α)) * w_max
```

**参数定义**：
```c
#define CUBIC_ALPHA 0.7f   /* 慢启动阈值调节因子 */
#define CUBIC_BETA  0.7f   /* 减速因子 */
#define CUBIC_CWND_SCALE 10  /* 拥塞窗口缩放因子 */
```

### 1.3 RTT 估算与 RTO 计算

**RTT 测量（Karn 算法）**：
```c
typedef struct
{
    uint32_t rtt_sample;       /* 最新 RTT 样本 */
    uint32_t rtt_min;          /* 最小 RTT */
    uint32_t rtt_var;          /* RTT 偏差 */
    uint32_t srtt;             /* 平滑 RTT */
    uint32_t rto;              /* 重传超时 */
} tcp_rtt_t;
```

**RTO 计算**：
```
RTO = srtt + 4 * rtt_var

初始 RTO = 1 秒
最小 RTO = 1 秒
最大 RTO = 60 秒
```

---

## Phase 2: TCP 重传优化

### 2.1 快速重传机制

**触发条件**：收到 3 个重复 ACK

```c
typedef struct
{
    uint32_t dup_acks;    /* 重复 ACK 计数器 */
    uint32_t last_ack;    /* 最后收到的 ACK */
} tcp_fast_recovery_t;
```

### 2.2 快速恢复

**快速恢复算法**：
```
1. 收到 3 个重复 ACK：ssthresh = cwnd / 2
2. ssthresh = max(ssthresh, 2 * MSS)
3. cwnd = ssthresh + 3 * MSS
4. 继续发送新数据（受 cwnd 限制）
5. 收到一个不重复的 ACK：cwnd = ssthresh，退出快速恢复
6. 收到 ACK = last_ack + 3：正常恢复
```

---

## Phase 3: TCP 分段合并

### 3.1 Nagle 算法

**触发条件**：
```
1. 有未确认的数据
2. 发送缓冲区大小 < MSS 且无 ACK
3. 手动发送（TCP_NODELAY = 0）
```

**实现逻辑**：
```c
#define TCP_NODELAY 0  /* 启用 Nagle 算法 */
#define TCP_CORK    1  /* 启用 Cork 模式 */
```

### 3.2 SACK（Selective Acknowledgment）

**SACK 选项格式**：
```
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   Kind=5   |  Length=10      |                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                              Left Edge of SACK Block 1      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                              Right Edge of SACK Block 1     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                              Left Edge of SACK Block 2      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                              Right Edge of SACK Block 2     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### 3.3 延迟 ACK

**延迟 ACK 机制**：
```
1. 收到数据包：延迟 40ms 后发送 ACK
2. 收到新数据包：取消延迟，立即发送 ACK
3. 发送新数据包：立即发送 ACK
```

---

## Phase 4: IP 分片重组

### 4.1 分片重组缓冲区

**分片条目结构**：
```c
typedef struct ip_reass_frag_t
{
    uint16_t frag_offset;     /* 分片偏移 */
    uint16_t frag_id;         /* 分片标识符 */
    uint32_t src_ip;          /* 源 IP 地址 */
    uint32_t dst_ip;          /* 目的 IP 地址 */
    uint32_t len;             /* 分片长度 */
    uint8_t  data[1500];      /* 分片数据 */
    uint32_t data_len;        /* 数据长度 */
    bool     in_use;          /* 使用标记 */
    uint64_t arrival_time;    /* 到达时间 */
    struct ip_reass_frag_t *next; /* 下一个分片 */
} ip_reass_frag_t;
```

**重组队列结构**：
```c
typedef struct ip_reass_queue_t
{
    ip_reass_frag_t *head;    /* 队列头 */
    ip_reass_frag_t *tail;    /* 队列尾 */
    uint16_t frag_id;         /* 分片标识符 */
    uint32_t src_ip;          /* 源 IP 地址 */
    uint32_t dst_ip;          /* 目的 IP 地址 */
    uint32_t total_len;       /* 总长度 */
    uint16_t header_offset;   /* IP 头偏移 */
    uint8_t  protocol;        /* 上层协议 */
    bool     in_use;          /* 使用标记 */
    uint64_t last_frag_time;  /* 最后分片到达时间 */
} ip_reass_queue_t;
```

### 4.2 重组缓冲区管理

**最大队列数**：`NET_MAX_REASS_QUEUE = 8`

**超时处理**：`REASS_TIMEOUT_MS = 60000` (60秒)

**实现函数**：
```c
/* 分片识别和管理 */
static ip_reass_queue_t *ip_find_reass_queue(
    uint32_t src_ip, uint32_t dst_ip, uint16_t frag_id);

/* 分片添加到重组队列 */
static kernel_status_t ip_add_reass_frag(
    ip_reass_queue_t *queue,
    const ipv4_header_t *ip_hdr,
    const uint8_t *payload,
    uint32_t payload_len);

/* 分片重组 */
static void ip_reassemble(ip_reass_queue_t *queue);

/* 分片超时处理 */
static void ip_reass_timeout(void);
```

---

## Phase 5: ICMP 错误消息

### 5.1 ICMP 错误消息类型

**ICMP 类型定义**：
```c
/* 类型 */
#define ICMP_TYPE_DEST_UNREACH   3   /* 目的不可达 */
#define ICMP_TYPE_TIME_EXCEEDED  11  /* 超时 */
#define ICMP_TYPE_PARAM_PROB     12  /* 参数问题 */
```

**代码定义**：
```c
#define ICMP_CODE_NET_UNREACH   0   /* 网络不可达 */
#define ICMP_CODE_HOST_UNREACH  1   /* 主机不可达 */
#define ICMP_CODE_PORT_UNREACH  3   /* 端口不可达 */
#define ICMP_CODE_FRAG_NEEDED   4   /* 需要分片 */
#define ICMP_CODE_REASS_TIME_EXCEEDED 1 /* 重组超时 */
#define ICMP_CODE_BAD_HEADER   0    /* 坏的 IP 头 */
```

### 5.2 ICMP 错误消息构造

**消息格式**：
```
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|    Type    |    Code    |      Checksum       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Internet Header + 64 bits of Original Data          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**实现函数**：
```c
/* 构造 ICMP 错误消息 */
static int64_t icmp_build_error_message(
    uint8_t type,
    uint8_t code,
    const uint8_t *orig_data,
    uint32_t orig_len);

/* 处理 IP 分片错误 */
static void icmp_send_frag_needed(
    uint32_t if_id, uint32_t src_ip, uint32_t dst_ip,
    const ipv4_header_t *ip_hdr, uint16_t frag_off);

/* 处理 IP 分片超时 */
static void icmp_send_reass_timeout(
    uint32_t if_id, uint32_t src_ip, uint32_t dst_ip,
    uint16_t frag_id);
```

---

## 架构设计

### 新增数据结构

```c
/* 拥塞控制状态 */
typedef struct tcp_congestion_ctrl_t
{
    congestion_state_t state;      /* 拥塞状态 */
    uint32_t ssthresh;            /* 慢启动阈值 */
    uint32_t cwnd;                /* 拥塞窗口 */
    uint32_t w_max;               /* 峰值窗口 */
    uint64_t last_acks;           /* 最后确认序列号 */
    uint64_t last_retrans;        /* 最后重传时间 */
    uint64_t last_rtt_sample;     /* 最后 RTT 样本 */
    tcp_rtt_t rtt;                /* RTT 测量 */
    uint32_t dup_acks;            /* 重复 ACK 计数 */
    uint32_t last_ack;            /* 最后 ACK */
} tcp_congestion_ctrl_t;
```

### 修改现有 TCB 结构

```c
/* TCB 新增字段 */
typedef struct tcp_tcb_t
{
    /* ... 现有字段 ... */

    /* 新增拥塞控制字段 */
    tcp_congestion_ctrl_t cong_ctrl;

    /* 新增 SACK 字段 */
    uint8_t sack_permitted;       /* SACK 允许标志 */
    uint8_t sack_blocks[4][2];    /* SACK 块（最多 4 个） */
    uint8_t sack_count;           /* SACK 块数量 */

    /* 新增 Nagle 算法字段 */
    uint8_t nagle_enabled;        /* Nagle 算法启用 */
    uint8_t tcp_cork;             /* Cork 模式 */
    uint8_t delayed_ack;          /* 延迟 ACK 计数 */
} tcp_tcb_t;
```

### 新增函数声明

```c
/* 拥塞控制函数 */
static void tcp_cong_slow_start(tcp_tcb_t *tcb);
static void tcp_cong_congestion_avoidance(tcp_tcb_t *tcb);
static void tcp_cong_fast_retransmit(tcp_tcb_t *tcb, uint32_t dup_acks);
static void tcp_cong_fast_recovery(tcp_tcb_t *tcb);
static void tcp_cong_timeout(tcp_tcb_t *tcb);
static void tcp_cong_new_ack(tcp_tcb_t *tcb, uint32_t ack);

/* RTT 估算函数 */
static void tcp_rtt_update(tcp_tcb_t *tcb, uint32_t rtt_sample);
static void tcp_rto_update(tcp_tcb_t *tcb);

/* Nagle 算法函数 */
static bool tcp_nagle_can_send(tcp_tcb_t *tcb, uint32_t unacked);
static void tcp_nagle_set_cork(tcp_tcb_t *tcb, bool enable);

/* SACK 函数 */
static void tcp_sack_add_block(tcp_tcb_t *tcb, uint32_t left, uint32_t right);
static bool tcp_sack_contains(tcp_tcb_t *tcb, uint32_t seq);

/* IP 分片重组函数 */
static ip_reass_queue_t *ip_find_reass_queue(
    uint32_t src_ip, uint32_t dst_ip, uint16_t frag_id);

static kernel_status_t ip_add_reass_frag(
    ip_reass_queue_t *queue,
    const ipv4_header_t *ip_hdr,
    const uint8_t *payload,
    uint32_t payload_len);

static void ip_reassemble(ip_reass_queue_t *queue);

static void ip_reass_timeout(void);

/* ICMP 错误消息函数 */
static int64_t icmp_build_error_message(
    uint8_t type,
    uint8_t code,
    const uint8_t *orig_data,
    uint32_t orig_len);

static void icmp_send_dest_unreachable(
    uint32_t if_id, uint32_t src_ip, uint32_t dst_ip,
    const ipv4_header_t *ip_hdr, uint8_t code);

static void icmp_send_time_exceeded(
    uint32_t if_id, uint32_t src_ip, uint32_t dst_ip,
    const ipv4_header_t *ip_hdr, uint8_t code);

static void icmp_send_param_problem(
    uint32_t if_id, uint32_t src_ip, uint32_t dst_ip,
    const ipv4_header_t *ip_hdr, uint8_t code, uint8_t ptr);
```

---

## 实现步骤

### Step 1: RED - 编写测试用例
1. 拥塞控制状态机测试（慢启动、拥塞避免、快速恢复）
2. CUBIC 拥塞窗口调整测试
3. RTT 估算测试
4. RTO 计算测试
5. Nagle 算法测试
6. SACK 测试
7. IP 分片重组测试（分片到达顺序、超时处理）
8. ICMP 错误消息测试

### Step 2: GREEN - 实现功能
1. 在 services/net/main.c 中添加新代码
2. 修改 TCB 结构添加新字段
3. 实现拥塞控制函数
4. 实现 RTT 估算函数
5. 实现 Nagle/SACK 函数
6. 实现 IP 分片重组函数
7. 实现 ICMP 错误消息函数

### Step 3: REFACTOR - 重构优化
1. 优化代码结构
2. 检查 MISRA C:2012 合规性
3. 添加中文注释
4. 更新统计信息

---

## 测试验证

### 单元测试
```bash
# 检查编译
cd /home/kerfs/AISafeOS64/AISafeOS64
make -j$(nproc)

# 运行 QEMU 测试
qemu-system-aarch64 \
  -M virt \
  -cpu cortex-a57 \
  -smp 4 \
  -m 1G \
  -kernel build/kernel/aisafe64.elf.elf \
  -nographic \
  -serial mon:stdio
```

### 功能测试
1. TCP 三次握手 + 慢启动测试
2. TCP 拥塞避免 + CUBIC 测试
3. TCP 快速重传测试
4. TCP Nagle 算法测试
5. IP 分片重组测试
6. ICMP 错误消息测试

---

## 预期结果

### 代码统计
- 新增代码：~1500 行
- 修改代码：~500 行
- 新增文件：2 个（tests/test_tcp_cong.c, tests/test_ip_reass.c）

### 性能优化
- 拥塞窗口：从 65535 → CUBIC 自适应调整
- RTT 估算：Karn 算法 + 自适应 RTO
- 数据传输效率：Nagle 算法减少小包
- 分片处理：完整重组缓冲区
- 错误处理：ICMP 错误消息正确路由

### MISRA C:2012 合规
- ✅ 零偏差
- ✅ 4 空格缩进
- ✅ Allman 括号
- ✅ 中文注释
- ✅ 圈复杂度 <= 10

---

## 参考文档

1. **RFC 3448**: TCP/CUBIC Congestion Control
2. **RFC 6298**: Computing TCP's Retransmission Timer
3. **RFC 896**: Path MTU Discovery
4. **RFC 2018**: Selective Acknowledgment Options
5. **RFC 1122**: Requirements for Internet Hosts
