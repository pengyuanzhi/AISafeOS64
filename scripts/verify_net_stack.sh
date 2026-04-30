#!/bin/bash
# 网络协议栈集成测试验证脚本
# 功能：启动 QEMU 并自动验证网络协议栈功能

set -e

# 项目根目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "${SCRIPT_DIR}")"
BUILD_DIR="${PROJECT_ROOT}/build"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "=========================================="
echo "AISafeOS64 网络协议栈集成测试"
echo "=========================================="
echo ""

# 检查编译产物
if [ ! -f "${BUILD_DIR}/aisafe64.bin" ]; then
    echo -e "${RED}错误：找不到 aisafe64.bin${NC}"
    echo "请先运行：cd ${BUILD_DIR} && make aisafe64.elf"
    exit 1
fi

if [ ! -f "${BUILD_DIR}/services/net.elf.elf" ]; then
    echo -e "${RED}错误：找不到 net.elf.elf${NC}"
    echo "请先运行：cd ${BUILD_DIR} && make net.elf"
    exit 1
fi

if [ ! -f "${BUILD_DIR}/services/drv_virtio_net.elf.elf" ]; then
    echo -e "${RED}错误：找不到 drv_virtio_net.elf.elf${NC}"
    echo "请先运行：cd ${BUILD_DIR} && make drv_virtio_net.elf"
    exit 1
fi

echo -e "${GREEN}✓ 所有编译产物检查通过${NC}"
echo ""

# QEMU 参数
QEMU="qemu-system-aarch64"
MACHINE="-M virt"
CPU="-cpu cortex-a57"
SMP="-smp 4"
MEMORY="-m 1G"
KERNEL="-kernel ${BUILD_DIR}/aisafe64.bin"
DISK="-drive file=${BUILD_DIR}/disk.img,format=raw,if=none,id=hd0 -device virtio-blk-device,drive=hd0"
NET_DEVICE="-netdev user,id=net0,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:80 -device virtio-net-device,netdev=net0"
NOGRAPHIC="-nographic"
SERIAL="-serial mon:stdio"

echo "QEMU 参数："
echo "  CPU: cortex-a57"
echo "  SMP: 4 核"
echo "  Memory: 1GB"
echo "  Network: 用户模式网络（user networking）"
echo "  Port Forwarding:"
echo "    2222 -> 22 (SSH)"
echo "    8080 -> 80 (HTTP)"
echo ""

# 创建临时 QEMU 输出日志
QEMU_LOG=$(mktemp)

echo "=========================================="
echo "启动 QEMU..."
echo "=========================================="
echo ""

# 启动 QEMU（后台运行）
cd "${BUILD_DIR}"
timeout 60 ${QEMU} ${MACHINE} ${CPU} ${SMP} ${MEMORY} ${KERNEL} ${DISK} ${NET_DEVICE} ${NOGRAPHIC} ${SERIAL} 2>&1 | tee "${QEMU_LOG}" &
QEMU_PID=$!

# 等待 QEMU 启动
sleep 3

echo "=========================================="
echo "网络协议栈功能验证"
echo "=========================================="
echo ""

# 验证函数
verify_feature() {
    local feature_name="$1"
    local pattern="$2"
    
    if grep -q "${pattern}" "${QEMU_LOG}"; then
        echo -e "${GREEN}✓ ${feature_name}${NC}"
        return 0
    else
        echo -e "${RED}✗ ${feature_name}${NC}"
        return 1
    fi
}

# 验证网络接口发现
verify_feature "网络接口自动发现" "net_if_auto_get_count"
verify_feature "VirtIO Net 设备初始化" "virtio-net-device"
verify_feature "网络接口注册" "net_if_register"

# 验证网络协议栈初始化
verify_feature "网络协议栈初始化" "net_init"
verify_feature "TCP 协议栈" "TCP"
verify_feature "UDP 协议栈" "UDP"
verify_feature "ICMP 协议栈" "ICMP"

# 验证用户态服务
verify_feature "Net 服务启动" "Net service"
verify_feature "FS 服务启动" "FS service"
verify_feature "Proc 服务启动" "Proc service"
verify_feature "Mem 服务启动" "Mem service"
verify_feature "Path 服务启动" "Path service"

# 验证网络功能
verify_feature "Socket API" "socket"
verify_feature "连接功能" "connect"
verify_feature "监听功能" "listen"
verify_feature "发送功能" "send"
verify_feature "接收功能" "recv"

echo ""
echo "=========================================="
echo "测试总结"
echo "=========================================="

# 统计成功和失败的数量
TOTAL=$(grep -c "✓" <<< "$features")
PASSED=$(grep -c "✓" <<< "$features")
FAILED=$(grep -c "✗" <<< "$features")

echo "总测试项：${TOTAL}"
echo -e "通过：${GREEN}${PASSED}${NC}"
echo -e "失败：${RED}${FAILED}${NC}"
echo ""

# 保存 QEMU 日志
LOG_FILE="${PROJECT_ROOT}/net_stack_test_$(date +%Y%m%d_%H%M%S).log"
cp "${QEMU_LOG}" "${LOG_FILE}"
echo "QEMU 日志已保存到：${LOG_FILE}"

# 清理临时文件
rm -f "${QEMU_LOG}"

# 停止 QEMU
kill ${QEMU_PID} 2>/dev/null || true

echo ""
echo "=========================================="
echo "测试完成"
echo "=========================================="
