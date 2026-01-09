#!/bin/bash
#
# 批量修改Git提交消息为中文
# 警告：此脚本会重写Git历史，改变所有提交哈希
#

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

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

# 提交消息翻译映射
translate_msg() {
    local msg="$1"

    # 使用sed进行替换
    msg=$(echo "$msg" | sed 's/refactor: 优化编码规范文档和添加Git工具/refactor: 优化编码规范文档和添加Git工具/')
    msg=$(echo "$msg" | sed 's/refactor(sync): enhance semaphore type safety and concurrency protection/refactor(sync): 增强信号量类型安全和并发保护/')
    msg=$(echo "$msg" | sed 's/refactor(error): migrate to POSIX standard error codes/refactor(error): 迁移到POSIX标准错误码/')
    msg=$(echo "$msg" | sed 's/feat(lib): add POSIX strerror() and perror() support/feat(lib): 添加POSIX strerror()和perror()支持/')
    msg=$(echo "$msg" | sed 's/feat(driver): implement device driver framework with character device support/feat(driver): 实现设备驱动框架和字符设备支持/')
    msg=$(echo "$msg" | sed 's/docs: update implementation status - Shell module completed/docs: 更新实现状态 - Shell模块已完成/')
    msg=$(echo "$msg" | sed 's/feat(types): adopt POSIX standard error codes/feat(types): 采用POSIX标准错误码/')
    msg=$(echo "$msg" | sed 's/feat(fs): implement VFS, initramfs and procfs filesystems/feat(fs): 实现VFS、initramfs和procfs文件系统/')
    msg=$(echo "$msg" | sed 's/refactor(sync): fix critical race conditions and atomicity issues/refactor(sync): 修复关键竞态条件和原子性问题/')
    msg=$(echo "$msg" | sed 's/feat(irq): add proper interrupt context detection with in_interrupt()/feat(irq): 添加使用in_interrupt()进行中断上下文检测/')
    msg=$(echo "$msg" | sed 's/fix(sync): remove unused SEVL from semaphore_post/fix(sync): 从semaphore_post中移除未使用的SEVL/')
    msg=$(echo "$msg" | sed 's/feat(scheduler): implement task monitoring and statistics/feat(scheduler): 实现任务监控和统计功能/')
    msg=$(echo "$msg" | sed 's/refactor(sync): optimize semaphore for interrupt-safe usage pattern/refactor(sync): 优化信号量以支持中断安全的使用模式/')
    msg=$(echo "$msg" | sed 's/test: add scheduler unit tests to build system/test: 添加调度器单元测试到构建系统/')
    msg=$(echo "$msg" | sed 's/fix(sync): remove WFE to ensure hard real-time determinism/fix(sync): 移除WFE以确保硬实时确定性/')
    msg=$(echo "$msg" | sed 's/docs: add infinite loop coding standard requirement/docs: 添加无限循环编码标准要求/')
    msg=$(echo "$msg" | sed 's/feat(sync): enhance semaphore with timeout support and scheduler integration/feat(sync): 增强信号量支持超时和调度器集成/')
    msg=$(echo "$msg" | sed 's/style: apply clang-format to all C\/C++ source files/style: 对所有C/C++源文件应用clang-format/')
    msg=$(echo "$msg" | sed 's/chore: reorganize project structure and update gitignore/chore: 重组项目结构并更新gitignore/')
    msg=$(echo "$msg" | sed 's/style(clang-format): simplify brace style config and add LLVM path setup script/style(clang-format): 简化大括号样式配置并添加LLVM路径设置脚本/')

    # 处理Conventional Commits的类型
    msg=$(echo "$msg" | sed 's/^feat:/功能:/')
    msg=$(echo "$msg" | sed 's/^fix:/修复:/')
    msg=$(echo "$msg" | sed 's/^refactor:/重构:/')
    msg=$(echo "$msg" | sed 's/^docs:/文档:/')
    msg=$(echo "$msg" | sed 's/^style:/风格:/')
    msg=$(echo "$msg" | sed 's/^test:/测试:/')
    msg=$(echo "$msg" | sed 's/^chore:/杂项:/')

    echo "$msg"
}

