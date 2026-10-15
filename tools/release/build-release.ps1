param(
    [string]$ReleaseName = '宁州一梦-单文件版-20261015',
    [switch]$Local,
    [string]$GameBinary = '',
    [string]$MsvcRuntimeDirectory = ''
)
$ErrorActionPreference = 'Stop'
# Keep the escaped -D string arguments compatible with Windows PowerShell 5.1
# and PowerShell 7. This script-scoped setting does not change the caller.
$PSNativeCommandArgumentPassing = 'Legacy'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$compilerRoot = 'C:/msys64/ucrt64/bin'
$compiler = Join-Path $compilerRoot 'g++.exe'
$windres = Join-Path $compilerRoot 'windres.exe'
$env:PATH = $compilerRoot + ';' + $env:PATH
if ([string]::IsNullOrWhiteSpace($GameBinary)) {
    $GameBinary = Join-Path $workspace 'build-vs-share/Release/ningzhou_web.exe'
    if (-not (Test-Path -LiteralPath $GameBinary)) { $GameBinary = Join-Path $workspace 'build/ningzhou_web.exe' }
}
if (-not (Test-Path -LiteralPath $GameBinary -PathType Leaf)) { throw 'Build the latest game before packaging, or pass -GameBinary.' }
$builtGame = (Resolve-Path -LiteralPath $GameBinary).Path
# 运行库只从调用者指定的官方 VC Redist 目录取白名单文件，不复制开发机任意 DLL 或玩家数据。
$MsvcRuntimeNames = @('concrt140.dll','msvcp140.dll','msvcp140_1.dll','msvcp140_2.dll',
    'msvcp140_atomic_wait.dll','msvcp140_codecvt_ids.dll','vccorlib140.dll',
    'vcruntime140.dll','vcruntime140_1.dll','vcruntime140_threads.dll')
