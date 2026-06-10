#!/bin/bash
#
# 全自动 Git 提交脚本 v2.0
# 自动暂存文件并遵循 Conventional Commits 规范
#
# 使用方法:
#   ./scripts/smart_commit.sh           # 默认模式：暂存所有更改
#   ./scripts/smart_commit.sh -i       # 交互模式：选择文件
#   ./scripts/smart_commit.sh -p       # 提交后自动推送到远程
#   ./scripts/smart_commit.sh -i -p    # 交互模式 + 推送
#   ./scripts/smart_commit.sh --help   # 显示帮助信息
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

# 获取脚本目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON_SCRIPT="$SCRIPT_DIR/conventional_commit.py"

# 默认选项
INTERACTIVE_MODE=false
AUTO_PUSH=false
SHOW_HELP=false

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

# 显示帮助信息
show_help() {
    cat << EOF
${BOLD}智能 Git 提交助手 v2.0${RESET}

${BOLD}用法:${RESET}
  $0 [选项]

${BOLD}选项:${RESET}
  -i, --interactive     交互模式：手动选择要暂存的文件
  -p, --push            提交后自动推送到远程仓库
  -h, --help            显示此帮助信息

${BOLD}示例:${RESET}
  $0                    默认模式：暂存所有更改并提交
  $0 -i                交互模式：选择要暂存的文件
  $0 -p                暂存所有更改、提交并推送
  $0 -i -p             交互模式选择文件、提交并推送

${BOLD}说明:${RESET}
  - 默认模式下会自动暂存所有修改的文件
  - 交互模式允许逐个选择要暂存的文件
  - 使用 -p 选项可在提交后自动推送到远程

EOF
}

# 解析命令行参数
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            -i|--interactive)
                INTERACTIVE_MODE=true
                shift
                ;;
            -p|--push)
                AUTO_PUSH=true
                shift
                ;;
            -h|--help)
                SHOW_HELP=true
                shift
                ;;
            *)
                print_error "未知选项: $1"
                echo "使用 -h 或 --help 查看帮助信息"
                exit 1
                ;;
        esac
    done
}

# 检查是否有暂存的更改
check_staged_changes() {
    if ! git diff --cached --quiet 2>/dev/null; then
        return 0
    else
        return 1
    fi
}

# 检查是否有未暂存的更改
check_unstaged_changes() {
    if ! git diff --quiet 2>/dev/null || ! git diff --cached --quiet 2>/dev/null; then
        return 0
    else
        return 1
    fi
}

# 检查是否有未跟踪的文件
check_untracked_files() {
    if [ -n "$(git ls-files --others --exclude-standard)" ]; then
        return 0
    else
        return 1
    fi
}

# 获取所有更改的文件列表
get_changed_files() {
    # 未暂存的修改文件
    local modified=$(git diff --name-only --diff-filter=M 2>/dev/null)

    # 未暂存的删除文件
    local deleted=$(git diff --name-only --diff-filter=D 2>/dev/null)

    # 未跟踪的新文件
    local untracked=$(git ls-files --others --exclude-standard 2>/dev/null)

    # 合并所有文件
    local all_files=""
    if [ -n "$modified" ]; then
        all_files="${all_files}${modified}\n"
    fi
    if [ -n "$deleted" ]; then
        all_files="${all_files}${deleted}\n"
    fi
    if [ -n "$untracked" ]; then
        all_files="${all_files}${untracked}\n"
    fi

    echo -e "$all_files" | grep -v '^$' | sort | uniq
}

# 显示更改的文件
show_file_status() {
    print_info "当前仓库状态:\n"

    # 显示分支信息
    local branch=$(git branch --show-current 2>/dev/null || echo "HEAD")
    print_info "分支: ${BOLD}${CYAN}${branch}${RESET}\n"

    # 显示未暂存的修改
    local modified=$(git diff --name-only --diff-filter=M 2>/dev/null)
    if [ -n "$modified" ]; then
        print_info "${YELLOW}修改的文件 (未暂存):${RESET}"
        echo "$modified" | while read -r file; do
            echo "  ${YELLOW}M${RESET} $file"
        done
        echo ""
    fi

    # 显示未暂存的删除
    local deleted=$(git diff --name-only --diff-filter=D 2>/dev/null)
    if [ -n "$deleted" ]; then
        print_info "${RED}删除的文件 (未暂存):${RESET}"
        echo "$deleted" | while read -r file; do
            echo "  ${RED}D${RESET} $file"
        done
        echo ""
    fi

    # 显示未跟踪的新文件
    local untracked=$(git ls-files --others --exclude-standard 2>/dev/null)
    if [ -n "$untracked" ]; then
        print_info "${GREEN}未跟踪的新文件:${RESET}"
        echo "$untracked" | while read -r file; do
            echo "  ${GREEN}??${RESET} $file"
        done
        echo ""
    fi

    # 显示已暂存的文件
    if check_staged_changes; then
        print_info "${CYAN}已暂存的文件:${RESET}"
        git diff --cached --name-status | while read -r status file; do
            echo "  ${GREEN}${status}${RESET} $file"
        done
        echo ""
    fi
}

