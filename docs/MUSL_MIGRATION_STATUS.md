# 用户态服务迁移到标准 musl 状态

## 更新日期
2026-04-25 10:43 (GMT+8)

---

## 📊 迁移进度

### 整体迁移进度
- **用户态服务**: ✅ 100% 完成
- **测试系统**: ⏳ 80% 完成 (旧版测试待处理)
- **整体进度**: ~95%

---

## ✅ 已完成迁移

### 1. 用户态服务 (100% 完成)

| 服务 | 状态 | 链接库 | 文件大小 |
|------|------|---------|---------|
| init.elf | ✅ 已迁移 | musl_aisafe | 11,168 bytes |
| mem.elf | ✅ 已迁移 | musl_aisafe | 5,104 bytes |
| proc.elf | ✅ 已迁移 | musl_aisafe | 10,424 bytes |
| net.elf | ✅ 已迁移 | musl_aisafe | 80,800 bytes |
| path.elf | ✅ 已迁移 | musl_aisafe | 待确认 |
| drv_virtio_net.elf | ✅ 已迁移 | musl_aisafe | 70,840 bytes |

**验证结果**:
- ✅ 所有用户态服务编译成功
- ✅ 使用标准 musl 上游 + musl_aisafe 适配层
- ✅ 未使用 lib/musl_legacy/

### 2. musl_aisafe 适配层 (v2.0 完成)

**核心功能**:
- ✅ syscall_arch.h - __sysinfo 函数指针路由
- ✅ syscall_dispatch.c - Linux syscall → AISafeOS64 SVC 翻译 (40+ syscall)
- ✅ musl_safety.c - 参数验证 + 审计日志
  - musl_audit_log_printf() - 格式化审计日志输出
  - musl_audit_log_syscall() - 系统调用审计
  - musl_audit_log_event() - 安全事件审计
- ✅ other_syscalls.c - uname/pipe2/sysinfo/getrlimit/setrlimit
  - gettimeofday/clock_gettime/clock_getres (时间系统调用)
- ✅ fs_ipc.c - FS IPC 客户端
  - open/close/read/write
  - lseek (文件定位)
  - fstat/ioctl/fcntl (预留接口，TODO)

**编译验证**:
- ✅ musl_aisafe.a 编译成功
- ✅ 所有用户态服务编译成功

### 3. CMakeLists.txt 更新

**已完成**:
- ✅ 用户态服务链接 musl_aisafe
- ✅ 移除对 lib/musl_legacy/ 的依赖
- ✅ 更新头文件搜索路径
- ✅ Phase 3 迁移标记

---

## ⏳ 待处理

### 1. 旧版测试 (保留还是删除？)

**旧版测试列表**:
| 测试 | 文件 | 状态 | 说明 |
|------|------|------|------|
| test_musl_string | tests/test_musl_string.c | ⏳ 待处理 | 使用 lib/musl_legacy/ 字符串函数 |
| test_musl_stdio | tests/test_musl_stdio.c | ⏳ 待处理 | 使用 lib/musl_legacy/ stdio 函数 |
| test_musl_stdlib | tests/test_musl_stdlib.c | ⏳ 待处理 | 使用 lib/musl_legacy/ stdlib 函数 |

**CMakeLists.txt 引用**:
```cmake
# 行 190-204: 旧版测试定义
add_executable(test_musl_string tests/test_musl_string.c ${MUSL_LEGACY_STRING_SOURCES} ${MUSL_LEGACY_ERRNO_SOURCES})
add_executable(test_musl_stdio tests/test_musl_stdio.c ${MUSL_LEGACY_STRING_SOURCES} ${MUSL_LEGACY_STDIO_SOURCES} ${MUSL_LEGACY_ERRNO_SOURCES})
add_executable(test_musl_stdlib tests/test_musl_stdlib.c ${MUSL_LEGACY_STRING_SOURCES} ${MUSL_LEGACY_STDLIB_SOURCES} ${MUSL_LEGACY_ERRNO_SOURCES})
```

**问题**: 这些测试是否需要保留？

### 2. 新版测试 (使用 musl_aisafe)

**新版测试列表**:
| 测试 | 状态 | 说明 |
|------|------|------|
| test_syscall_dispatch | ✅ 已完成 | musl 系统调用分发器测试 |
| test_musl_safety | ✅ 已完成 | musl 功能安全包装测试 |
| test_posix_api | ✅ 已完成 | POSIX API 集成测试 (编译成功) |

---

## 🔍 迁移验证

### 用户态服务代码检查
```bash
# 检查是否还有使用 lib/musl_legacy/ 的地方
$ find services -name "*.c" | xargs grep -l "lib/musl_legacy\|\"../musl_legacy"
# 结果: 没有找到使用 lib/musl_legacy 的地方 ✅
```

### CMakeLists.txt 引用检查
```bash
# 检查对 musl_legacy 的引用
$ grep -rn "musl_legacy" CMakeLists.txt services/CMakeLists.txt
# 结果:
# - CMakeLists.txt: 旧版测试定义 (保留向后兼容)
# - services/CMakeLists.txt: Phase 3 迁移标记
```

---

## 🎯 后续计划

### 选项 A: 删除旧版测试（推荐）
- [ ] 删除 test_musl_string/stdio/stdlib 测试
- [ ] 删除 CMakeLists.txt 中旧版测试定义
- [ ] 删除 tests/test_musl_*.c 文件
- [ ] 保留 lib/musl_legacy/ 作为参考

### 选项 B: 迁移旧版测试到 musl_aisafe
- [ ] 更新 test_musl_string 使用 musl_aisafe
- [ ] 更新 test_musl_stdio 使用 musl_aisafe
- [ ] 更新 test_musl_stdlib 使用 musl_aisafe
- [ ] 验证测试覆盖

### 选项 C: 标记为已废弃（中间方案）
- [ ] 在 CMakeLists.txt 中添加注释标记为废弃
- [ ] 在 README 中说明旧版测试已废弃
- [ ] 推荐使用新版测试

---

## 📋 迁移检查清单

- [x] 用户态服务迁移完成
- [x] musl_aisafe 适配层 v2.0 完成
- [x] 编译验证通过
- [x] 无运行时依赖 lib/musl_legacy/
- [ ] 旧版测试处理（删除/迁移/标记废弃）
- [ ] lib/musl_legacy/ 标记为已废弃
- [ ] 文档更新 (README/MIGRATION_GUIDE)

---

## 📄 相关文档

- **musl_aisafe v2.0**: docs/musl_aisafe_v2.md
- **项目状态**: docs/PROJECT_STATUS.md
- **每日记录**: memory/2026-04-25.md

---

## 📦 推荐方案

**推荐: 选项 A - 删除旧版测试**

**理由**:
1. 用户态服务已经 100% 迁移到 musl_aisafe
2. 新版测试 (test_syscall_dispatch, test_musl_safety) 已覆盖功能
3. 旧版测试使用的是手写的 musl 子集，不再代表标准 musl 行为
4. 保留旧版测试会造成混淆

**执行步骤**:
1. 从 CMakeLists.txt 删除旧版测试定义 (行 190-204)
2. 从 tests/ 删除 test_musl_*.c 文件
3. 在 lib/musl_legacy/ 添加 README.md 标记为已废弃
4. 更新 docs/MIGRATION_GUIDE.md

---

**生成日期**: 2026-04-25 10:43 (GMT+8)
**文档版本**: 1.0
