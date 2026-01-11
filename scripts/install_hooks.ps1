# Git Hooks 安装脚本 (PowerShell)
# 支持 clang-format 代码格式化和 Conventional Commits 验证
#
# 使用方法:
#   .\scripts\install_hooks.ps1
#

$ErrorActionPreference = "Stop"

# 获取脚本目录
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir
$HooksDir = "$ProjectRoot\.git\hooks"
$PythonScript = "$ScriptDir\conventional_commit.py"

# 颜色输出函数
function Write-Header {
    param([string]$Message)
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Blue -Bold
    Write-Host "$Message" -ForegroundColor Blue -Bold
    Write-Host "========================================" -ForegroundColor Blue -Bold
    Write-Host ""
}

function Write-Success {
    param([string]$Message)
    Write-Host "✓ $Message" -ForegroundColor Green
}

function Write-Error {
    param([string]$Message)
    Write-Host "✗ $Message" -ForegroundColor Red
}

function Write-Warning {
    param([string]$Message)
    Write-Host "⚠ $Message" -ForegroundColor Yellow
}

function Write-Info {
    param([string]$Message)
    Write-Host $Message -ForegroundColor Cyan
}

# 检查 clang-format
function Test-ClangFormat {
    Write-Info "检查 clang-format..."

    try {
        $cfCmd = Get-Command clang-format -ErrorAction SilentlyContinue
        if ($null -eq $cfCmd) {
            Write-Warning "未找到 clang-format"
            Write-Info "代码格式化功能将被跳过"
            return $false
        }

        $version = & clang-format --version 2>&1
        Write-Success "找到 clang-format: $version"
        return $true
    }
    catch {
        Write-Warning "检查 clang-format 失败: $_"
        return $false
    }
}

# 检查 Python
function Test-Python {
    Write-Info "检查 Python..."

    try {
        $pythonCmd = Get-Command python -ErrorAction SilentlyContinue
        if ($null -eq $pythonCmd) {
            $pythonCmd = Get-Command python3 -ErrorAction SilentlyContinue
        }

        if ($null -eq $pythonCmd) {
            Write-Error "未找到 Python"
            return $false
        }

        $version = & $pythonCmd --version 2>&1
        Write-Success "找到 Python: $version"
        return $true
    }
    catch {
        Write-Error "检查 Python 失败: $_"
        return $false
    }
}

# 安装 pre-commit hook
function Install-PreCommitHook {
    Write-Info "安装 pre-commit hook（代码格式化）..."

    $hookContent = @"
#!/bin/bash
#
# Git pre-commit hook
# 功能：
#   1. 运行 clang-format 格式化代码
#   2. 如果格式化改变了文件，则拒绝提交并提示用户
#

set -e

# 获取项目根目录
PROJECT_ROOT=`"$(git rev-parse --show-toplevel)``
CLANG_FORMAT=`"`$PROJECT_ROOT/.clang-format`"

# 检查 clang-format 是否存在
if ! command -v clang-format &> /dev/null; then
    echo "警告: 找不到 clang-format，跳过代码格式化检查"
    exit 0
fi

# 检查 .clang-format 配置文件是否存在
if [ ! -f "`$CLANG_FORMAT" ]; then
    echo "警告: 找不到 .clang-format 配置文件，跳过代码格式化检查"
    exit 0
fi

echo "正在检查代码格式..."

# 获取所有将要提交的 C/C++ 文件
FILES=`$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(c|h|cpp|hpp|cc|cxx)`$ || true`

if [ -z "`$FILES" ]; then
    echo "没有需要格式化的 C/C++ 文件"
    exit 0
fi

# 创建临时目录用于格式化对比
TEMP_DIR=`$(mktemp -d)`
trap "rm -rf $TEMP_DIR" EXIT

# 格式化所有文件并检查是否有变更
FORMATTED=0
for FILE in `$FILES`; do
    if [ ! -f "`$FILE" ]; then
        continue
    fi

    # 保存原始文件的格式
    cp "`$FILE" "`$TEMP_DIR/`$(basename `$FILE`).original"

    # 运行 clang-format
    clang-format -i "`$FILE" 2>/dev/null || true

    # 检查文件是否有变更
    if ! diff -q "`$FILE" "`$TEMP_DIR/`$(basename `$FILE`).original" > /dev/null 2>&1; then
        echo "  格式化: `$FILE"
        FORMATTED=1
    else
        rm "`$TEMP_DIR/`$(basename `$FILE`).original"
    fi
done

# 如果有文件被格式化，则拒绝提交
if [ `$FORMATTED` -eq 1 ]; then
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
"@

    $hookPath = "$HooksDir\pre-commit"
    $hookContent | Out-File -FilePath $hookPath -Encoding UTF8

    # Git Bash 需要 Unix 风格的换行符
    $content = [System.IO.File]::ReadAllText($hookPath) -replace "`r`n", "`n"
    [System.IO.File]::WriteAllText($hookPath, $content)

    Write-Success "pre-commit hook 已安装"
}