$RequiresMsvcRuntime = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($builtGame)) -match '(?i)(MSVCP140|VCRUNTIME140)(_[A-Za-z0-9_]+)?\.dll'
if ($RequiresMsvcRuntime -and [string]::IsNullOrWhiteSpace($MsvcRuntimeDirectory)) {
    throw 'This MSVC build needs app-local runtime DLLs. Pass -MsvcRuntimeDirectory pointing to the official x64 Microsoft.VC143.CRT directory.'
}
if (-not [string]::IsNullOrWhiteSpace($MsvcRuntimeDirectory)) {
    $MsvcRuntimeDirectory = (Resolve-Path -LiteralPath $MsvcRuntimeDirectory).Path
    foreach ($RequiredDll in @('msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $MsvcRuntimeDirectory $RequiredDll) -PathType Leaf)) {
            throw ('Missing required x64 MSVC runtime: '+$RequiredDll)
        }
    }
}
$buildRoot = Join-Path $workspace ('build/release-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$staging = Join-Path $buildRoot 'staging'
$runtime = Join-Path $staging 'runtime'
$destination = Join-Path $workspace 'dist'
New-Item -ItemType Directory -Path $runtime,$destination -Force | Out-Null
Copy-Item -LiteralPath $builtGame -Destination (Join-Path $runtime 'ningzhou_web.exe')
if (-not [string]::IsNullOrWhiteSpace($MsvcRuntimeDirectory)) {
    foreach ($DllName in $MsvcRuntimeNames) {
        $DllSource = Join-Path $MsvcRuntimeDirectory $DllName
        if (Test-Path -LiteralPath $DllSource -PathType Leaf) {
            Copy-Item -LiteralPath $DllSource -Destination (Join-Path $runtime $DllName)
        }
    }
}
# Only game runtime assets. Generator metadata, private paths, test reports and saves are excluded.
$assetRoot = Join-Path $workspace 'web'
$allowed = @('.html','.css','.mjs','.png','.jpg','.jpeg','.webp','.woff','.woff2','.ttf','.ico','.svg','.mp3','.ogg','.wav')
$manifest = @()
Get-ChildItem -LiteralPath $assetRoot -File -Recurse | Sort-Object FullName | ForEach-Object {
    if ($allowed -notcontains $_.Extension.ToLowerInvariant()) { return }
    # 注意：Windows PowerShell 5.1 的 [IO.Path] 没有 GetRelativePath，改用前缀截取，5.1/7 都能跑。
    $relative = $_.FullName.Substring($assetRoot.Length + 1)
    # 独立斗法试炼仅在开发者本机使用，不进入正式网页或单文件发行包。
    if ($relative -match '^battle-lab\.(html|css|mjs)$') { return }
    $target = Join-Path (Join-Path $runtime 'web') $relative
    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $_.FullName -Destination $target
}
Get-ChildItem -LiteralPath $staging -File -Recurse | Sort-Object FullName | ForEach-Object {
    $manifest += [ordered]@{path=$_.FullName.Substring($staging.Length + 1).Replace('\','/'); length=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
}
$manifestJson = $manifest | ConvertTo-Json -Depth 10
[IO.File]::WriteAllText((Join-Path $buildRoot 'manifest.json'),$manifestJson,[Text.UTF8Encoding]::new($false))
$packer = Join-Path $buildRoot 'release_pack.exe'
& $compiler '-std=c++20' '-O2' '-municode' '-static' '-static-libgcc' '-static-libstdc++' (Join-Path $PSScriptRoot 'pack.cpp') '-lcabinet' '-o' $packer
if ($LASTEXITCODE -ne 0) { throw 'Could not build package writer.' }
$payload = Join-Path $buildRoot 'payload.bin'
& $packer $staging $payload
if ($LASTEXITCODE -ne 0) { throw 'Could not create embedded payload.' }
$packageId = (Get-FileHash -LiteralPath $payload -Algorithm SHA256).Hash
$resource = Join-Path $buildRoot 'payload.rc'
$resourceText = @'
101 RCDATA "payload.bin"
1 24 "app.manifest"
1 VERSIONINFO
 FILEVERSION 6,2026,10,15
 PRODUCTVERSION 6,2026,10,15
 FILEOS 0x40004
 FILETYPE 0x1
 BEGIN
  BLOCK "StringFileInfo"
  BEGIN
   BLOCK "040904B0"
   BEGIN
    VALUE "FileDescription", "Ningzhou Dream - Single-file Game\0"
    VALUE "ProductName", "Ningzhou Dream\0"
    VALUE "FileVersion", "6.2026.10.15\0"
    VALUE "OriginalFilename", "NingzhouDream.exe\0"
   END
  END
  BLOCK "VarFileInfo"
  BEGIN
   VALUE "Translation", 0x0409, 1200
  END
 END
'@
[IO.File]::WriteAllText($resource,$resourceText,[Text.Encoding]::ASCII)
$appManifest = @'
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
 <assemblyIdentity version="6.2026.10.15" processorArchitecture="amd64" name="NingzhouDream.Game" type="win32"/>
 <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3"><security><requestedPrivileges><requestedExecutionLevel level="asInvoker" uiAccess="false"/></requestedPrivileges></security></trustInfo>
 <application xmlns="urn:schemas-microsoft-com:asm.v3"><windowsSettings><dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true</dpiAware></windowsSettings></application>
 <compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1"><application><supportedOS Id="{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"/></application></compatibility>
</assembly>
'@
[IO.File]::WriteAllText((Join-Path $buildRoot 'app.manifest'),$appManifest,[Text.UTF8Encoding]::new($false))
Push-Location $buildRoot
try {
    & $windres '--input=payload.rc' '--output=payload.o' '--output-format=coff' 2>&1 | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) { throw 'Could not embed package resource.' }
} finally { Pop-Location }
$output = Join-Path $destination ($ReleaseName + '.exe')
$localDefine = if ($Local) { '-DNZ_LOCAL_MODE=1' } else { '-DNZ_LOCAL_MODE=0' }
# 双引号要写成 \"：Windows PowerShell 5.1 会吃掉 -D 值里的普通引号，宏会变成裸十六进制串而编译失败（invalid digit in octal constant）。
$packageDefine = '-DNZ_PACKAGE_ID=\"' + $packageId + '\"'
$launcherArgs = @(
    '-std=c++20','-O2','-Wall','-Wextra','-municode','-mwindows','-static','-static-libgcc','-static-libstdc++',
    '-finput-charset=UTF-8','-fexec-charset=UTF-8',$localDefine,$packageDefine,
    ('-I' + (Join-Path $workspace 'src')),(Join-Path $PSScriptRoot 'launcher.cpp'),
    (Join-Path $workspace 'src/core/foundation/json.cpp'),(Join-Path $buildRoot 'payload.o'),
    '-lcabinet','-lwinhttp','-lws2_32','-lshell32','-lole32','-luuid','-lcomctl32','-o',$output)
# g++ 的告警走 stderr，Windows PowerShell 5.1 在 Stop 偏好下会把它当成终止错误，这里临时放宽（5.1/7 都能跑）。
$previousPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try { & $compiler @launcherArgs } finally { $ErrorActionPreference = $previousPreference }
if ($LASTEXITCODE -ne 0) { throw 'Could not build the single-file launcher.' }
$record = [ordered]@{exe=$output; size=(Get-Item -LiteralPath $output).Length; sha256=(Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash; payloadId=$packageId; buildRoot=$buildRoot; files=$manifest.Count; gameSha256=(Get-FileHash -LiteralPath $builtGame -Algorithm SHA256).Hash; gameBinary=$builtGame; local=[bool]$Local; msvcRuntime=$RequiresMsvcRuntime}
[IO.File]::WriteAllText((Join-Path $buildRoot 'release.json'),($record | ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
Write-Output ($record | ConvertTo-Json -Depth 10)