# 显示详细的文件差异
show_file_diff() {
    local file=$1

    # 检查文件是否存在
    if [ ! -e "$file" ] && ! git ls-files --error-unmatch "$file" >/dev/null 2>&1; then
        print_error "文件不存在: $file"
        return 1
    fi

    print_info "\n文件: ${BOLD}${CYAN}${file}${RESET}"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    # 显示差异
    if git diff --quiet "$file" 2>/dev/null; then
        # 如果在工作区没有差异，检查暂存区
        if git diff --cached --quiet "$file" 2>/dev/null; then
            print_warning "  (新文件，无历史记录)"
        else
            git diff --cached "$file" 2>/dev/null || true
        fi
    else
        git diff "$file" 2>/dev/null || true
    fi

    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
}

# 交互式选择文件
interactive_select_files() {
    local files=($(get_changed_files))

    if [ ${#files[@]} -eq 0 ]; then
        return 1
    fi

    print_header "选择要暂存的文件"

    local index=1
    declare -A file_map
    declare -A selected_map

    for file in "${files[@]}"; do
        # 确定文件状态
        local status="??"
        if git diff --quiet "$file" 2>/dev/null; then
            if ! git diff --cached --quiet "$file" 2>/dev/null; then
                status="M"
            fi
        else
            status="M"
        fi

        if ! git ls-files --error-unmatch "$file" >/dev/null 2>&1; then
            status="??"
        fi

        file_map[$index]=$file
        selected_map[$index]=0

        # 根据状态选择颜色
        local color="${RESET}"
        if [ "$status" == "M" ]; then
            color="${YELLOW}"
        elif [ "$status" == "??" ]; then
            color="${GREEN}"
        fi

        printf "  ${CYAN}[%2d]${RESET} ${color}%s${RESET} ${color}%s${RESET}\n" "$index" "$status" "$file"
        ((index++))
    done

    echo ""
    print_info "操作说明:"
    print_info "  输入文件编号 (如: 1 3 5) - 选择/取消选择文件"
    print_info "  a - 全选"
    print_info "  n - 取消全选"
    print_info "  d <编号> - 查看文件差异 (如: d 1)"
    print_info "  Enter - 确认并继续"
    print_info "  q - 取消"
    echo ""

    while true; do
        echo -n -e "${CYAN}选择文件> ${RESET}"
        read input

        case "$input" in
            q|Q)
                print_info "取消操作"
                return 1
                ;;
            a|A)
                for i in "${!file_map[@]}"; do
                    selected_map[$i]=1
                done
                # 更新显示
                for i in "${!file_map[@]}"; do
                    local file="${file_map[$i]}"
                    if [ ${selected_map[$i]} -eq 1 ]; then
                        printf "  ${GREEN}[%2d]${RESET} ${GREEN}✓${RESET} %s\n" "$i" "$file"
                    else
                        printf "  ${CYAN}[%2d]${RESET}  %s\n" "$i" "$file"
                    fi
                done
                ;;
            n|N)
                for i in "${!file_map[@]}"; do
                    selected_map[$i]=0
                done
                for i in "${!file_map[@]}"; do
                    local file="${file_map[$i]}"
                    printf "  ${CYAN}[%2d]${RESET}  %s\n" "$i" "$file"
                done
                ;;
            d\ *|D\ *)
                local num=$(echo "$input" | cut -d' ' -f2)
                if [ -n "${file_map[$num]}" ]; then
                    show_file_diff "${file_map[$num]}"
                else
                    print_error "无效的编号: $num"
                fi
                ;;
            "")
                # 检查是否至少选择了一个文件
                local has_selection=0
                for i in "${!selected_map[@]}"; do
                    if [ ${selected_map[$i]} -eq 1 ]; then
                        has_selection=1
                        break
                    fi
                done

                if [ $has_selection -eq 0 ]; then
                    print_warning "至少需要选择一个文件"
                    continue
                fi

                # 暂存选中的文件
                local staged_count=0
                for i in "${!selected_map[@]}"; do
                    if [ ${selected_map[$i]} -eq 1 ]; then
                        local file="${file_map[$i]}"
                        print_info "暂存: $file"
                        git add "$file" 2>/dev/null || true
                        ((staged_count++))
                    fi
                done

                if [ $staged_count -gt 0 ]; then
                    print_success "已暂存 $staged_count 个文件\n"
                    return 0
                else
                    print_error "暂存失败"
                    return 1
                fi
                ;;
            *)
                # 处理数字输入
                local valid=0
                for num in $input; do
                    if [[ "$num" =~ ^[0-9]+$ ]] && [ -n "${file_map[$num]}" ]; then
                        # 切换选择状态
                        if [ ${selected_map[$num]} -eq 0 ]; then
                            selected_map[$num]=1
                        else
                            selected_map[$num]=0
                        fi
                        valid=1
                    fi
                done

                if [ $valid -eq 1 ]; then
                    # 更新显示
                    for i in "${!file_map[@]}"; do
                        local file="${file_map[$i]}"
                        if [ ${selected_map[$i]} -eq 1 ]; then
                            printf "  ${GREEN}[%2d]${RESET} ${GREEN}✓${RESET} %s\n" "$i" "$file"
                        else
                            printf "  ${CYAN}[%2d]${RESET}  %s\n" "$i" "$file"
                        fi
                    done
                else
                    print_warning "无效的输入: $input"
                fi
                ;;
        esac
    done
}

