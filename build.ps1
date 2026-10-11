# 宁州一梦 C++ 版 — 构建脚本
# 用法：
#   .\build.ps1              # 构建 Release
#   .\build.ps1 -Debug       # 构建 Debug（含调试符号，便于 gdb）
#   .\build.ps1 -Run         # 构建后直接运行（控制台版）
#   .\build.ps1 -Web         # 构建后启动网页版（推荐，界面和原版一致）
#   .\build.ps1 -Test        # 构建后运行全部自测
#   .\build.ps1 -Portable    # 构建后验证 exe 不依赖工具链 DLL
#   .\build.ps1 -Clean       # 先删 build/

param(
    [switch]$Debug,
    [switch]$Run,
    [switch]$Web,
    [switch]$Test,
    [switch]$UI,
    [switch]$Clean,
    [switch]$Portable
)

$ErrorActionPreference = 'Stop'

# MSYS2 UCRT64 工具链。必须把 ucrt64\bin 加进 PATH，
# 否则 cc1plus.exe 会因找不到运行时 DLL 而静默失败（0xC0000135）。
$ToolchainBin = 'C:\msys64\ucrt64\bin'
if (-not (Test-Path (Join-Path $ToolchainBin 'g++.exe'))) {
    throw "找不到 C++ 编译器：$ToolchainBin\g++.exe。请先安装 MSYS2 的 mingw-w64-ucrt-x86_64-gcc。"
}
$env:PATH = "$ToolchainBin;$env:PATH"

$Root = $PSScriptRoot
$BuildDir = Join-Path $Root 'build'

if ($Clean -and (Test-Path $BuildDir)) {
    Remove-Item -Recurse -Force $BuildDir
    Write-Host "已清除 build 目录" -ForegroundColor Yellow
}

$Config = if ($Debug) { 'Debug' } else { 'Release' }

# 原生程序（cmake/g++）往 stderr 写信息时，PowerShell 默认会当成终止错误，
# 所以这里把 stderr 重定向到文件，只按退出码判断成败。
$ConfigureLog = Join-Path $BuildDir 'configure.log'
$BuildLog = Join-Path $BuildDir 'build.log'
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$configureArgs = @(
    '-S', $Root, '-B', $BuildDir, '-G', 'Ninja',
    "-DCMAKE_BUILD_TYPE=$Config",
    "-DCMAKE_CXX_COMPILER=$ToolchainBin/g++.exe"
)
& cmake @configureArgs 2>&1 | Tee-Object -FilePath $ConfigureLog | Out-Null
if ($LASTEXITCODE -ne 0) {
    Get-Content $ConfigureLog | Select-Object -Last 30
    throw 'CMake 配置失败'
}

& cmake --build $BuildDir 2>&1 | Tee-Object -FilePath $BuildLog | Out-Null
if ($LASTEXITCODE -ne 0) {
    Get-Content $BuildLog | Select-Object -Last 40
    throw '编译失败'
}

$Exe = Join-Path $BuildDir 'ningzhou.exe'
if (Test-Path $Exe) {
    $size = (Get-Item $Exe).Length
    Write-Host ("构建成功：{0} ({1:N0} 字节, {2})" -f $Exe, $size, $Config) -ForegroundColor Green
}

