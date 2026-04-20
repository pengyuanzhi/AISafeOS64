# AISafeOS64 网络协议栈设计文档

## 文档信息

- **项目**: AISafeOS64 微内核实时操作系统
- **模块**: 网络协议栈（Network Stack）
- **版本**: 3.0
- **日期**: 2026-04-20
- **作者**: AISafe64 Team
- **状态**: 设计完成，实现中

---

## 目录

1. [概述](#概述)
2. [架构设计](#架构设计)
3. [网络协议栈分层](#网络协议栈分层)
4. [Socket API](#socket-api)
5. [网络接口抽象层](#网络接口抽象层)
6. [协议实现](#协议实现)
7. [TCP/IP 优化功能](#tcpip-优化功能)
8. [用户态服务集成](#用户态服务集成)
9. [性能优化](#性能优化)
10. [安全考虑](#安全考虑)
11. [测试策略](#测试策略)
12. [未来工作](#未来工作)

---

## 概述

### 设计目标

AISafeOS64 网络协议栈设计目标是实现一个安全、可靠、高性能的微内核网络协议栈，满足安全关键系统（ISO 26262 ASIL-D, IEC 61508 SIL-4）的需求。

### 核心特性

1. **微内核架构**: 网络协议栈运行在用户态，通过 IPC 与内核通信
2. **Socket API 兼容**: 完整的 POSIX Socket API 实现
3. **TCP/IP 完整支持**: TCP, UDP, ICMP, IPv4, ARP
4. **高性能**: 零拷贝数据传输、中断驱动、批量处理
5. **安全**: 能力模型、访问控制、审计日志
6. **可验证**: MISRA C:2012 合规、形式化验证支持

### 设计原则

1. **最小权限原则**: 网络协议栈仅在网络服务空间内运行
2. **分层设计**: 清晰的协议分层，便于维护和扩展
3. **模块化**: 每个协议模块独立，支持热插拔
4. **可移植性**: 协议实现与硬件无关，通过 HAL 抽象层访问硬件

---

## 架构设计

### 总体架构

```
┌─────────────────────────────────────────────────────┐
│  用户态应用 (User Applications)                │
│    - 网络客户端 / 服务器应用                     │
│    - 使用 Socket API (socket/bind/listen...)      │
└─────────────────────────────────────────────────────┘
                        ↓ Socket API
┌─────────────────────────────────────────────────────┐
│  网络协议栈服务 (net_svc)                      │
│  ┌───────────────────────────────────────────────┐ │
│  │  Socket API 层                             │ │
│  │    - socket/bind/listen/accept/connect       │ │
│  │    - send/recv/sendto/recvfrom/shutdown     │ │
│  │    - setsockopt/getsockopt/ioctl            │ │
│  └───────────────────────────────────────────────┘ │
│  ┌───────────────────────────────────────────────┐ │
│  │  协议实现层                               │ │
│  │    - TCP (状态机、拥塞控制、重传）          │ │
│  │    - UDP (校验和、无连接）                 │ │
│  │    - ICMP (回显请求/应答、错误报告）        │ │
│  │    - IPv4 (分片、路由、TTL）               │ │
│  │    - ARP (缓存、请求/应答）                 │ │
│  └───────────────────────────────────────────────┘ │
│  ┌───────────────────────────────────────────────┐ │
│  │  网络接口抽象层 (net_if)                   │ │
│  │    - 统一的网卡驱动接口                     │ │
│  │    - 以太网帧收发                           │ │
│  │    - 驱动注册和管理                         │ │
│  └───────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────┘
                        ↓ net_if_send_frame/net_if_recv_frame
┌─────────────────────────────────────────────────────┐
│  网卡驱动 (Userland Drivers)                    │
│    - drv_virtio_net (VirtIO Net 驱动)          │
│    - drv_ethernet (Ethernet 驱动)               │
│    - drv_wifi (Wi-Fi 驱动)                      │
│    - drv_ppp (PPP 驱动)                        │
└─────────────────────────────────────────────────────┘
                        ↓ MMIO / DMA
┌─────────────────────────────────────────────────────┐
│  硬件层 (Hardware)                              │
│    - VirtIO 设备 (QEMU virtio-net-device)        │
│    - 以太网网卡                                │
│    - Wi-Fi 网卡                                 │
└─────────────────────────────────────────────────────┘
```

### 模块划分

| 模块 | 文件 | 行数 | 说明 |
|------|------|------|------|
| 主程序 | services/net/main.c | ~2,800 | 网络协议栈主循环和接口管理 |
| Socket API | services/net/socket.c | ~500 | Socket API 实现 |
| TCP 协议 | services/net/tcp.c | ~1,200 | TCP 状态机、重传、拥塞控制 |
| UDP 协议 | services/net/udp.c | ~300 | UDP 校验和、无连接处理 |
| ICMP 协议 | services/net/icmp.c | ~400 | ICMP 回显、错误报告 |
| IPv4 协议 | services/net/ipv4.c | ~800 | IPv4 分片、路由、TTL |
| ARP 协议 | services/net/arp.c | ~200 | ARP 缓存、请求/应答 |
| 网络接口层 | services/net/net_if/ | ~800 | 统一的网卡驱动接口 |
| TCP/IP 优化 | services/net/tcp_*.c | ~3,500 | CUBIC、时间戳、Keepalive |

---

## 网络协议栈分层

### L2 - 链路层（以太网）

**文件**: `services/net/net_if/ethernet.c`

**功能**:
- 以太网帧头解析（MAC 地址、类型）
- 以太网类型识别（IPv4、ARP、IPv6）
- 以太网帧构造

**数据结构**:
```c
typedef struct
{
    uint8_t  dst_mac[6];  /* 目标 MAC */
    uint8_t  src_mac[6];  /* 源 MAC */
    uint16_t eth_type;    /* 以太网类型 */
} eth_header_t;
```

### L3 - 网络层（IPv4）

**文件**: `services/net/main.c` (ipv4_* 函数)

**功能**:
- IPv4 数据包封装/解析
- IP 分片和重组
- IP 路由（多接口支持）
- TTL 处理
- 校验和计算

**数据结构**:
```c
typedef struct
{
    uint8_t  version_ihl;   /* 版本(4) + 头长(4) */
    uint8_t  tos;           /* 服务类型 */
    uint16_t total_length;  /* 总长度 */
    uint16_t identification;/* 标识 */
    uint16_t flags_offset;  /* 标志 + 片偏移 */
    uint8_t  ttl;           /* 生存时间 */
    uint8_t  protocol;      /* 上层协议 */
    uint16_t checksum;      /* 头部校验和 */
    uint32_t src_ip;        /* 源 IP */
    uint32_t dst_ip;        /* 目标 IP */
} ipv4_header_t;
```

### L4 - 传输层（TCP/UDP）

#### TCP 协议

**文件**: `services/net/main.c` (tcp_* 函数)

**功能**:
- TCP 状态机（11 个状态）
- 连接建立/关闭（三次握手、四次挥手）
- 滑动窗口和流量控制
- 可靠传输（ACK、重传、快速重传）
- 拥塞控制（CUBIC、慢启动、快恢复）
- Keepalive 机制

**TCP 状态机**:
```
CLOSED → LISTEN → SYN_RECEIVED → ESTABLISHED → FIN_WAIT_1
   ↑                                                      ↓
   ←──────────────── FIN_WAIT_2 ←─────────────────────────┘
   ↑                                                      ↓
   ←──────────── TIME_WAIT ←──────────────────────────────┘
```

**数据结构**:
```c
typedef enum
{
    TCP_CLOSED = 0,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_CLOSING,
    TCP_LAST_ACK,
    TCP_TIME_WAIT
} tcp_state_t;

typedef struct
{
    uint32_t sock_id;        /* 关联套接字 ID */
    tcp_state_t state;      /* TCP 状态 */
    uint32_t local_ip;       /* 本地 IP */
    uint32_t remote_ip;      /* 远端 IP */
    uint16_t local_port;     /* 本地端口 */
    uint16_t remote_port;    /* 远端端口 */
    uint32_t snd_una;        /* 发送未确认 */
    uint32_t snd_nxt;        /* 下一个发送 */
    uint32_t snd_wnd;        /* 发送窗口 */
    uint32_t rcv_nxt;        /* 下一个期望接收 */
    uint32_t rcv_wnd;        /* 接收窗口 */
    uint8_t  recv_buf[1460 * 4]; /* 接收缓冲 */
    uint32_t recv_len;       /* 接收缓冲已用长度 */
    /* ... 拥塞控制、重传队列等 */
} tcp_tcb_t;
```

#### UDP 协议

**文件**: `services/net/main.c` (udp_* 函数)

**功能**:
- UDP 数据包封装/解析
- UDP 校验和计算（含伪首部）
- UDP 接收队列管理（每个 socket 独立）
- 无连接通信支持（sendto/recvfrom）

**数据结构**:
```c
typedef struct
{
    uint16_t src_port;  /* 源端口 */
    uint16_t dst_port;  /* 目标端口 */
    uint16_t length;    /* 长度 */
    uint16_t checksum;  /* 校验和 */
} udp_header_t;

#define UDP_RX_QUEUE_DEPTH 16

typedef struct
{
    bool in_use;
    uint32_t sock_id;
    net_sockaddr_t src_addr;
    uint8_t data[1514];
    uint32_t len;
} udp_rx_entry_t;
```

### L5 - 应用层（Socket API）

**文件**: `services/net/main.c` (net_* 函数)

**功能**:
- Socket 创建和关闭
- 绑定、监听、接受连接
- 连接建立（TCP）
- 数据发送和接收
- 选项设置和获取
- 控制操作

**Socket 类型**:
- `SOCK_STREAM`: TCP 流式套接字
- `SOCK_DGRAM`: UDP 数据报套接字
- `SOCK_RAW`: 原始套接字（预留）

---

## Socket API

### 核心接口

#### 套接字管理

```c
int32_t net_socket(net_af_t family, net_sock_type_t type);
kernel_status_t net_bind(uint32_t sock_id, const net_sockaddr_t *addr);
kernel_status_t net_listen(uint32_t sock_id, uint32_t backlog);
int32_t net_accept(uint32_t sock_id);
kernel_status_t net_connect(uint32_t sock_id, const net_sockaddr_t *addr);
kernel_status_t net_close(uint32_t sock_id);
```

#### 数据收发

```c
/* TCP 流式数据收发 */
int64_t net_send(uint32_t sock_id, const void *buf, uint64_t size);
int64_t net_recv(uint32_t sock_id, void *buf, uint64_t size);

/* UDP 无连接数据收发 */
int64_t net_sendto(uint32_t sock_id, const void *buf, uint64_t size,
                    const net_sockaddr_t *dest_addr);
int64_t net_recvfrom(uint32_t sock_id, void *buf, uint64_t size,
                      net_sockaddr_t *src_addr);
```

#### 连接控制

```c
kernel_status_t net_shutdown(uint32_t sock_id, uint32_t how);
```

**shutdown 方式**:
- `SHUT_RD`: 关闭读方向
- `SHUT_WR`: 关闭写方向
- `SHUT_RDWR`: 关闭读写

#### 套接字选项

```c
kernel_status_t net_setsockopt(uint32_t sock_id, uint32_t level,
                                uint32_t optname, const void *optval,
                                uint32_t optlen);
kernel_status_t net_getsockopt(uint32_t sock_id, uint32_t level,
                                uint32_t optname, void *optval,
                                uint32_t *optlen);
```

**Socket 级别选项（SOL_SOCKET）**:
- `SO_REUSEADDR`: 地址复用
- `SO_KEEPALIVE`: TCP Keepalive
- `SO_RCVBUF`: 接收缓冲区大小
- `SO_SNDBUF`: 发送缓冲区大小
- `SO_BROADCAST`: 广播（UDP）

**TCP 级别选项（IPPROTO_TCP）**:
- `TCP_NODELAY`: 禁用 Nagle 算法

#### 控制操作

```c
kernel_status_t net_ioctl(uint32_t sock_id, uint32_t request, void *arg);
```

**请求类型**:
- `FIONBIO`: 设置非阻塞模式
- `FIONREAD`: 获取可读字节数

---

## 网络接口抽象层

### 设计目标

1. **统一接口**: 所有网卡驱动通过统一接口注册和管理
2. **硬件无关**: 协议栈实现与具体硬件无关
3. **可扩展**: 支持多种网卡驱动（VirtIO、Ethernet、Wi-Fi、PPP）

### 接口定义

**文件**: `services/net/net_if/net_if.h`

```c
typedef struct net_if_ops
{
    int32_t (*init)(void);                           /* 初始化 */
    int64_t (*send_frame)(const void *buf, uint64_t size);  /* 发送帧 */
    int64_t (*recv_frame)(void *buf, uint64_t size);       /* 接收帧 */
    int32_t (*close)(void);                          /* 关闭 */
    int (*is_running)(void);                           /* 运行状态 */
} net_if_ops_t;

/* 驱动注册 */
int32_t net_if_register(const char *name, const char *driver,
                        const net_if_ops_t *ops, const uint8_t mac_addr[6]);

/* 帧收发 */
int64_t net_if_send_frame(const char *name, const void *buf, uint64_t size);
int64_t net_if_recv_frame(const char *name, void *buf, uint64_t size);
```

### 自动发现机制

**文件**: `services/net/net_if/net_if_auto.c`

```c
typedef struct net_if_ops_auto
{
    char name[16];                 /* 接口名称 */
    char driver[16];               /* 驱动名称 */
    const net_if_ops_t *ops;       /* 操作接口 */
} net_if_ops_auto_t;

/* 注册网络接口到自动发现表 */
int32_t net_if_auto_register(const char *name, const char *driver,
                               const net_if_ops_t *ops);

/* 获取接口数量 */
uint32_t net_if_auto_get_count(void);

/* 通过索引获取接口名称 */
int32_t net_if_auto_get_name(uint32_t index, char *name, uint32_t len);

/* 通过接口名称获取操作接口 */
const net_if_ops_auto_t *net_if_auto_get_ops(const char *name);
```

---

## 协议实现

### TCP 协议

#### 连接建立（三次握手）

```
Client                          Server
   |                               |
   | ---- SYN (seq=x) ----------> |
   |                               |
   | <--- SYN+ACK (seq=y, ack=x+1) |
   |                               |
   | ---- ACK (ack=y+1) ---------> |
   |                               |
   |       ESTABLISHED              |
```

#### 连接关闭（四次挥手）

```
Client                          Server
   |                               |
   | ---- FIN (seq=x) ----------> |
   |                               |
   | <--- ACK (ack=x+1)          |
   |       FIN_WAIT_1               |
   |       FIN_WAIT_2               |
   |                               |
   | <--- FIN (seq=y)            |
   |                               |
   | ---- ACK (ack=y+1) ---------> |
   |                               |
   |       CLOSED                  |
```

#### 拥塞控制

**算法**: CUBIC

**状态**:
- 慢启动（Slow Start）
- 拥塞避免（Congestion Avoidance）
- 快速恢复（Fast Recovery）

**参数**:
- `cwnd`: 拥塞窗口（Congestion Window）
- `ssthresh`: 慢启动阈值（Slow Start Threshold）
- `w_max`: 峰值窗口（Maximum Window）

#### 重传机制

**超时计算**:
```
RTO = SRTT + 4 * RTTVAR
```

- `SRTT`: 平滑往返时间（Smoothed RTT）
- `RTTVAR`: RTT 偏差（RTT Variation）

**快速重传**:
- 接收到 3 个重复 ACK
- 立即重传丢失的数据包

### UDP 协议

#### 校验和计算

**伪首部**:
```
+----------------+----------------+----------------+----------------+
| 源 IP (32 位) | 目标 IP (32 位) | 零 (8 位)    | 协议 (8 位)   |
+----------------+----------------+----------------+----------------+
| UDP 长度 (16 位)                                              |
+----------------+----------------+----------------+----------------+
```

**校验和算法**:
1. 构造伪首部 + UDP 头部 + 数据
2. 按 16 位对齐
3. 计算所有 16 位字的和（包括进位）
4. 取反

---

## TCP/IP 优化功能

### 拥塞控制（CUBIC）

**文件**: `services/net/tcp_cubic.c`

**特点**:
- 函数 W(t) = C(t - K)^3 + W_max
- 适合高带宽、高延迟网络
- 公平性好
- 计算复杂度低

### TCP 时间戳

**文件**: `services/net/tcp_timestamp.c`

**特点**:
- PAWS (Protect Against Wrapped Sequence Numbers)
- RTT 测量更精确
- 避免序列号回绕问题

### TCP Keepalive

**文件**: `services/net/tcp_keepalive.c`

**特点**:
- 检测连接存活状态
- 发送保活探测包
- 超时后关闭连接

**参数**:
- `keepalive_idle`: 空闲超时（默认 7200 秒）
- `keepalive_interval`: 探测间隔（默认 75 秒）
- `keepalive_count`: 最大探测次数（默认 9 次）

### TCP 选项处理

**文件**: `services/net/tcp_options.c`

**支持的选项**:
- MSS (Maximum Segment Size)
- Window Scale (窗口缩放)
- Timestamp (时间戳)
- SACK (Selective Acknowledgment)

### Nagle 算法

**文件**: `services/net/tcp_nagle_sack.c`

**特点**:
- 减少小包发送
- 提高网络利用率
- `TCP_NODELAY` 可禁用

### SACK (Selective Acknowledgment)

**文件**: `services/net/tcp_nagle_sack.c`

**特点**:
- 选择性确认丢失的数据块
- 提高丢包恢复效率
- 最多支持 4 个 SACK 块

---

## 用户态服务集成

### 服务启动顺序

```
1. 内核启动
2. init 服务启动
3. 路径服务 (path_svc) 启动
4. 内存服务 (mem_svc) 启动
5. 进程管理器 (proc_svc) 启动
6. 文件系统服务 (fs_svc) 启动
7. 网络协议栈服务 (net_svc) 启动
8. 网卡驱动 (drv_virtio_net) 启动
```

### IPC 通信

网络协议栈通过 IPC 与其他用户态服务通信：

**与 FS 服务**:
- 读取网络配置文件
- 写入网络日志文件

**与 PROC 服务**:
- 创建网络进程
- 管理网络进程生命周期
- 监控网络进程状态

**与内核**:
- 系统调用（SVC）
- 能力传递
- 内存映射

---

## 性能优化

### 零拷贝数据传输

**机制**:
1. 网络协议栈在用户态分配缓冲区
2. 驱动通过 DMA 直接访问用户态缓冲区
3. 避免内核态和用户态之间的数据复制

**实现**:
- 使用 `ipc_send_msg()` 直接传递缓冲区指针
- 驱动通过能力访问缓冲区

### 中断驱动

**机制**:
1. 网卡产生中断
2. 中断通过 IPC 投递到网络协议栈
3. 网络协议栈处理数据包

**实现**:
- `irq_enable()` 使能网卡中断
- `irq_disable()` 禁用网卡中断
- 中断处理程序：接收数据包、加入接收队列

### 批量处理

**机制**:
1. 一次处理多个数据包
2. 减少上下文切换次数
3. 提高吞吐量

**实现**:
- 环形缓冲区
- 批量接收
- 批量发送

---

## 安全考虑

### 访问控制

**机制**:
1. 套接字能力（Socket Capabilities）
2. 端口绑定权限
3. 原始套接字限制

**实现**:
- Socket 创建时分配能力
- 端口绑定时检查能力
- 原始套接字需要特权能力

### 审计日志

**机制**:
1. 记录所有网络操作
2. 包括时间、进程、操作类型
3. 支持事后审计

**实现**:
- 日志函数：`net_log()`
- 日志级别：`DEBUG`, `INFO`, `WARN`, `ERROR`
- 日志输出：文件、控制台

### 输入验证

**机制**:
1. 参数边界检查
2. 协议合规性验证
3. 防止缓冲区溢出

**实现**:
- 所有公共 API 进行参数验证
- 使用安全的字符串操作（`memcpy`, `memset`）
- 缓冲区大小检查

---

## 测试策略

### 单元测试

**文件**: `tests/test_net_socket_api.c`, `tests/net_test.c`

**覆盖**:
- Socket API 接口测试
- 协议实现测试
- 边界条件测试
- 错误处理测试

### 集成测试

**文件**: `services/test_net_integration.c`, `services/test_net_integration_services.c`

**覆盖**:
- 网络协议栈与网卡驱动集成
- 网络协议栈与用户态服务集成
- 多进程网络通信测试
- 实际网络通信测试

### 性能测试

**指标**:
- 吞吐量（Mbps）
- 延迟（ms）
- 丢包率（%）
- CPU 使用率（%）

**工具**:
- QEMU 网络性能测试
- Iperf 性能测试
- Wireshark 协议分析

### QEMU 测试

**文件**: `scripts/run_qemu_net.sh`, `scripts/run_net_test.sh`

**配置**:
- 用户模式网络（User Networking）
- 端口转发（2222->SSH, 8080->HTTP）
- 多核支持（SMP 4）

---

## 未来工作

### 短期（Phase 5）

- [ ] IPv6 支持
- [ ] 原始套接字（SOCK_RAW）
- [ ] 套接字选项扩展（TCP_DEFER_ACCEPT, SO_LINGER 等）
- [ ] 网络统计信息完善
- [ ] QEMU 实际运行测试

### 中期（Phase 6）

- [ ] 多核网络优化
- [ ] 网络协议栈卸载（硬件加速）
- [ ] 安全协议支持（TLS/SSL）
- [ ] 网络服务质量（QoS）
- [ ] 网络监控和管理工具

### 长期（Phase 7）

- [ ] SDN（软件定义网络）支持
- [ ] 容器网络支持
- [ ] 网络功能虚拟化（NFV）
- [ ] 边缘计算网络支持

---

## 附录

### 参考文档

1. **RFC 791**: Internet Protocol (IPv4)
2. **RFC 792**: Internet Control Message Protocol (ICMP)
3. **RFC 768**: User Datagram Protocol (UDP)
4. **RFC 793**: Transmission Control Protocol (TCP)
5. **RFC 826**: Ethernet Address Resolution Protocol (ARP)
6. **RFC 1122**: Requirements for Internet Hosts
7. **RFC 2581**: TCP Congestion Control
8. **RFC 8312**: CUBIC Congestion Control for TCP
9. **POSIX.1-2017**: IEEE Std 1003.1-2017

### 相关文件

- **ARCHITECTURE.md**: AISafeOS64 总体架构设计
- **KERNEL.md**: 内核设计文档
- **MEMORY.md**: 开发日志和决策记录

---

**文档结束**