print_info "开始批量翻译提交消息..."
echo ""

# 备份当前分支
CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
BACKUP_BRANCH="backup-before-translate-$(date +%Y%m%d-%H%M%S)"

print_info "创建备份分支: $BACKUP_BRANCH"
git branch "$BACKUP_BRANCH"

# 创建临时脚本用于filter-branch
cat > /tmp/translate_msg.sh << 'EOFSCRIPT'
#!/bin/bash
translate_msg() {
    local msg="$1"

    # 类型翻译
    msg=$(echo "$msg" | sed 's/^refactor:/重构:/')
    msg=$(echo "$msg" | sed 's/^feat:/功能:/')
    msg=$(echo "$msg" | sed 's/^fix:/修复:/')
    msg=$(echo "$msg" | sed 's/^docs:/文档:/')
    msg=$(echo "$msg" | sed 's/^style:/风格:/')
    msg=$(echo "$msg" | sed 's/^test:/测试:/')
    msg=$(echo "$msg" | sed 's/^chore:/杂项:/')

    # 具体提交消息翻译
    msg=$(echo "$msg" | sed 's/enhance semaphore type safety and concurrency protection/增强信号量类型安全和并发保护/')
    msg=$(echo "$msg" | sed 's/migrate to POSIX standard error codes/迁移到POSIX标准错误码/')
    msg=$(echo "$msg" | sed 's/add POSIX strerror() and perror() support/添加POSIX strerror()和perror()支持/')
    msg=$(echo "$msg" | sed 's/implement device driver framework with character device support/实现设备驱动框架和字符设备支持/')
    msg=$(echo "$msg" | sed 's/update implementation status - Shell module completed/更新实现状态 - Shell模块已完成/')
    msg=$(echo "$msg" | sed 's/adopt POSIX standard error codes/采用POSIX标准错误码/')
    msg=$(echo "$msg" | sed 's/implement VFS, initramfs and procfs filesystems/实现VFS、initramfs和procfs文件系统/')
    msg=$(echo "$msg" | sed 's/fix critical race conditions and atomicity issues/修复关键竞态条件和原子性问题/')
    msg=$(echo "$msg" | sed 's/add proper interrupt context detection with in_interrupt()/添加使用in_interrupt()进行中断上下文检测/')
    msg=$(echo "$msg" | sed 's/remove unused SEVL from semaphore_post/从semaphore_post中移除未使用的SEVL/')
    msg=$(echo "$msg" | sed 's/implement task monitoring and statistics/实现任务监控和统计功能/')
    msg=$(echo "$msg" | sed 's/optimize semaphore for interrupt-safe usage pattern/优化信号量以支持中断安全的使用模式/')
    msg=$(echo "$msg" | sed 's/add scheduler unit tests to build system/添加调度器单元测试到构建系统/')
    msg=$(echo "$msg" | sed 's/remove WFE to ensure hard real-time determinism/移除WFE以确保硬实时确定性/')
    msg=$(echo "$msg" | sed 's/add infinite loop coding standard requirement/添加无限循环编码标准要求/')
    msg=$(echo "$msg" | sed 's/enhance semaphore with timeout support and scheduler integration/增强信号量支持超时和调度器集成/')
    msg=$(echo "$msg" | sed 's/apply clang-format to all C\/C++ source files/对所有C\/C++源文件应用clang-format/')
    msg=$(echo "$msg" | sed 's/reorganize project structure and update gitignore/重组项目结构并更新gitignore/')
    msg=$(echo "$msg" | sed 's/simplify brace style config and add LLVM path setup script/简化大括号样式配置并添加LLVM路径设置脚本/')
    msg=$(echo "$msg" | sed 's/optimize encoding specification documentation and add Git tools/优化编码规范文档和添加Git工具/')

    echo "$msg"
}

cat
translate_msg
EOFSCRIPT

chmod +x /tmp/translate_msg.sh

print_info "开始重写Git历史..."
print_warning "这可能需要一些时间..."

