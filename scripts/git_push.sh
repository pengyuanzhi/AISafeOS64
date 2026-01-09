#!/bin/bash
#
# AISafe64 Git 提交和推送脚本
# 用途：便捷地提交代码到远程仓库
# 使用：./scripts/git_push.sh "提交消息" [分支名]
#

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 打印带颜色的消息
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查参数
if [ $# -eq 0 ]; then
    print_error "缺少提交消息参数"
    echo ""
    echo "用法: $0 \"提交消息\" [分支名]"
    echo ""
    echo "示例:"
    echo "  $0 \"feat: 添加新的调度器算法\""
    echo "  $0 \"fix: 修复内存泄漏问题\" develop"
    echo ""
    exit 1
fi

COMMIT_MESSAGE="$1"
BRANCH_NAME="${2:-master}"

print_info "开始提交流程..."
echo ""

# 1. 检查git仓库
print_info "检查Git仓库状态..."
if ! git rev-parse --git-dir > /dev/null 2>&1; then
    print_error "当前目录不是Git仓库"
    exit 1
fi
print_success "Git仓库检查通过"

# 2. 检查是否有变更
print_info "检查文件变更..."
if git diff --quiet && git diff --cached --quiet; then
    print_warning "没有检测到文件变更"
    echo ""
    git status
    exit 0
fi

# 显示变更文件
echo ""
echo "=== 变更文件列表 ==="
git status -s
echo ""

# 3. 添加所有变更文件
print_info "添加所有变更文件..."
git add -A
print_success "文件添加完成"

# 4. 显示将要提交的内容
echo ""
echo "=== 提交预览 ==="
git diff --cached --stat
echo ""

# 5. 创建提交
print_info "创建提交..."
git commit -m "$COMMIT_MESSAGE

🤖 Generated with [Claude Code](https://claude.com/claude-code)

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>"

if [ $? -eq 0 ]; then
    print_success "提交创建成功"
else
    print_error "提交创建失败"
    exit 1
fi

# 6. 推送到远程仓库
print_info "推送到远程仓库 (origin/$BRANCH_NAME)..."
git push origin "$BRANCH_NAME"

if [ $? -eq 0 ]; then
    print_success "推送成功！"
    echo ""
    echo "=== 提交信息 ==="
    git log -1 --pretty=format:"%h - %s (%an, %ar)" HEAD
    echo ""
    print_success "代码已成功提交到远程仓库 origin/$BRANCH_NAME"
else
    print_error "推送失败"
    print_info "请检查网络连接或远程仓库配置"
    exit 1
fi
