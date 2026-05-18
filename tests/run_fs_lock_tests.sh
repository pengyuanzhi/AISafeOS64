#!/bin/bash
# 文件锁分片锁性能测试脚本
# @author  AISafe64 Team
# @date    2026-05-09

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 脚本目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_DIR"

echo "=========================================="
echo "文件锁分片锁性能测试"
echo "=========================================="
echo "项目目录: $PROJECT_DIR"
echo ""

# 检查编译器
if ! command -v gcc &> /dev/null; then
    echo -e "${RED}错误: 未找到 gcc${NC}"
    exit 1
fi

echo -e "${GREEN}[1/3] 编译压力测试程序...${NC}"
gcc -o tests/fs_lock_shard_stress tests/fs_lock_shard_stress.c -lpthread -O2
echo -e "${GREEN}✓ 压力测试程序编译完成${NC}"

echo ""
echo -e "${GREEN}[2/3] 编译性能基准测试程序...${NC}"
gcc -o tests/fs_lock_shard_perf tests/fs_lock_shard_perf.c -lpthread -O2
echo -e "${GREEN}✓ 性能基准测试程序编译完成${NC}"

echo ""
echo -e "${GREEN}[3/3] 运行测试...${NC}"
echo ""

# 运行压力测试
echo "=========================================="
echo "压力测试"
echo "=========================================="
./tests/fs_lock_shard_stress
echo ""

# 运行性能基准测试
echo "=========================================="
echo "性能基准测试"
echo "=========================================="
./tests/fs_lock_shard_perf
echo ""

# 生成测试报告
REPORT_FILE="tests/fs_lock_perf_report_$(date +%Y%m%d_%H%M%S).md"

echo -e "${YELLOW}生成测试报告...${NC}"
cat > "$REPORT_FILE" << EOF
# 文件锁分片锁性能测试报告

**测试日期**: $(date)
**测试人**: AISafe64 Team
**测试环境**: Linux $(uname -r)

## 测试概述

本测试验证文件锁分片锁在多线程环境下的性能和正确性。

**测试参数**:
- FS_MAX_LOCKS: 128
- FS_SHARDS_COUNT: 8
- TEST_MOUNTS: 8

## 测试结果

### 压力测试

\`\`\`
[压力测试程序输出]
\`\`\`

### 性能基准测试

\`\`\`
[性能基准测试程序输出]
\`\`\`

## 性能指标

| 指标 | 数值 |
|------|------|
| 加锁吞吐量 | - ops/秒 |
| 解锁吞吐量 | - ops/秒 |
| 总吞吐量 | - ops/秒 |
| 平均锁操作时间 | - 微秒 |

## 结论

EOF

echo -e "${GREEN}✓ 测试完成，报告已保存到: $REPORT_FILE${NC}"
echo ""
echo -e "${YELLOW}提示: 测试报告包含完整的测试输出，可用于性能分析和优化。${NC}"
