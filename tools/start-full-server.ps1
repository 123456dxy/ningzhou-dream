# 本机完整服务版：保留注册、登录和完整网页，只监听本机，不使用 --local。
[CmdletBinding()]
param([switch]$Background, [switch]$NoOpen)

$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$taskDirectory = Join-Path $taskRoot 'local-server'
$taskRuntime = Join-Path $taskDirectory 'runtime'
$taskRuntimeExe = Join-Path $taskRuntime 'ningzhou_web.exe'
$taskUrl = 'http://127.0.0.1:8108/'

# 用途：保护运行文件复制目标；流程：启动前检查；传仓库内路径，拒绝跳转到外部的重解析点，无返回值。
function Assert-TaskDirectory([string]$Path) {
    $taskAbsolute = [IO.Path]::GetFullPath($Path)
    if (-not $taskAbsolute.StartsWith($taskRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw '运行目录必须位于当前仓库内。'
    }
    if (Test-Path -LiteralPath $Path) {
        $taskEntry = Get-Item -LiteralPath $Path -Force
        if (($taskEntry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw ('运行目录不允许使用目录联接或符号链接：' + $Path)
        }
    }
}

# 用途：检查固定端口并识别自己的完整服务；流程：重复启动；无参数，返回同路径进程或空，其他占用明确报错。
function Get-TaskExistingServer {
    if (-not (Get-Command Get-NetTCPConnection -ErrorAction SilentlyContinue)) {
        throw '当前 Windows 缺少 Get-NetTCPConnection，无法安全检查端口占用。'
    }
    $taskListeners = @(Get-NetTCPConnection -LocalPort 8108 -State Listen -ErrorAction SilentlyContinue)
    if ($taskListeners.Count -eq 0) { return $null }
    $taskOwners = @($taskListeners | Select-Object -ExpandProperty OwningProcess -Unique)
    if ($taskOwners.Count -ne 1) { throw '8108 端口被其他服务占用，请先正常关闭该服务。' }
    $taskOwnerId = [int]$taskOwners[0]
    $taskOwner = Get-CimInstance Win32_Process -Filter ('ProcessId = ' + $taskOwnerId)
    if (-not $taskOwner -or -not $taskOwner.ExecutablePath -or
        -not [string]::Equals([IO.Path]::GetFullPath($taskOwner.ExecutablePath), $taskRuntimeExe, [StringComparison]::OrdinalIgnoreCase) -or
        $taskOwner.CommandLine -match '(?:^|\s)--local(?:\s|$)' -or
        @($taskListeners | Where-Object { $_.LocalAddress -ne '127.0.0.1' }).Count -gt 0) {
        throw ('8108 端口已被其他服务占用（PID ' + $taskOwnerId + '），本入口不会停止它。')
    }
    return $taskOwner
}

# 用途：确认本次子进程已启动完整服务；流程：启动验收；传进程，返回无，失败保留日志并报告。
function Wait-TaskServer($Process) {
    $taskDeadline = [DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $taskDeadline) {
        $Process.Refresh()
        if ($Process.HasExited) { throw ('服务启动失败，退出码 ' + $Process.ExitCode + '，请查看 local-server 的日志。') }
        try {
            $taskMode = Invoke-RestMethod -Uri ($taskUrl + 'api/mode') -TimeoutSec 1
            if (($taskMode.PSObject.Properties.Name -contains 'local') -and $taskMode.local -eq $false) { return }
        } catch { # 监听建立前的短暂连接失败允许继续等待。
        }
        Start-Sleep -Milliseconds 200
    }
    throw '服务未在十五秒内就绪，请查看 local-server 的日志和 server.pid。'
}

try {
    foreach ($taskPath in @($taskDirectory, $taskRuntime, (Join-Path $taskRuntime 'web'), (Join-Path $taskDirectory 'saves'), (Join-Path $taskDirectory 'feedback'))) {
        Assert-TaskDirectory $taskPath
    }
    $taskExisting = Get-TaskExistingServer
    if ($taskExisting) {
        Write-Host ('完整服务已运行，复用 PID ' + $taskExisting.ProcessId + '：' + $taskUrl)
        Write-Host '本次未覆盖正在运行的程序或数据；更新版本请先正常关闭该服务。'
        if (-not $NoOpen) { Start-Process $taskUrl }
        exit 0
    }

    $taskSourceWeb = Join-Path $taskRoot 'web'
    foreach ($taskResource in @('index.html', 'app.mjs', 'login.mjs', 'nz-mode.mjs')) {
        if (-not (Test-Path -LiteralPath (Join-Path $taskSourceWeb $taskResource) -PathType Leaf)) {
            throw ('当前源码缺少完整前端资源：web/' + $taskResource)
        }
    }
    $taskCandidates = @('build-backend-desktop/ningzhou_web.exe', 'build-vs-share/Debug/ningzhou_web.exe',
        'build-vs-share/Release/ningzhou_web.exe', 'build-vs2022/Debug/ningzhou_web.exe', 'build-vs2022/Release/ningzhou_web.exe')
    $taskSourceExe = $taskCandidates | ForEach-Object { Join-Path $taskRoot $_ } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
        ForEach-Object { Get-Item -LiteralPath $_ } | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if (-not $taskSourceExe) { throw '尚未构建服务程序，请先构建 visual-studio/ningzhou.sln 或 CMake mingw-debug。' }

    foreach ($taskPath in @($taskRuntime, (Join-Path $taskRuntime 'web'), (Join-Path $taskDirectory 'saves'), (Join-Path $taskDirectory 'feedback'))) {
        New-Item -ItemType Directory -Path $taskPath -Force | Out-Null
    }
    # 只覆盖受控的程序和网页；不删除、替换或导入玩家存档与反馈。
    foreach ($taskEntry in Get-ChildItem -LiteralPath $taskRuntime -Recurse -Force) {
        if (($taskEntry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw ('运行资源存在不安全的链接：' + $taskEntry.FullName) }
    }
    Copy-Item -LiteralPath $taskSourceExe.FullName -Destination $taskRuntimeExe -Force
    foreach ($taskDll in Get-ChildItem -LiteralPath $taskSourceExe.DirectoryName -Filter '*.dll' -File) {
        Copy-Item -LiteralPath $taskDll.FullName -Destination $taskRuntime -Force
    }
    foreach ($taskEntry in Get-ChildItem -LiteralPath $taskSourceWeb -Force) {
        Copy-Item -LiteralPath $taskEntry.FullName -Destination (Join-Path $taskRuntime 'web') -Recurse -Force
    }

    $taskStamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
    $taskStdout = Join-Path $taskDirectory ('server-' + $taskStamp + '.stdout.log')
    $taskStderr = Join-Path $taskDirectory ('server-' + $taskStamp + '.stderr.log')
    # 参数采用运行目录相对路径，避免 Windows main(argv) 的中文目录编码问题。
    $taskArguments = @('--host', '127.0.0.1', '--port', '8108', '--web', 'web', '--data', '../saves', '--feedback', '../feedback', '--no-open')
    $taskStart = @{ FilePath=$taskRuntimeExe; ArgumentList=$taskArguments; WorkingDirectory=$taskRuntime;
        RedirectStandardOutput=$taskStdout; RedirectStandardError=$taskStderr; PassThru=$true }
    if ($Background) { $taskStart.WindowStyle='Hidden' } else { $taskStart.NoNewWindow=$true }
    $taskProcess = Start-Process @taskStart
    [IO.File]::WriteAllText((Join-Path $taskDirectory 'server.pid'), [string]$taskProcess.Id, [Text.UTF8Encoding]::new($false))
    $taskMetadata = @{ pid=$taskProcess.Id; url=$taskUrl; executable=$taskRuntimeExe; sourceExecutable=$taskSourceExe.FullName;
        startedAt=(Get-Date).ToString('o'); stdout=$taskStdout; stderr=$taskStderr; background=[bool]$Background }
    [IO.File]::WriteAllText((Join-Path $taskDirectory 'server.json'), ($taskMetadata | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
    Wait-TaskServer $taskProcess
    Write-Host ('本机完整服务已就绪：' + $taskUrl + '（PID ' + $taskProcess.Id + '）')
    Write-Host ('数据：' + (Join-Path $taskDirectory 'saves') + '；日志：' + $taskStdout)
    if (-not $NoOpen) { Start-Process $taskUrl }
    if ($Background) { exit 0 }
    Write-Host '前台运行中，按 Ctrl+C 保存并关闭服务；再次打开入口会复用此服务。'
    $taskProcess.WaitForExit()
    exit $taskProcess.ExitCode
} catch {
    [Console]::Error.WriteLine('完整服务启动失败：' + $_.Exception.Message)
    exit 1
}