# 自动暂存所有更改
stage_all_changes() {
    print_info "暂存所有更改..."

    # 添加所有修改和删除的文件
    git add -u 2>/dev/null || true

    # 添加所有未跟踪的文件
    git add -A 2>/dev/null || true

    if check_staged_changes; then
        local count=$(git diff --cached --name-only | wc -l)
        print_success "已暂存 $count 个文件\n"
        return 0
    else
        print_error "没有文件可暂存"
        return 1
    fi
}

# 显示更改的文件
show_staged_changes() {
    print_info "暂存的更改:"
    git diff --cached --stat
}

# 推送到远程仓库
push_to_remote() {
    print_header "推送到远程仓库"

    # 获取当前分支
    local branch=$(git branch --show-current 2>/dev/null || echo "master")
    local remote=$(git config branch.${branch}.remote || echo "origin")

    print_info "推送分支: ${BOLD}${CYAN}${branch}${RESET}"
    print_info "远程仓库: ${BOLD}${CYAN}${remote}${RESET}\n"

    if git push "${remote}" "${branch}"; then
        print_success "推送成功!"
        return 0
    else
        print_error "推送失败"
        print_info "\n请检查网络连接或远程仓库配置"
        return 1
    fi
}

# 主函数
smart_commit() {
    parse_args "$@"

    # 显示帮助
    if [ "$SHOW_HELP" = true ]; then
        show_help
        exit 0
    fi

    print_header "智能 Git 提交助手 v2.0"

    # 检查是否在 Git 仓库中
    if ! git rev-parse --git-dir > /dev/null 2>&1; then
        print_error "当前目录不是 Git 仓库"
        return 1
    fi

    # 检查是否已有暂存的更改
    local has_staged=0
    if check_staged_changes; then
        has_staged=1
    fi

    # 检查是否有未暂存的更改或未跟踪的文件
    local has_unstaged=0
    if check_unstaged_changes || check_untracked_files; then
        has_unstaged=1
    fi

    # 显示当前状态
    show_file_status

    # 如果没有更改
    if [ $has_staged -eq 0 ] && [ $has_unstaged -eq 0 ]; then
        print_warning "没有检测到任何更改"
        print_info "\n工作目录干净，无需提交"
        return 0
    fi

    # 暂存文件
    if [ $has_unstaged -eq 1 ]; then
        if [ "$INTERACTIVE_MODE" = true ]; then
            # 交互模式
            if ! interactive_select_files; then
                print_info "取消操作"
                return 0
            fi
        else
            # 默认模式：暂存所有
            print_info "暂存所有更改...\n"
            if ! stage_all_changes; then
                return 1
            fi
        fi
    fi

    # 显示将要提交的更改
    print_header "确认提交"
    show_staged_changes

    echo ""
    echo -n -e "${CYAN}是否继续提交? (Y/n): ${RESET}"
    read confirm

    if [[ $confirm == "n" || $confirm == "N" ]]; then
        print_info "取消提交"
        return 0
    fi

    # 检查 Python 脚本是否存在
    if [ ! -f "$PYTHON_SCRIPT" ]; then
        print_error "找不到 Python 脚本: $PYTHON_SCRIPT"
        print_info "将使用普通的 git commit"
        if git commit; then
            print_success "提交成功!"
            if [ "$AUTO_PUSH" = true ]; then
                push_to_remote
            fi
        fi
        return $?
    fi

    # 生成提交消息
    print_header "生成提交消息"

    if ! python3 "$PYTHON_SCRIPT" generate; then
        print_error "生成提交消息失败"
        return 1
    fi

    # 提交
    COMMIT_FILE=".git/COMMIT_EDITMSG"

    if [ ! -f "$COMMIT_FILE" ]; then
        print_error "找不到提交消息文件"
        return 1
    fi

    print_info "\n执行提交..."
    if git commit -F "$COMMIT_FILE"; then
        print_success "提交成功!"

        # 显示提交信息
        print_header "最新提交"
        git log -1 --pretty=format:"%C(auto)%h %s%n%n%b" --color=always

        # 清理临时文件
        rm -f "$COMMIT_FILE"

        # 推送到远程
        if [ "$AUTO_PUSH" = true ]; then
            push_to_remote
        fi

        return 0
    else
        print_error "提交失败"
        return 1
    fi
}

# 如果直接运行脚本
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    smart_commit "$@"
fi
