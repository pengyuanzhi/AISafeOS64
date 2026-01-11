#!/bin/bash
#
# 安装 Conventional Commits Git Hooks
#
# 使用方法:
#   ./scripts/install_hooks.sh
#

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

# 打印信息
print_info() {
    echo -e "${CYAN}$1${RESET}"
}

print_success() {
    echo -e "${GREEN}✓ $1${RESET}"
}

print_error() {
    echo -e "${RED}✗ $1${RESET}"
}

print_warning() {
    echo -e "${YELLOW}⚠ $1${RESET}"
}

print_header() {
    echo -e "\n${BOLD}${BLUE}========================================${RESET}"
    echo -e "${BOLD}${BLUE}$1${RESET}"
    echo -e "${BOLD}${BLUE}========================================${RESET}\n"
}

# 获取脚本目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
HOOKS_DIR="$PROJECT_ROOT/.git/hooks"
PYTHON_SCRIPT="$SCRIPT_DIR/conventional_commit.py"

# 检查 Python 是否可用
check_python() {
    if ! command -v python3 &> /dev/null; then
        print_error "未找到 python3"
        return 1
    fi
    print_success "找到 python3: $(python3 --version)"
    return 0
}

# 安装 commit-msg hook
install_commit_msg_hook() {
    print_info "安装 commit-msg hook..."

    cat > "$HOOKS_DIR/commit-msg" << 'EOF'
#!/bin/bash
#
# Conventional Commits 提交消息验证 Hook
#

set -e

# 获取脚本目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/scripts"
PYTHON_SCRIPT="$SCRIPT_DIR/conventional_commit.py"

# 检查 Python 脚本是否存在
if [ ! -f "$PYTHON_SCRIPT" ]; then
    echo "警告: 找不到 $PYTHON_SCRIPT"
    echo "跳过提交消息验证"
    exit 0
fi

# 运行验证
python3 "$PYTHON_SCRIPT" validate "$1"
EOF

    chmod +x "$HOOKS_DIR/commit-msg"
    print_success "commit-msg hook 已安装"
}

# 安装 prepare-commit-msg hook (可选)
install_prepare_commit_msg_hook() {
    print_info "安装 prepare-commit-msg hook..."

    cat > "$HOOKS_DIR/prepare-commit-msg" << 'EOF'
#!/bin/bash
#
# Conventional Commits 提交消息辅助 Hook
#

# 只在交互式提交时运行
if [ -z "$2" ] || [ "$2" = "message" ]; then
    exit 0
fi

# 可以在这里添加自动化的提交消息模板
# 例如引用 Issue ID 等
EOF

    chmod +x "$HOOKS_DIR/prepare-commit-msg"
    print_success "prepare-commit-msg hook 已安装"
}

# 主函数
main() {
    print_header "Conventional Commits Git Hooks 安装"

    # 检查是否在 Git 仓库中
    if [ ! -d ".git" ]; then
        print_error "当前目录不是 Git 仓库根目录"
        print_info "请在项目根目录运行此脚本"
        exit 1
    fi

    # 检查 Python
    if ! check_python; then
        print_error "需要 Python 3 才能运行提交消息验证"
        exit 1
    fi

    # 检查 Python 脚本是否存在
    if [ ! -f "$PYTHON_SCRIPT" ]; then
        print_error "找不到 Python 验证脚本: $PYTHON_SCRIPT"
        exit 1
    fi

    # 创建 hooks 目录（如果不存在）
    mkdir -p "$HOOKS_DIR"

    # 安装 hooks
    install_commit_msg_hook
    install_prepare_commit_msg_hook

    print_header "安装完成"
    print_success "Git Hooks 已成功安装"
    print_info ""
    print_info "使用方法:"
    print_info "  1. 交互式生成提交消息:"
    print_info "     ${CYAN}python3 scripts/conventional_commit.py generate${RESET}"
    print_info ""
    print_info "  2. 正常提交（自动验证）:"
    print_info "     ${CYAN}git commit${RESET}"
    print_info ""
    print_info "  3. 查看 Conventional Commits 规范:"
    print_info "     ${CYAN}cat .commitlintrc.yml${RESET}"
    print_info ""
}

# 运行主函数
main
