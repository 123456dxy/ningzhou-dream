#requires -Version 5.1
[CmdletBinding()]
param(
    [string]$ReleaseName = '宁州一梦-完整服务器版-20261015',
    [string]$GameBinary = '',
    [string]$MsvcRuntimeDirectory = ''
)

$ErrorActionPreference = 'Stop'
$Workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Utf8 = [Text.UTF8Encoding]::new($false)
if ([string]::IsNullOrWhiteSpace($ReleaseName) -or $ReleaseName.Length -gt 100 -or
    $ReleaseName.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0 -or
    $ReleaseName.EndsWith('.') -or $ReleaseName.EndsWith(' ')) {
    throw 'ReleaseName must be a single valid folder name.'
}
if ([string]::IsNullOrWhiteSpace($GameBinary)) {
    $GameBinary = Join-Path $Workspace 'build-vs-share/Release/ningzhou_web.exe'
}
$BuiltGame = (Resolve-Path -LiteralPath $GameBinary).Path
if (-not (Test-Path -LiteralPath $BuiltGame -PathType Leaf)) { throw 'Build ningzhou_web before packaging.' }
$RuntimeNames = @('concrt140.dll','msvcp140.dll','msvcp140_1.dll','msvcp140_2.dll',
    'msvcp140_atomic_wait.dll','msvcp140_codecvt_ids.dll','vccorlib140.dll',
    'vcruntime140.dll','vcruntime140_1.dll','vcruntime140_threads.dll')
$RequiresMsvcRuntime = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($BuiltGame)) -match '(?i)(MSVCP140|VCRUNTIME140)(_[A-Za-z0-9_]+)?\.dll'
if ($RequiresMsvcRuntime -and [string]::IsNullOrWhiteSpace($MsvcRuntimeDirectory)) {
    throw 'Pass -MsvcRuntimeDirectory pointing to the official x64 Microsoft.VC143.CRT directory.'
}
if (-not [string]::IsNullOrWhiteSpace($MsvcRuntimeDirectory)) {
    $MsvcRuntimeDirectory = (Resolve-Path -LiteralPath $MsvcRuntimeDirectory).Path
    foreach ($RequiredDll in @('msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $MsvcRuntimeDirectory $RequiredDll) -PathType Leaf)) {
            throw ('Missing required x64 runtime: ' + $RequiredDll)
        }
    }
}

