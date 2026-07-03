# 文件系统 TODO Triage 报告

**分析日期**: 2026-07-04
**范围**: services/fs 下 95 个 TODO 标记（17 个文件）
**目的**: 分类整理技术债，指导后续清理和实现

## 概览

| 类别 | 数量 | 占比 | 处理策略 |
|------|------|------|---------|
| A 类 - 无效/过时（可清理） | 14 | 15% | 立即清理 |
| B 类 - 关键（应实现） | 55 | 58% | 按功能模块逐步实现 |
| C 类 - 延后（技术债） | 26 | 27% | 建档跟踪 |

## fs 服务构建状态

- fs.elf 用户态服务**已编译**（build/services/fs.elf.elf）
- 但当前内核 QEMU 启动**不加载 fs 服务**（CONFIG_DEBUG=0）
- partition.c 是**未编译的死代码**（不在 fs.elf 源文件列表）

## A 类 - 无效/过时 TODO（14 个，可立即清理）

### A1. partition.c 重复 stub 函数（死代码，4 个）

partition.c 未被编译（死代码），其中 4 个 stub 函数与 partition_create_delete_sync.c
重复定义。建议删除 partition.c 中这 4 个函数（或整个文件）：
- partition.c:582 `partition_create` stub
- partition.c:659 `partition_delete` stub
- partition.c:740 `partition_resize` stub
- partition.c:760 `partition_sync` stub

### A2. "TODO: 移除" 占位（3 个）

- fsck/fsck_fat32.c:190 `ret = -1; /* TODO: 移除 */`（应接 block_read_sectors）
- fsck/fsck_ext4.c:180 `ret = -1; /* TODO: 移除 */`（同上）
- block_device.c:135 `total_sectors = 0ULL; /* TODO: 移除 */`（接 device get_size）

### A3. 基础设施已就绪的占位（3 个）

- partition_mbr.c:216-217 MBR 写入占位（write_sector 已存在）
- partition_gpt_types.c:104 时间戳占位

## B 类 - 关键 TODO（55 个，按优先级）

### B1. 最高优先级：数据丢失/锁失效（6 个）

| 文件:行号 | 问题 | 影响 |
|-----------|------|------|
| fat32_cache.c:120 | 缓存淘汰不刷盘 | 脏扇区静默丢失 |
| fat32_cache.c:206 | flush_all 无实际写入 | 数据不落盘 |
| fat32_cache.c:246 | flush_sector 无实际写入 | 数据不落盘 |
| partition_gpt.c:291,410,521 | 备用 GPT 头未写入 | 分区表无冗余 |
| fs_server_ipc.c:447 | flock 线程 ID=0 | 锁机制失效 |
| hotplug.c:221 | 卸载不调用 fs_unmount | 脏数据丢失 |

### B2. fsck 完全不可用（41 个）

fsck_ext4.c（23 个）+ fsck_fat32.c（17 个）+ fsck.c（1 个）
- 超级块/BPB 读取用占位 ret=-1
- 一致性检查、孤立资源查找、修复逻辑全部未实现
- 影响：文件系统检查与修复功能完全缺失

### B3. FAT32 VFS 钩子未实现（8 个）

fat32.c 的 mount/lookup/create/read/write/mkdir/unlink/sync 全部返回 -1
影响：FAT32 文件系统不可读写

### B4. 分区表/GUID 完整性（4 个）

- partition_gpt.c:669 CRC32 未校验
- partition_gpt_types.c:108 GUID 用计数器代替随机数
- partition.c:315 备用 GPT 头

## C 类 - 延后 TODO（26 个）

### C1. fs_ipc.c 客户端桩（13 个）
所有 VFS API 的 IPC 调用桩，待 IPC 通道打通后实现

### C2. hotplug 后台监控（2 个）
设备热插拔监控线程

### C3. 日志/缓存同步钩子（2 个）
ext4/fat32 sync 钩子空实现

### C4. 时间戳/设备类型（3 个）
热插拔时间戳硬编码

### C5. 其他（6 个）
路径匹配、分区编号、设备大小获取

## 建议的实施顺序

1. **阶段 1（数据安全）**: B1 的 6 个（缓存刷盘、GPT 备用头、flock、卸载刷盘）
2. **阶段 2（fsck）**: B2 的 41 个（文件系统检查与修复）
3. **阶段 3（FAT32）**: B3 的 8 个（FAT32 读写）
4. **阶段 4（清理）**: A 类 14 个 + C 类建档
5. **阶段 5（IPC）**: C1 的 13 个（待内核 IPC 子系统修复后）
