# Conventional Commits Git Hooks 安装脚本 (PowerShell)
# 使用方法:
#   .\scripts\install_hooks.ps1

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

function Write-Info {
    param([string]$Message)
    Write-Host $Message -ForegroundColor Cyan
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
SCRIPT_DIR="$(cd "$(dirname "`${BASH_SOURCE[0]}")/../.." && pwd)/scripts"
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

    # Git Bash 需要Unix 风格的换行符
    $content = [System.IO.File]::ReadAllText($hookPath) -replace "`r`n", "`n"
    [System.IO.File]::WriteAllText($hookPath, $content)

    Write-Success "prepare-commit-msg hook 已安装"
}

# 主函数
function Main {
    Write-Header "Conventional Commits Git Hooks 安装"

    # 检查是否在 Git 仓库中
    if (-not (Test-Path ".git")) {
        Write-Error "当前目录不是 Git 仓库根目录"
        Write-Info "请在项目根目录运行此脚本"
        exit 1
    }

    # 检查 Python
    if (-not (Test-Python)) {
        Write-Error "需要 Python 3 才能运行提交消息验证"
        exit 1
    }

    # 检查 Python 脚本是否存在
    if (-not (Test-Path $PythonScript)) {
        Write-Error "找不到 Python 验证脚本: $PythonScript"
        exit 1
    }

    # 创建 hooks 目录（如果不存在）
    if (-not (Test-Path $HooksDir)) {
        New-Item -ItemType Directory -Path $HooksDir -Force | Out-Null
    }

    # 安装 hooks
    Install-CommitMsgHook
    Install-PrepareCommitMsgHook

    Write-Header "安装完成"
    Write-Success "Git Hooks 已成功安装"
    Write-Info ""
    Write-Info "使用方法:"
    Write-Info "  1. 交互式生成提交消息:"
    Write-Info "     python scripts\conventional_commit.py generate"
    Write-Info ""
    Write-Info "  2. 正常提交（自动验证）:"
    Write-Info "     git commit"
    Write-Info ""
    Write-Info "  3. 查看 Conventional Commits 规范:"
    Write-Info "     cat .commitlintrc.yml"
    Write-Info ""
}

# 运行主函数
Main