$Stamp = [DateTimeOffset]::UtcNow.ToOffset([TimeSpan]::FromHours(8)).ToString('yyyyMMdd-HHmmss')
$BuildRoot = Join-Path $Workspace ('build/server-folder-' + $Stamp + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
$Staging = Join-Path $BuildRoot 'staging'
$Package = Join-Path $Staging $ReleaseName
$Destination = Join-Path $Workspace 'dist'
New-Item -ItemType Directory -Path $Package,$Destination -Force | Out-Null
Copy-Item -LiteralPath $BuiltGame -Destination (Join-Path $Package 'ningzhou_web.exe')
if (-not [string]::IsNullOrWhiteSpace($MsvcRuntimeDirectory)) {
    foreach ($DllName in $RuntimeNames) {
        $SourceDll = Join-Path $MsvcRuntimeDirectory $DllName
        if (Test-Path -LiteralPath $SourceDll -PathType Leaf) {
            Copy-Item -LiteralPath $SourceDll -Destination (Join-Path $Package $DllName)
        }
    }
}

# 只发布网页运行资源；数据库、私人存档、生成记录和独立斗法实验页不从源码目录拷入。
$SourceWeb = Join-Path $Workspace 'web'
$AllowedExtensions = @('.html','.css','.mjs','.png','.jpg','.jpeg','.webp','.woff','.woff2','.ttf','.ico','.svg','.mp3','.ogg','.wav')
Get-ChildItem -LiteralPath $SourceWeb -File -Recurse | Sort-Object FullName | ForEach-Object {
    if ($AllowedExtensions -notcontains $_.Extension.ToLowerInvariant()) { return }
    $Relative = $_.FullName.Substring($SourceWeb.Length + 1)
    if ($Relative -match '^battle-lab\.(html|css|mjs)$' -or
        $Relative -match '(?i)(^|[\\/])(saves|players|feedback|node_modules|\.git)([\\/]|$)') { return }
    if (($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw ('Runtime assets must not contain linked files: ' + $Relative)
    }
    $Target = Join-Path (Join-Path $Package 'web') $Relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $Target) -Force | Out-Null
    Copy-Item -LiteralPath $_.FullName -Destination $Target
}
foreach ($RequiredAsset in @('index.html','app.mjs','login.mjs','nz-mode.mjs','v2.html')) {
    if (-not (Test-Path -LiteralPath (Join-Path (Join-Path $Package 'web') $RequiredAsset) -PathType Leaf)) {
        throw ('Missing complete-server web asset: ' + $RequiredAsset)
    }
}

# 启动批处理只使用ASCII命令，运行目录由%~dp0确定，中文与空格路径无需修改参数。
$StartTemplate = @'
@echo off
setlocal
cd /d "%~dp0"
echo Ningzhou Dream - full server
echo Open http://127.0.0.1:8106/ in your browser.
echo Keep this window open. Press Ctrl+C to stop the server.
"%~dp0ningzhou_web.exe" --host __HOST__ --port 8106 --web web --data saves --feedback feedback
if errorlevel 1 pause
'@
$StartTemplate = $StartTemplate.Replace("`r`n","`n").Replace("`n","`r`n")
[IO.File]::WriteAllText((Join-Path $Package '启动完整服务器.cmd'), $StartTemplate.Replace('__HOST__','127.0.0.1'), [Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $Package '启动局域网服务器.cmd'), $StartTemplate.Replace('__HOST__','0.0.0.0'), [Text.Encoding]::ASCII)
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '完整服务器版使用说明.txt') -Destination (Join-Path $Package '使用说明.txt')

$Manifest = @(Get-ChildItem -LiteralPath $Package -File -Recurse | Sort-Object FullName | ForEach-Object {
    [ordered]@{path=$_.FullName.Substring($Package.Length+1).Replace('\','/'); length=$_.Length;
        sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
})
[IO.File]::WriteAllText((Join-Path $BuildRoot 'manifest.json'), ($Manifest | ConvertTo-Json -Depth 5), $Utf8)
# 每次都从全新staging打包，避免重打时夹带旧运行目录中产生的账号和存档。
$TemporaryZip = Join-Path $BuildRoot 'server-folder.zip'
Compress-Archive -LiteralPath $Package -DestinationPath $TemporaryZip -CompressionLevel Optimal
$Output = Join-Path $Destination ($ReleaseName + '.zip')
$PendingOutput = Join-Path $Destination ($ReleaseName + '.' + [Guid]::NewGuid().ToString('N') + '.zip.tmp')
Copy-Item -LiteralPath $TemporaryZip -Destination $PendingOutput
Move-Item -LiteralPath $PendingOutput -Destination $Output -Force
$Record = [ordered]@{zip=$Output; size=(Get-Item -LiteralPath $Output).Length;
    sha256=(Get-FileHash -LiteralPath $Output -Algorithm SHA256).Hash; buildRoot=$BuildRoot;
    packageDirectory=$Package; files=$Manifest.Count; gameBinary=$BuiltGame;
    gameSha256=(Get-FileHash -LiteralPath $BuiltGame -Algorithm SHA256).Hash; mode='full-server';
    msvcRuntime=$RequiresMsvcRuntime; createdAt=[DateTimeOffset]::UtcNow.ToOffset([TimeSpan]::FromHours(8)).ToString('o')}
[IO.File]::WriteAllText((Join-Path $BuildRoot 'release.json'), ($Record | ConvertTo-Json -Depth 5), $Utf8)
Write-Output ($Record | ConvertTo-Json -Depth 5)
