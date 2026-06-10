#!/bin/bash
#
# 安装 Git Hooks（支持代码格式化和 Conventional Commits）
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

# 检查 clang-format 是否可用
check_clang_format() {
    if ! command -v clang-format &> /dev/null; then
        print_warning "未找到 clang-format"
        print_info "代码格式化功能将被跳过"
        return 1
    fi
    print_success "找到 clang-format: $(clang-format --version)"
    return 0
}

# 检查 Python 是否可用
check_python() {
    if ! command -v python3 &> /dev/null; then
        print_error "未找到 python3"
        return 1
    fi
    print_success "找到 python3: $(python3 --version)"
    return 0
}

# 安装 pre-commit hook（代码格式化）
install_pre_commit_hook() {
    print_info "安装 pre-commit hook（代码格式化）..."

    cat > "$HOOKS_DIR/pre-commit" << 'EOF'
#!/bin/bash
#
# Git pre-commit hook
# 功能：
#   1. 运行 clang-format 格式化代码
#   2. 如果格式化改变了文件，则拒绝提交并提示用户
#

set -e

# 获取项目根目录
PROJECT_ROOT="$(git rev-parse --show-toplevel)"
CLANG_FORMAT="${PROJECT_ROOT}/.clang-format"

# 检查 clang-format 是否存在
if ! command -v clang-format &> /dev/null; then
    echo "警告: 找不到 clang-format，跳过代码格式化检查"
    exit 0
fi

# 检查 .clang-format 配置文件是否存在
if [ ! -f "$CLANG_FORMAT" ]; then
    echo "警告: 找不到 .clang-format 配置文件，跳过代码格式化检查"
    exit 0
fi

echo "正在检查代码格式..."

# 获取所有将要提交的 C/C++ 文件
FILES=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(c|h|cpp|hpp|cc|cxx)$' || true)

if [ -z "$FILES" ]; then
    echo "没有需要格式化的 C/C++ 文件"
    exit 0
fi

# 创建临时目录用于格式化对比
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

# 格式化所有文件并检查是否有变更
FORMATTED=0
for FILE in $FILES; do
    if [ ! -f "$FILE" ]; then
        continue
    fi

    # 保存原始文件的格式
    cp "$FILE" "$TEMP_DIR/$(basename "$FILE").original"

    # 运行 clang-format
    clang-format -i "$FILE" 2>/dev/null || true

    # 检查文件是否有变更
    if ! diff -q "$FILE" "$TEMP_DIR/$(basename "$FILE").original" > /dev/null 2>&1; then
        echo "  格式化: $FILE"
        FORMATTED=1
    else
        rm "$TEMP_DIR/$(basename "$FILE").original"
    fi
done

# 如果有文件被格式化，则拒绝提交
if [ $FORMATTED -eq 1 ]; then
    echo ""
    echo "警告: 代码格式不正确，已自动格式化"
    echo ""
    echo "以下文件已被格式化："
    git diff --name-only
    echo ""
    echo "请检查格式化后的更改，然后重新提交："
    echo "  git add ."
    echo "  git commit"
    echo ""
    echo "如需跳过格式化检查，使用："
    echo "  git commit --no-verify"
    echo ""
    exit 1
fi

echo "代码格式检查通过"
exit 0
EOF

    chmod +x "$HOOKS_DIR/pre-commit"
    print_success "pre-commit hook 已安装"
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
    print_header "Git Hooks 安装工具"

    # 检查是否在 Git 仓库中
    if [ ! -d ".git" ]; then
        print_error "当前目录不是 Git 仓库根目录"
        print_info "请在项目根目录运行此脚本"
        exit 1
    fi

    # 创建 hooks 目录（如果不存在）
    mkdir -p "$HOOKS_DIR"

    # 检查工具
    HAS_CLANG_FORMAT=0
    HAS_PYTHON=0

    if check_clang_format; then
        HAS_CLANG_FORMAT=1
    fi

    if check_python; then
        HAS_PYTHON=1
    fi

    if [ $HAS_PYTHON -eq 0 ]; then
        print_error "需要 Python 3 才能运行提交消息验证"
        exit 1
    fi

    # 检查 Python 脚本是否存在
    if [ ! -f "$PYTHON_SCRIPT" ]; then
        print_error "找不到 Python 验证脚本: $PYTHON_SCRIPT"
        exit 1
    fi

    # 安装 hooks
    if [ $HAS_CLANG_FORMAT -eq 1 ]; then
        install_pre_commit_hook
    fi

    install_commit_msg_hook
    install_prepare_commit_msg_hook

    print_header "安装完成"
    print_success "Git Hooks 已成功安装"
    print_info ""
    print_info "已安装的 Hooks:"
    print_info "  1. ${CYAN}pre-commit${RESET} - 代码格式化检查（clang-format）"
    print_info "  2. ${CYAN}commit-msg${RESET} - 提交消息格式验证"
    print_info "  3. ${CYAN}prepare-commit-msg${RESET} - 提交消息辅助"
    print_info ""
    print_info "使用方法:"
    print_info "  1. 智能提交（推荐）:"
    print_info "     ${CYAN}./scripts/smart_commit.sh${RESET}"
    print_info ""
    print_info "  2. 正常提交（自动格式化）:"
    print_info "     ${CYAN}git commit${RESET}"
    print_info ""
    print_info "  3. 格式化所有代码:"
    print_info "     ${CYAN}find . -name '*.c' -o -name '*.h' | xargs clang-format -i${RESET}"
    print_info ""
    print_info "  4. 跳过 hooks（不推荐）:"
    print_info "     ${CYAN}git commit --no-verify${RESET}"
    print_info ""
}

# 运行主函数
main
