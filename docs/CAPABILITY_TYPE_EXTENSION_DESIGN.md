# 能力类型扩展设计文档

**文档版本**: 1.0
**创建日期**: 2026-05-25
**开发者**: AISafe64 Team

---

## 1. 背景和目标

### 1.1 背景
当前 AISafeOS64 能力系统支持 10 种内核对象类型，但缺少文件系统和内存管理相关的对象类型。

### 1.2 目标
扩展能力系统，支持：
- 文件描述符（File Descriptor）
- 文件 Inode
- 内存区域（Memory Region）
- 能力继承标志

---

## 2. 新增内核对象类型

### 2.1 KOBJ_FD - 文件描述符

**用途**：表示打开的文件句柄，用于文件系统操作。

**属性**：
- 文件偏移量
- 访问模式（只读/只写/读写）
- 文件标志（O_APPEND, O_NONBLOCK 等）
- 关联的 Inode

**权限**：
- `CAP_RIGHT_READ`: 读取文件
- `CAP_RIGHT_WRITE`: 写入文件
- `CAP_RIGHT_GRANT`: 传递给其他进程
- `CAP_RIGHT_REVOKE`: 撤销子能力

### 2.2 KOBJ_INODE - 文件 Inode

**用途**：表示文件系统中的文件或目录节点。

**属性**：
- Inode 编号
- 文件类型（文件/目录/符号链接）
- 文件大小
- 权限位
- 引用计数

**权限**：
- `CAP_RIGHT_READ`: 读取 Inode 元数据
- `CAP_RIGHT_WRITE`: 修改 Inode 元数据
- `CAP_RIGHT_EXECUTE`: 执行（对可执行文件）
- `CAP_RIGHT_GRANT`: 传递给其他进程
- `CAP_RIGHT_REVOKE`: 撤销子能力

### 2.3 KOBJ_MEMORY_REGION - 内存区域

**用途**：表示物理内存区域或虚拟内存区域。

**属性**：
- 物理地址范围
- 虚拟地址范围
- 访问权限（读/写/执行）
- 缓存属性

**权限**：
- `CAP_RIGHT_READ`: 读取内存
- `CAP_RIGHT_WRITE`: 写入内存
- `CAP_RIGHT_EXECUTE`: 执行内存中的代码
- `CAP_RIGHT_GRANT`: 映射到其他进程
- `CAP_RIGHT_REVOKE`: 撤销子能力

---

## 3. 能力继承标志

### 3.1 设计思路

在某些情况下，父能力希望子能力不能被进一步派生或传播。例如：
- 安全关键文件：只允许读取，不能传递给其他进程
- 内核内存：只允许特定进程访问，不能传播

### 3.2 实现方式

在 `cap_t` 结构中添加 `inheritable` 标志：

```c
typedef struct
{
    cap_state_t     state;
    kobj_type_t     kobj_type;
    uint8_t         rights;
    uint16_t        badge;
    kobj_id_t       kobj_id;
    cap_slot_t      parent_slot;
    cap_slot_t      cspace_root;
    uint8_t         derive_depth;
    bool            inheritable;     /* 新增：能力继承标志 */
    struct list_head children;
    struct list_head sibling;
} cap_t;
```

### 3.3 使用规则

- `inheritable = true`: 子能力可以继续派生和传播（默认）
- `inheritable = false`: 子能力不能派生和传播

---

## 4. 权限矩阵更新

### 4.1 新增对象类型的权限规则

| 对象类型 | 允许权限 | 强制权限 |
|---------|---------|---------|
| KOBJ_FD | READ \| WRITE \| GRANT \| REVOKE | READ |
| KOBJ_INODE | READ \| WRITE \| EXECUTE \| GRANT \| REVOKE | READ |
| KOBJ_MEMORY_REGION | READ \| WRITE \| EXECUTE \| GRANT \| REVOKE | READ |

### 4.2 实现位置

更新 `kernel/cap/capability.c` 中的 `s_cap_type_rights_table` 数组。

---

## 5. API 接口