# 使用filter-branch重写所有提交消息
export FILTER_BRANCH_SQUELCH_WARNING=1
git filter-branch -f --msg-filter '
    msg=$(cat)

    # 类型翻译
    msg=$(echo "$msg" | sed "s/^refactor:/重构:/")
    msg=$(echo "$msg" | sed "s/^feat:/功能:/")
    msg=$(echo "$msg" | sed "s/^fix:/修复:/")
    msg=$(echo "$msg" | sed "s/^docs:/文档:/")
    msg=$(echo "$msg" | sed "s/^style:/风格:/")
    msg=$(echo "$msg" | sed "s/^test:/测试:/")
    msg=$(echo "$msg" | sed "s/^chore:/杂项:/")

    # 具体消息翻译
    msg=$(echo "$msg" | sed "s/enhance semaphore type safety and concurrency protection/增强信号量类型安全和并发保护/")
    msg=$(echo "$msg" | sed "s/migrate to POSIX standard error codes/迁移到POSIX标准错误码/")
    msg=$(echo "$msg" | sed "s/add POSIX strerror() and perror() support/添加POSIX strerror()和perror()支持/")
    msg=$(echo "$msg" | sed "s/implement device driver framework with character device support/实现设备驱动框架和字符设备支持/")
    msg=$(echo "$msg" | sed "s/update implementation status - Shell module completed/更新实现状态 - Shell模块已完成/")
    msg=$(echo "$msg" | sed "s/adopt POSIX standard error codes/采用POSIX标准错误码/")
    msg=$(echo "$msg" | sed "s/implement VFS, initramfs and procfs filesystems/实现VFS、initramfs和procfs文件系统/")
    msg=$(echo "$msg" | sed "s/fix critical race conditions and atomicity issues/修复关键竞态条件和原子性问题/")
    msg=$(echo "$msg" | sed "s/add proper interrupt context detection with in_interrupt()/添加使用in_interrupt()进行中断上下文检测/")
    msg=$(echo "$msg" | sed "s/remove unused SEVL from semaphore_post/从semaphore_post中移除未使用的SEVL/")
    msg=$(echo "$msg" | sed "s/implement task monitoring and statistics/实现任务监控和统计功能/")
    msg=$(echo "$msg" | sed "s/optimize semaphore for interrupt-safe usage pattern/优化信号量以支持中断安全的使用模式/")
    msg=$(echo "$msg" | sed "s/add scheduler unit tests to build system/添加调度器单元测试到构建系统/")
    msg=$(echo "$msg" | sed "s/remove WFE to ensure hard real-time determinism/移除WFE以确保硬实时确定性/")
    msg=$(echo "$msg" | sed "s/add infinite loop coding standard requirement/添加无限循环编码标准要求/")
    msg=$(echo "$msg" | sed "s/enhance semaphore with timeout support and scheduler integration/增强信号量支持超时和调度器集成/")
    msg=$(echo "$msg" | sed "s/apply clang-format to all C\/C++ source files/对所有C\/C++源文件应用clang-format/")
    msg=$(echo "$msg" | sed "s/reorganize project structure and update gitignore/重组项目结构并更新gitignore/")
    msg=$(echo "$msg" | sed "s/simplify brace style config and add LLVM path setup script/简化大括号样式配置并添加LLVM路径设置脚本/")

    echo "$msg"
' --tag-name-filter cat -- --all

if [ $? -eq 0 ]; then
    print_success "Git历史重写完成！"
    echo ""
    echo "=== 翻译后的提交历史 ==="
    git log --oneline -20
    echo ""

    print_info "备份分支: $BACKUP_BRANCH"
    print_warning "如果需要恢复，运行: git reset --hard $BACKUP_BRANCH"
    echo ""

    print_info "清理临时文件..."
    rm -rf .git/refs/original/

    print_success "完成！现在可以使用 'git push --force' 推送到远程仓库"
    print_warning "注意：需要使用 --force 或 --force-with-lease 强制推送"
else
    print_error "Git历史重写失败"
    print_info "恢复到备份分支: git reset --hard $BACKUP_BRANCH"
    exit 1
fi
