# lib/musl_legacy - 已废弃

## 状态
**❌ 已废弃** - 用户态服务已 100% 迁移到标准 musl

## 废弃日期
2026-04-25

## 废弃原因

用户态服务已全部迁移到标准 musl + musl_aisafe 适配层，此目录不再使用。

### 迁移详情
- **新位置**: `lib/musl_aisafe/`
- **架构**: 标准 musl 上游 + 最小适配层
- **参考**: seL4/musllibc 方案

### 迁移状态
- ✅ 用户态服务 100% 完成 (init/mem/proc/net/path)
- ✅ musl_aisafe v2.0 完成
- ✅ 编译验证通过
- ✅ 旧版测试已删除

## 文件内容

此目录包含手写的 musl 子集实现（~3,786 行），已在迁移阶段验证：

### 字符串函数
- memcpy.c, memset.c, memmove.c, memcmp.c
- memchr.c, strlen.c, strcmp.c, strncmp.c
- strcpy.c, strncpy.c, strcat.c, strncat.c
- strchr.c, strrchr.c, strstr.c

### stdio 函数
- vsnprintf.c, sprintf.c, snprintf.c

### stdlib 函数
- atoi.c, strtol.c, strtoul.c
- malloc.c, calloc.c, realloc.c
- exit.c, abort.c, atexit.c

### errno 函数
- errno.c

## 测试覆盖

**已删除的旧版测试**:
- test_musl_string.c (247 个测试，全部通过)
- test_musl_stdio.c (测试用例)
- test_musl_stdlib.c (测试用例)

**新版测试**:
- test_syscall_dispatch (使用 musl_aisafe)
- test_musl_safety (使用 musl_aisafe)
- test_posix_api (使用 musl_aisafe)

## 保留建议

保留此目录作为历史参考，建议在以下情况后删除：
1. musl_aisafe 适配层完全稳定后
2. 所有功能测试覆盖新版 musl 后
3. 安全认证完成后

## 参考

- **迁移状态**: `docs/MUSL_MIGRATION_STATUS.md`
- **musl_aisafe 文档**: `docs/musl_aisafe_v2.md`
- **项目状态**: `docs/PROJECT_STATUS.md`

---

**生成日期**: 2026-04-25 10:43 (GMT+8)
**文档版本**: 1.0