# 生成的 exe 依赖 UCRT64 运行库（libstdc++-6.dll 等），
# 所以运行前必须把 $ToolchainBin 放进 PATH，否则会以 0xC0000135 静默退出。
if ($Test) {
    $testExe = Join-Path $BuildDir 'ningzhou_tests.exe'
    if (Test-Path $testExe) {
        Write-Host "`n===== 运行自测 =====" -ForegroundColor Cyan
        & $testExe
        if ($LASTEXITCODE -ne 0) { throw '游戏自测失败，请查看失败的检查项。' }
    } else {
        throw '未找到测试可执行文件 ningzhou_tests.exe'
    }

    # 静态审计：死函数、死变量、前后端字段契约、路由、未定义调用。
    # 这一层专治「代码写好了却没接上」——本项目已经栽过好几次
    # （hand 没插进返回值、sectPanel 死代码、battleToJson 没人调用）。
    $audit = Join-Path $Root 'tools\audit.mjs'
    if ((Test-Path $audit) -and (Get-Command node -ErrorAction SilentlyContinue)) {
        Write-Host "`n===== 静态审计 =====" -ForegroundColor Cyan
        & node $audit
        if ($LASTEXITCODE -ne 0) {
            throw '静态审计未通过，请查看上面的清单。'
        }
    }

    $webCheck = Join-Path $Root 'tools\check-web.mjs'
    if ((Test-Path $webCheck) -and (Get-Command node -ErrorAction SilentlyContinue)) {
        Write-Host "`n===== 前端交互回归 =====" -ForegroundColor Cyan
        & node $webCheck
        if ($LASTEXITCODE -ne 0) { throw '前端交互回归未通过。' }
    }

    # 真浏览器点击测试（CDP）：起无头 Chrome，派发真实鼠标事件并截图。
    # 这一层抓静态审计查不出的东西：CSS 没生效、按钮被遮住、
    # 元素在视口外点不到、真浏览器里才报的错。
    # 需要先有一个在跑的服务；-UI 显式开启，避免默认拖慢构建。
    $uitest = Join-Path $Root 'tools\uitest.mjs'
    if ($UI -and (Test-Path $uitest) -and (Get-Command node -ErrorAction SilentlyContinue)) {
        Write-Host "`n===== 浏览器点击测试 =====" -ForegroundColor Cyan
        & node $uitest --play
        if ($LASTEXITCODE -ne 0) {
            throw '浏览器测试未通过，截图在 build\shots\。'
        }
    }
}

if ($Run) {
    Write-Host "`n===== 启动游戏（控制台版）=====" -ForegroundColor Cyan
    & $Exe
}

if ($Web) {
    $webExe = Join-Path $BuildDir 'ningzhou_web.exe'
    if (Test-Path $webExe) {
        Write-Host "`n===== 启动网页版 =====" -ForegroundColor Cyan
        Write-Host "浏览器会自动打开。按 Ctrl+C 停止服务。" -ForegroundColor Yellow
        # 先把前端同步一次，方便改完 web/ 直接看效果
        Copy-Item (Join-Path $Root 'web\*') (Join-Path $BuildDir 'web') -Recurse -Force
        & $webExe
    } else {
        Write-Warning "未找到 ningzhou_web.exe"
    }
}

# 验证 exe 不依赖工具链 DLL（静态链接是否生效）。
# 缺 DLL 时程序会静默闪退，退出码 0xC0000135，很难人工发现。
if ($Portable) {
    Write-Host "`n===== 检查可移植性 =====" -ForegroundColor Cyan
    $objdump = Join-Path $ToolchainBin 'objdump.exe'
    if (Test-Path $objdump) {
        $deps = & $objdump -p $Exe 2>$null |
                Select-String 'DLL Name' |
                ForEach-Object { ($_.Line -replace '.*DLL Name:\s*', '').Trim() } |
                Sort-Object -Unique |
                Where-Object { $_ -notmatch '^(api-ms-win|KERNEL32|USER32|ADVAPI32|SHELL32|ole32|OLEAUT32|uuid|comdlg32|gdi32|winspool|msvcrt)' }
        if ($deps.Count -eq 0) {
            Write-Host "  通过：没有依赖工具链 DLL，exe 可以单独拷走运行" -ForegroundColor Green
        } else {
            Write-Host "  失败：仍依赖以下 DLL，静态链接可能没生效：" -ForegroundColor Red
            $deps | ForEach-Object { Write-Host "    $_" }
            throw "exe 不可移植"
        }
    }

    # 真正清掉 PATH 再跑一次，这才是双击时的真实情况
    $savedPath = $env:PATH
    $env:PATH = ($env:PATH -split ';' | Where-Object { $_ -notmatch 'msys64' }) -join ';'
    & $Exe --help > $null 2>&1
    $code = $LASTEXITCODE
    $env:PATH = $savedPath
    if ($code -eq 0) {
        Write-Host "  通过：去掉 msys64 的 PATH 后仍能正常启动" -ForegroundColor Green
    } else {
        Write-Host ("  失败：去掉 PATH 后退出码 {0}（0xC0000135 = 缺 DLL）" -f $code) -ForegroundColor Red
        throw "exe 在无工具链环境下无法运行"
    }
}