### 5.1 能力继承控制

```c
/**
 * @brief 设置能力继承标志
 *
 * @param cspace    CSpace 指针
 * @param slot      能力槽索引
 * @param inheritable 继承标志
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 * @return -EPERM 权限不足
 */
kernel_status_t cap_set_inheritable(cspace_t *cspace,
                                     cap_slot_t slot,
                                     bool inheritable);

/**
 * @brief 查询能力继承标志
 *
 * @param cspace    CSpace 指针
 * @param slot      能力槽索引
 * @param[out] inheritable 继承标志
 *
 * @return KERNEL_OK 成功
 * @return -EINVAL 参数无效
 */
kernel_status_t cap_get_inheritable(cspace_t *cspace,
                                     cap_slot_t slot,
                                     bool *inheritable);
```

### 5.2 能力复制时检查继承标志

在 `cap_copy()` 函数中添加检查：

```c
if (!parent_cap->inheritable) {
    return -EPERM;  /* 父能力不可继承 */
}
```

---

## 6. 测试用例

### 6.1 能力类型扩展测试

```c
/* test_capability_fd.c */
void test_fd_capability_create(void);
void test_fd_capability_permissions(void);
void test_fd_capability_copy(void);

/* test_capability_inode.c */
void test_inode_capability_create(void);
void test_inode_capability_permissions(void);
void test_inode_capability_execute(void);

/* test_capability_memory_region.c */
void test_memory_region_capability_create(void);
void test_memory_region_capability_permissions(void);
void test_memory_region_capability_mapping(void);
```

### 6.2 能力继承标志测试

```c
/* test_capability_inheritable.c */
void test_inheritable_true(void);
void test_inheritable_false(void);
void test_inheritable_copy_allowed(void);
void test_inheritable_copy_denied(void);
```

---

## 7. 实现步骤

### Step 1: 扩展内核对象类型（1天）
- [ ] 在 `include/kernel/kobject.h` 中添加 `KOBJ_FD`, `KOBJ_INODE`, `KOBJ_MEMORY_REGION`
- [ ] 更新 `KOBJ_TYPE_COUNT`
- [ ] 在 `kernel/kobject.c` 中添加对象初始化代码

### Step 2: 更新权限矩阵（0.5天）
- [ ] 在 `kernel/cap/capability.c` 中添加新的权限规则
- [ ] 更新 `s_cap_type_rights_table` 数组

### Step 3: 实现能力继承标志（0.5天）
- [ ] 在 `cap_t` 结构中添加 `inheritable` 字段
- [ ] 实现 `cap_set_inheritable()` 和 `cap_get_inheritable()`
- [ ] 在 `cap_copy()` 中添加继承标志检查

### Step 4: 编写测试用例（1天）
- [ ] 实现能力类型测试
- [ ] 实现继承标志测试
- [ ] 运行测试并验证

---

## 8. MISRA C:2012 合规性检查

### 8.1 必须遵守的规则

- Rule 5.2: 标识符不得被重用
- Rule 8.4: 声明的对象必须具有作用域
- Rule 8.5: 声明的函数必须具有原型
- Rule 10.1: 不得使用嵌套注释
- Dir 4.9: 结构体/联合体必须有 typedef

### 8.2 代码风格

- 4 空格缩进
- Allman 括号风格
- 中文注释
- 每行最多 120 字符

---

## 9. 性能考虑

- 能力继承标志检查：O(1)
- 新增权限矩阵查找：O(1)
- 内存开销：每个能力增加 1 字节（`inheritable` 标志）

---

## 10. 安全性考虑

- 禁止权限提升：子能力权限不能超过父能力
- 禁止绕过继承检查：所有能力复制操作必须检查 `inheritable` 标志
- 防止能力泄露：只有具有 `CAP_RIGHT_GRANT` 权限的能力才能传播

---

## 11. 参考资料

- seL4 能力系统文档
- Fuchsia Zircon 能力模型
- MINIX 3 能力设计

---

**最后更新**: 2026-05-25