# 安装 commit-msg hook
function Install-CommitMsgHook {
    Write-Info "安装 commit-msg hook..."

    $hookContent = @"
#!/bin/bash
#
# Conventional Commits 提交消息验证 Hook
#

set -e

# 获取脚本目录
SCRIPT_DIR="$(cd "`$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/scripts"
PYTHON_SCRIPT="`$SCRIPT_DIR/conventional_commit.py"

# 检查 Python 脚本是否存在
if [ ! -f "`$PYTHON_SCRIPT" ]; then
    echo "警告: 找不到 `$PYTHON_SCRIPT"
    echo "跳过提交消息验证"
    exit 0
fi

# 运行验证
python3 "`$PYTHON_SCRIPT" validate "`$1"
"@

    $hookPath = "$HooksDir\commit-msg"
    $hookContent | Out-File -FilePath $hookPath -Encoding UTF8

    # Git Bash 需要 Unix 风格的换行符
    $content = [System.IO.File]::ReadAllText($hookPath) -replace "`r`n", "`n"
    [System.IO.File]::WriteAllText($hookPath, $content)

    Write-Success "commit-msg hook 已安装"
}

# 安装 prepare-commit-msg hook
function Install-PrepareCommitMsgHook {
    Write-Info "安装 prepare-commit-msg hook..."

    $hookContent = @"
#!/bin/bash
#
# Conventional Commits 提交消息辅助 Hook
#

# 只在交互式提交时运行
if [ -z "`$2" ] || [ "`$2" = "message" ]; then
    exit 0
fi

# 可以在这里添加自动化的提交消息模板
# 例如引用 Issue ID 等
"@

    $hookPath = "$HooksDir\prepare-commit-msg"
    $hookContent | Out-File -FilePath $hookPath -Encoding UTF8

    # Git Bash 需要 Unix 风格的换行符
    $content = [System.IO.File]::ReadAllText($hookPath) -replace "`r`n", "`n"
    [System.IO.File]::WriteAllText($hookPath, $content)

    Write-Success "prepare-commit-msg hook 已安装"
}

# 主函数
function Main {
    Write-Header "Git Hooks 安装工具"

    # 检查是否在 Git 仓库中
    if (-not (Test-Path ".git")) {
        Write-Error "当前目录不是 Git 仓库根目录"
        Write-Info "请在项目根目录运行此脚本"
        exit 1
    }

    # 创建 hooks 目录（如果不存在）
    if (-not (Test-Path $HooksDir)) {
        New-Item -ItemType Directory -Path $HooksDir -Force | Out-Null
    }

    # 检查工具
    $hasClangFormat = $false
    $hasPython = $false

    if (Test-ClangFormat) {
        $hasClangFormat = $true
    }

    if (Test-Python) {
        $hasPython = $true
    }

    if (-not $hasPython) {
        Write-Error "需要 Python 3 才能运行提交消息验证"
        exit 1
    }

    # 检查 Python 脚本是否存在
    if (-not (Test-Path $PythonScript)) {
        Write-Error "找不到 Python 验证脚本: $PythonScript"
        exit 1
    }

    # 安装 hooks
    if ($hasClangFormat) {
        Install-PreCommitHook
    }

    Install-CommitMsgHook
    Install-PrepareCommitMsgHook

    Write-Header "安装完成"
    Write-Success "Git Hooks 已成功安装"
    Write-Info ""
    Write-Info "已安装的 Hooks:"
    Write-Info "  1. pre-commit - 代码格式化检查（clang-format）"
    Write-Info "  2. commit-msg - 提交消息格式验证"
    Write-Info "  3. prepare-commit-msg - 提交消息辅助"
    Write-Info ""
    Write-Info "使用方法:"
    Write-Info "  1. 智能提交（推荐）:"
    Write-Info "     .\scripts\smart_commit.sh"
    Write-Info ""
    Write-Info "  2. 正常提交（自动格式化）:"
    Write-Info "     git commit"
    Write-Info ""
    Write-Info "  3. 格式化所有代码:"
    Write-Info "     Get-ChildItem -Recurse -Filter *.c | ForEach-Object { clang-format -i `$_ }"
    Write-Info ""
    Write-Info "  4. 跳过 hooks（不推荐）:"
    Write-Info "     git commit --no-verify"
    Write-Info ""
}

# 运行主函数
Main
