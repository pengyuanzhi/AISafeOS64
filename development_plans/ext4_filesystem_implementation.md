# Ext4 文件系统实现计划

**创建时间**: 2026-05-06 10:05 (GMT+8)
**目标**: 实现 Ext4 文件系统，支持文件/目录操作和权限管理
**参考**: BSD ext4 实现

---

## 📋 实现范围

### 1. Ext4 超级块（Superblock）解析
- 读取 Ext4 超级块
- 解析 Ext4 特性字段
- 解析卷信息和挂载状态

### 2. Ext4 Inode 结构
- Inode 数据结构定义
- Inode 读取/写入
- Inode 缓存管理

### 3. Ext4 块位图和 Inode 位图
- 块分配/释放
- Inode 分配/释放
- 位图缓存

### 4. Ext4 目录项
- 目录项结构定义
- 目录项解析
- 目录查找

### 5. 文件操作 API
- `ext4_open()` - 打开文件
- `ext4_close()` - 关闭文件
- `ext4_read()` - 读取文件
- `ext4_write()` - 写入文件
- `ext4_create()` - 创建文件
- `ext4_delete()` - 删除文件
- `ext4_seek()` - 文件指针定位
- `ext4_stat()` - 获取文件状态

### 6. 目录操作 API
- `ext4_mkdir()` - 创建目录
- `ext4_rmdir()` - 删除目录
- `ext4_readdir()` - 读取目录列表
- `ext4_rename()` - 重命名文件/目录

### 7. 权限管理
- `ext4_chmod()` - 修改权限
- `ext4_chown()` - 修改所有者
- 权限检查机制
- 用户/组管理

---

## 📊 Ext4 数据结构

### 超级块（Superblock）
```c
typedef struct ext4_superblock
{
    uint32_t s_inodes_count;      /* Inode 总数 */
    uint32_t s_blocks_count;     /* 块总数 */
    uint32_t s_r_blocks_count;   /* 保留块数 */
    uint32_t s_free_blocks_count; /* 空闲块数 */
    uint32_t s_free_inodes_count;/* 空闲 inode 数 */
    uint32_t s_first_data_block; /* 第一个数据块 */
    uint32_t s_log_block_size;  /* 块大小 log2 */
    uint32_t s_log_frag_size;   /* 碎片大小 log2 */
    uint32_t s_blocks_per_group; /* 每组块数 */
    uint32_t s_frags_per_group; /* 每组碎片数 */
    uint32_t s_inodes_per_group;/* 每组 inode 数 */
    uint32_t s_mtime;           /* 挂载时间 */
    uint32_t s_wtime;           /* 写入时间 */
    uint16_t s_mnt_count;       /* 挂载计数 */
    uint16_t s_max_mnt_count;   /* 最大挂载计数 */
    uint16_t s_magic;           /* 魔数 0xEF53 */
    uint16_t s_state;           /* 文件系统状态 */
    /* ... 更多字段 */
} ext4_superblock_t;
```

### Inode
```c
typedef struct ext4_inode
{
    uint16_t i_mode;            /* 文件模式/类型 */
    uint16_t i_uid;             /* 用户 ID */
    uint32_t i_size;            /* 文件大小 */
    uint32_t i_atime;           /* 访问时间 */
    uint32_t i_ctime;           /* 创建时间 */
    uint32_t i_mtime;           /* 修改时间 */
    uint32_t i_dtime;           /* 删除时间 */
    uint16_t i_gid;             /* 组 ID */
    uint16_t i_links_count;      /* 链接计数 */
    uint32_t i_blocks;          /* 块数 */
    uint32_t i_flags;           /* 文件标志 */
    uint32_t i_block[15];       /* 块指针 */
    uint32_t i_generation;       /* inode 生成号 */
    /* ... 更多字段 */
} ext4_inode_t;
```

### 目录项
```c
typedef struct ext4_dir_entry
{
    uint32_t inode;             /* Inode 编号 */
    uint16_t rec_len;          /* 记录长度 */
    uint8_t  name_len;         /* 文件名长度 */
    uint8_t  file_type;        /* 文件类型 */
    char     name[EXT4_NAME_LEN]; /* 文件名 */
} ext4_dir_entry_t;
```

---

## 🎯 TDD 开发流程

### Step 1: RED - 先写测试
- 创建 `tests/test_fs_ext4.c`
- 编写单元测试覆盖所有功能
- 编译运行确认测试失败

### Step 2: GREEN - 最小实现
- 创建 `services/fs/fs_ext4/` 目录
- 实现 ext4_superblock.c
- 实现 ext4_inode.c
- 实现 ext4_block_bitmap.c
- 实现 ext4_dir.c
- 实现 ext4_file.c
- 实现权限管理
- 编译运行确认测试通过

### Step 3: REFACTOR - 重构优化
- 在测试保护下进行重构
- 检查 MISRA C:2012 合规
- 添加中文注释
- 确认所有测试仍然通过

---

## 📁 目录结构

```
services/fs/fs_ext4/
├── ext4.h              # Ext4 公共头文件
├── ext4.c              # Ext4 初始化和挂载
├── ext4_superblock.h   # 超级块头文件
├── ext4_superblock.c   # 超级块解析
├── ext4_inode.h        # Inode 头文件
├── ext4_inode.c        # Inode 操作
├── ext4_block_bitmap.h # 块位图头文件
├── ext4_block_bitmap.c # 块分配/释放
├── ext4_inode_bitmap.h # Inode 位图头文件
├── ext4_inode_bitmap.c # Inode 分配/释放
├── ext4_dir.h         # 目录项头文件
├── ext4_dir.c         # 目录操作
├── ext4_file.h        # 文件操作头文件
├── ext4_file.c        # 文件操作
├── ext4_permission.h  # 权限管理头文件
├── ext4_permission.c  # 权限检查和修改
└── ext4_types.h       # Ext4 类型定义
```

---

## 📚 参考资料

- Linux Ext4 文件系统文档
- BSD ext4 实现
- Ext4 Wikipedia
- POSIX 文件系统标准
- AISafeOS64 微内核架构设计

---

**创建时间**: 2026-05-06 10:05 (GMT+8)
**创建人**: AISafe64 编程助手 (Kernel)
**状态**: ✅ 完成
