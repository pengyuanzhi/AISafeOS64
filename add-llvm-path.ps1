# LLVM PATH 配置脚本
# 使用管理员权限运行此脚本可添加到系统 PATH
# 使用普通用户权限运行可添加到用户 PATH

$PathToAdd = "C:\Program Files\LLVM\bin"

# 获取当前用户 PATH
$CurrentUserPath = [Environment]::GetEnvironmentVariable("Path", "User")

# 检查是否已存在
if ($CurrentUserPath -split ';' | Where-Object { $_ -eq $PathToAdd }) {
    Write-Host "✅ LLVM bin 目录已在 PATH 中" -ForegroundColor Green
} else {
    Write-Host "正在添加 LLVM 到用户 PATH..." -ForegroundColor Yellow

    # 添加到 PATH
    if ($CurrentUserPath) {
        [Environment]::SetEnvironmentVariable("Path", "$CurrentUserPath;$PathToAdd", "User")
    } else {
        [Environment]::SetEnvironmentVariable("Path", $PathToAdd, "User")
    }

    Write-Host "✅ 成功添加 LLVM 到用户 PATH" -ForegroundColor Green
    Write-Host ""
    Write-Host "⚠️  请执行以下操作使配置生效：" -ForegroundColor Yellow
    Write-Host "   1. 重启 VS Code" -ForegroundColor Cyan
    Write-Host "   2. 或重启终端（关闭并重新打开）" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "重启后运行以下命令验证：" -ForegroundColor Yellow
    Write-Host "   clang-format --version" -ForegroundColor Cyan
}
