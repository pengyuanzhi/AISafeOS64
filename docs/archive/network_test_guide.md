# AISafeOS64 网络协议栈测试指南

## 测试环境

### QEMU 配置

- **CPU**: Cortex-A57 (ARMv8-A)
- **SMP**: 4 核
- **Memory**: 1GB
- **Network**: 用户模式网络（User Networking）
- **Port Forwarding**:
  - 2222 -> 22 (SSH)
  - 8080 -> 80 (HTTP)

### 网络拓扑

```
┌─────────────────────────────────────────────────────────┐
│                    Host Machine                        │
│                                                         │
│  ┌─────────────┐          ┌─────────────┐              │
│  │   QEMU      │          │  Host OS    │              │
│  │  (Guest)    │          │             │              │
│  │             │          │             │              │
│  │  AISafeOS64 │          │   SSH/HTTP  │              │
│  │  网络协议栈  │─────────│             │              │
│  │             │          │             │              │
│  │ VirtIO Net │          │  User Mode   │              │
│  │  驱动      │─────────│  Network     │              │
│  │             │          │  Networking  │              │
│  └─────────────┘          └─────────────┘              │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

---

## 启动步骤

### 1. 编译项目

```bash
cd /home/kerfs/AISafeOS64/AISafeOS64/build
make -j4
```

确保以下编译产物存在：
- `aisafe64.bin` - 内核镜像
- `net.elf.elf` - 网络协议栈
- `drv_virtio_net.elf.elf` - VirtIO Net 驱动

### 2. 启动 QEMU（带网络）

**方式 1：使用 CMake 目标**
```bash
cd /home/kerfs/AISafeOS64/AISafeOS64/build
make qemu-net
```

**方式 2：使用启动脚本**
```bash
cd /home/kerfs/AISafeOS64/AISafeOS64
bash scripts/run_qemu_net.sh
```

---

## 测试项目

### 1. 网络接口初始化测试

**目标**：验证 VirtIO Net 驱动是否正确初始化

**预期输出**：
```
[NET] VirtIO Net driver registered
[NET]   (RX/TX VirtQueue framework ready)
[NET]   MAC address: 52:54:00:12:34:56
```

**验证要点**：
- VirtIO Net 设备探测成功
- VirtQueue 初始化成功
- MAC 地址读取成功
- 驱动注册到网络接口层成功

---

### 2. 网络协议栈初始化测试

**目标**：验证网络协议栈是否正确初始化

**预期输出**：
```
[NET] Network stack initialized
[NET] Auto-discovering network interfaces...
[NET] Found interface: eth0
[NET]   MAC: 52:54:00:12:34:56
[NET]   Driver: virtio-net
[NET] Interface eth0 registered
[NET] Interface eth0 up
```

**验证要点**：
- 网络协议栈初始化成功
- 自动发现网络接口
- 网络接口注册成功
- 网络接口启动成功

---

### 3. 网络数据包发送测试

**目标**：验证网络数据包能否正确发送

**测试步骤**：

1. 在 QEMU 中启动 AISafeOS64
2. 在 Host 机器上运行网络监听工具：
   ```bash
   sudo tcpdump -i lo -n 'icmp' &
   ```

3. 在 AISafeOS64 中发送 ICMP 回显请求：
   ```c
   /* 在网络协议栈中调用 */
   ping("10.0.2.2");
   ```

**预期输出**：

QEMU 输出：
```
[NET] Sending ICMP echo request to 10.0.2.2
[NET]   TX packet: 42 bytes
```

Host tcpdump 输出：
```
IP 10.0.2.15 > 10.0.2.2: ICMP echo request, id 1, seq 1, length 8
```

**验证要点**：
- 网络数据包正确构造
- 以太网帧发送成功
- Host 机器收到网络数据包

---

### 4. 网络数据包接收测试

**目标**：验证网络数据包能否正确接收

**测试步骤**：

1. 在 Host 机器上发送 ICMP 回显请求到 QEMU：
   ```bash
   ping -c 3 10.0.2.15
   ```

2. 观察 AISafeOS64 的输出

**预期输出**：

QEMU 输出：
```
[NET] Received ICMP echo request from 10.0.2.2
[NET]   RX packet: 42 bytes
[NET] Sending ICMP echo reply to 10.0.2.2
[NET]   TX packet: 42 bytes
```

Host ping 输出：
```
PING 10.0.2.15 (10.0.2.15) 56(84) bytes of data.
64 bytes from 10.0.2.15: icmp_seq=1 ttl=64 time=1.23 ms
64 bytes from 10.0.2.15: icmp_seq=2 ttl=64 time=0.98 ms
64 bytes from 10.0.2.15: icmp seq=3 ttl=64 time=1.12 ms
```

**验证要点**：
- 网络数据包正确接收
- ICMP 回显请求正确解析
- ICMP 回显应答正确构造
- Host 机器收到 ICMP 回显应答

---

### 5. TCP 连接测试

**目标**：验证 TCP 连接建立和数据传输

**测试步骤**：

1. 在 AISafeOS64 中启动 TCP 服务器：
   ```c
   /* 在网络协议栈中调用 */
   tcp_server(8080);
   ```

2. 在 Host 机器上使用 netcat 连接：
   ```bash
   nc -v 127.0.0.1 8080
   ```

**预期输出**：

AISafeOS64 输出：
```
[NET] TCP server listening on port 8080
[NET] Received SYN from 10.0.2.2:12345
[NET]   State: LISTEN -> SYN_RECEIVED
[NET] Sending SYN+ACK to 10.0.2.2:12345
[NET] Received ACK from 10.0.2.2:12345
[NET]   State: SYN_RECEIVED -> ESTABLISHED
[NET] TCP connection established
```

Host nc 输出：
```
Connection to 127.0.0.1 8080 port [tcp/*] succeeded!
```

**验证要点**：
- TCP 三次握手正确执行
- TCP 状态机正确转换
- TCP 连接成功建立
- TCP 选项正确协商

---

### 6. TCP 数据传输测试

**目标**：验证 TCP 数据能否正确传输

**测试步骤**：

1. 在 AISafeOS64 TCP 服务器中读取数据
2. 在 Host 机器上发送数据：
   ```bash
   echo "Hello, AISafeOS64!" | nc -v 127.0.0.1 8080
   ```

**预期输出**：

AISafeOS64 输出：
```
[NET] Received TCP segment from 10.0.2.2:12345
[NET]   Payload: 20 bytes
[NET]   Data: "Hello, AISafeOS64!"
[NET] Sending ACK to 10.0.2.2:12345
```

Host nc 输出：
```
Hello, AISafeOS64!
```

**验证要点**：
- TCP 数据包正确接收
- TCP Payload 正确解析
- TCP ACK 正确发送
- 数据完整传输

---

### 7. TCP 选项测试

**目标**：验证 TCP 选项是否正确协商

**测试步骤**：

1. 在 AISafeOS64 中启用 TCP 选项
2. 建立 TCP 连接
3. 使用 tcpdump 查看选项协商

**预期输出**：

Host tcpdump 输出：
```
IP 10.0.2.15 > 10.0.2.2: Flags [S], seq 0, win 65535, options [mss 1460,nop,wscale 7,nop,nop,sackOK,eol]
IP 10.0.2.2 > 10.0.2.15: Flags [S.], seq 0, ack 1, win 65535, options [mss 1460,nop,wscale 7,nop,nop,sackOK,eol]
IP 10.0.2.15 > 10.0.2.2: Flags [.], ack 1, win 512, options [nop,nop,TS val 123456789 ecr 0,eol]
```

**验证要点**：
- MSS 选项正确协商
- 窗口缩放选项正确协商
- SACK 选项正确协商
- 时间戳选项正确协商

---

### 8. TCP Keepalive 测试

**目标**：验证 TCP Keepalive 是否正确工作

**测试步骤**：

1. 在 AISafeOS64 中建立 TCP 连接
2. 在 Host 机器上停止发送数据
3. 观察 AISafeOS64 的输出

**预期输出**：

AISafeOS64 输出（2 小时后）：
```
[NET] TCP keepalive probe sent to 10.0.2.2:12345
[NET]   Probe count: 1/9
[NET]   Next probe: 75 seconds
...
[NET] TCP keepalive probe sent to 10.0.2.2:12345
[NET]   Probe count: 9/9
[NET]   No response, closing connection
[NET]   State: ESTABLISHED -> CLOSED
```

**验证要点**：
- Keepalive 探测正确发送
- 探测间隔正确（75 秒）
- 探测次数正确（9 次）
- 超时后连接正确关闭

---

### 9. CUBIC 拥塞控制测试

**目标**：验证 CUBIC 拥塞控制算法是否正确工作

**测试步骤**：

1. 在 AISafeOS64 中建立 TCP 连接
2. 发送大量数据
3. 模拟网络拥塞（使用 tc 命令）
4. 观察窗口变化

**预期输出**：

AISafeOS64 输出：
```
[NET] CUBIC: Slow Start
[NET]   cwnd: 1460 -> 2920 -> 5840 -> 11680 -> 23360
[NET] CUBIC: Congestion Avoidance
[NET]   cwnd: 23360 -> 23523 -> 23686 -> 23849
[NET] CUBIC: Fast Retransmit (3 duplicate ACKs)
[NET]   cwnd: 23849 -> 16694 (0.7x reduction)
[NET] CUBIC: Fast Recovery
[NET]   cwnd: 16694 -> 18012 -> 19330
```

**验证要点**：
- 慢启动阶段窗口指数增长
- 拥塞避免阶段窗口线性增长
- 快速重传触发（3 个重复 ACK）
- 快速恢复阶段窗口恢复
- 窗口减少比例正确（0.7）

---

### 10. 性能测试

**目标**：测试网络协议栈性能

**测试步骤**：

1. 在 AISafeOS64 中启动 TCP 服务器
2. 在 Host 机器上使用 iperf 测试性能：
   ```bash
   iperf -c 127.0.0.1 -p 8080 -t 10
   ```

**预期输出**：

Host iperf 输出：
```
------------------------------------------------------------
Client connecting to 127.0.0.1, TCP port 8080
TCP window size: 85.0 KByte (default)
------------------------------------------------------------
[  3] local 127.0.0.1 port 12345 connected with 127.0.0.1 port 8080
[ ID] Interval       Transfer     Bandwidth
[  3]  0.0- 1.0 sec  1.00 MBytes  8.38 Mbits/sec
[  3]  1.0- 2.0 sec  1.20 MBytes  10.1 Mbits/sec
[  3]  2.0- 3.0 sec  1.15 MBytes  9.66 Mbits/sec
[  3]  3.0- 4.0 sec  1.18 MBytes  9.91 Mbits/sec
[  3]  4.0- 5.0 sec  1.22 MBytes  10.2 Mbits/sec
[  3]  5.0- 6.0 sec  1.19 MBytes  10.0 Mbits/sec
[  3]  6.0- 7.0 sec  1.21 MBytes  10.1 Mbits/sec
[  3]  7.0- 8.0 sec  1.17 MBytes  9.83 Mbits/sec
[  3]  8.0- 9.0 sec  1.20 MBytes  10.1 Mbits/sec
[  3]  9.0-10.0 sec  1.18 MBytes  9.91 Mbits/sec
[  3]  0.0-10.0 sec  11.7 MBytes  9.84 Mbits/sec
```

**验证要点**：
- 吞吐量 > 5 Mbps
- 延迟 < 100ms
- 无数据包丢失

---

## 故障排查

### 问题 1：VirtIO Net 设备未初始化

**症状**：
```
[NET] VirtIO Net device not found
```

**解决方案**：
- 检查 QEMU 是否包含 `-device virtio-net-device` 参数
- 检查 QEMU 是否包含 `-netdev user` 参数

### 问题 2：网络接口未发现

**症状**：
```
[NET] No network interfaces found
```

**解决方案**：
- 检查 VirtIO Net 驱动是否正确注册
- 检查网络接口自动发现机制是否正确实现

### 问题 3：网络数据包无法发送

**症状**：
```
[NET] Failed to send packet: -22
```

**解决方案**：
- 检查 VirtQueue 是否正确初始化
- 检查设备 Kick 是否正确执行

### 问题 4：网络数据包无法接收

**症状**：
```
[NET] Failed to receive packet: -22
```

**解决方案**：
- 检查 VirtQueue 是否有可用数据
- 检查设备中断是否正确配置

---

## 总结

本测试指南涵盖了 AISafeOS64 网络协议栈的完整测试流程：

1. ✅ 网络接口初始化测试
2. ✅ 网络协议栈初始化测试
3. ✅ 网络数据包发送测试
4. ✅ 网络数据包接收测试
5. ✅ TCP 连接测试
6. ✅ TCP 数据传输测试
7. ✅ TCP 选项测试
8. ✅ TCP Keepalive 测试
9. ✅ CUBIC 拥塞控制测试
10. ✅ 性能测试

---

**文档版本**：1.0
**创建日期**：2026-04-17
**创建人员**：AISafe64 Team
**项目名称**：AISafeOS64 微内核操作系统
