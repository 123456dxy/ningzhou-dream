# 本地电脑端入口：只使用当前仓库构建，不同步或覆盖玩家存档。
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskCandidates = @(
    'build-backend-desktop/ningzhou_web.exe',
    'build-vs-share/Debug/ningzhou_web.exe',
    'build-vs-share/Release/ningzhou_web.exe',
    'build-vs2022/Debug/ningzhou_web.exe',
    'build-vs2022/Release/ningzhou_web.exe'
) | ForEach-Object { Join-Path $taskRoot $_ } | Where-Object { Test-Path -LiteralPath $_ }
if (-not $taskCandidates) { throw '请先构建 visual-studio/ningzhou.sln 或运行 CMake mingw-debug 预设。' }
$taskExecutable = $taskCandidates | ForEach-Object { Get-Item -LiteralPath $_ } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not (Test-Path -LiteralPath (Join-Path $taskExecutable.DirectoryName 'web/index.html'))) {
    throw '该构建缺少 web/index.html，请完整构建解决方案以同步界面资源。'
}
Set-Location -LiteralPath $taskRoot
Write-Host ('本地电脑端：' + $taskExecutable.FullName)
Write-Host '浏览器地址 http://127.0.0.1:8107/；按 Ctrl+C 保存并关闭服务。'
& $taskExecutable.FullName --local --host 127.0.0.1 --port 8107 --data saves-local
exit $LASTEXITCODE
