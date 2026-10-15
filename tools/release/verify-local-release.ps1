#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$ReleaseRecordPath)

# Exercise the distributed EXE's default mode, not a forced --local override.
# All data remains in a fresh directory below the selected release build.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Net.Http
$utf8 = [Text.UTF8Encoding]::new($false)
$recordPath = (Resolve-Path -LiteralPath $ReleaseRecordPath).Path
$record = [IO.File]::ReadAllText($recordPath,$utf8) | ConvertFrom-Json
$buildRoot = [IO.Path]::GetFullPath([string]$record.buildRoot).TrimEnd('\','/')
if (-not $buildRoot.Equals((Split-Path -Parent $recordPath),[StringComparison]::OrdinalIgnoreCase)) {
    throw 'The release record must be inside its own release build directory.'
}
$exe = (Resolve-Path -LiteralPath ([string]$record.exe)).Path
$manifestValue = [IO.File]::ReadAllText((Join-Path $buildRoot 'manifest.json'),$utf8) | ConvertFrom-Json
$manifest = @($manifestValue)
$testRoot = Join-Path $buildRoot ('本地发行验收 中文空格 ' + [Guid]::NewGuid().ToString('N'))
$exeDirectory = Join-Path $testRoot '只含 EXE 中文目录'
$copiedExe = Join-Path $exeDirectory '宁州一梦-验收.exe'
$dataRoot = Join-Path $testRoot '独立数据 中文目录'
$runtime = Join-Path $dataRoot 'runtime'
$saveRoot = Join-Path $runtime 'saves'
$checks = [Collections.Generic.List[object]]::new()
$launcher = $null
$ownedChild = $null
$port = 0
$baseUrl = ''
$errorText = $null
$completed = $false
$startedAt = (Get-Date).ToUniversalTime().ToString('o')
$handler = [Net.Http.HttpClientHandler]::new()
$handler.UseCookies = $false
$handler.UseProxy = $false
$client = [Net.Http.HttpClient]::new($handler)
$client.Timeout = [TimeSpan]::FromSeconds(10)

function Assert-Check([string]$Name,[bool]$Condition,$Evidence=$null) {
    $checks.Add([ordered]@{name=$Name; passed=$Condition; evidence=$Evidence})
    if (-not $Condition) { throw ('Local release verification failed: ' + $Name) }
}

function Assert-Below([string]$Root,[string]$Path) {
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\','/')
    $pathFull = [IO.Path]::GetFullPath($Path)
    if (-not $pathFull.StartsWith($rootFull+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw ('Refusing a path outside the isolated directory: ' + $pathFull)
    }
    # Do not follow junctions or symlinks into someone else's game data.
    $current = $pathFull
    while ($current.Length -ge $rootFull.Length) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw ('Refusing a linked path: ' + $current)
            }
        }
        if ($current.Equals($rootFull,[StringComparison]::OrdinalIgnoreCase)) { break }
        $current = Split-Path -Parent $current
    }
    return $pathFull
}

function Bytes-Hash([byte[]]$Bytes) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($Bytes)).Replace('-','') }
    finally { $algorithm.Dispose() }
}

function Json-Hash($Value) {
    return Bytes-Hash ($utf8.GetBytes(($Value | ConvertTo-Json -Depth 100 -Compress)))
}

function Free-Port {
    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0)
    try { $listener.Start(); return ([Net.IPEndPoint]$listener.LocalEndpoint).Port }
    finally { $listener.Stop() }
}

function Port-IsListening {
    $connection = [Net.Sockets.TcpClient]::new()
    try {
        $pending = $connection.ConnectAsync('127.0.0.1',$port)
        return $pending.Wait(500) -and $connection.Connected
    } catch { return $false }
    finally { $connection.Dispose() }
}

function Invoke-Http([string]$Path,[string]$Method='GET',$Body=$null) {
    $request = [Net.Http.HttpRequestMessage]::new([Net.Http.HttpMethod]::new($Method),$baseUrl+$Path)
    $response = $null
    try {
        if ($null -ne $Body) {
            $request.Content = [Net.Http.StringContent]::new(($Body | ConvertTo-Json -Depth 100 -Compress),$utf8,'application/json')
        }
        $response = $client.SendAsync($request).GetAwaiter().GetResult()
        $bytes = $response.Content.ReadAsByteArrayAsync().GetAwaiter().GetResult()
        return [pscustomobject]@{status=[int]$response.StatusCode; bytes=$bytes; text=$utf8.GetString($bytes)}
    } finally {
        if ($null -ne $response) { $response.Dispose() }
        $request.Dispose()
    }
}

function Invoke-Api([string]$Path,[string]$Method='GET',$Body=$null) {
    $response = Invoke-Http $Path $Method $Body
    $result = $response.text | ConvertFrom-Json
    if ($response.status -ne 200 -or -not $result.ok) {
        throw ('API failed: ' + $Method + ' ' + $Path + ' HTTP ' + $response.status)
    }
    return $result
}

function New-Launcher([bool]$ExtractOnly=$false) {
    # Start-Process joins arguments; keep quotes around our controlled space path.
    if ($dataRoot.Contains('"') -or $dataRoot.EndsWith('\')) { throw 'Invalid verification directory.' }
    $arguments = @('--data-dir',('"'+$dataRoot+'"'),'--no-open','--headless')
    if ($ExtractOnly) { $arguments += '--extract-only' }
    else { $arguments += @('--port',[string]$port) }
    # Only system DLL search paths are inherited, as on a recipient's PC.
    $originalPath = $env:PATH
    try {
        $env:PATH = (Join-Path $env:SystemRoot 'System32')+';'+$env:SystemRoot
        return Start-Process -FilePath $copiedExe -ArgumentList $arguments -WorkingDirectory $exeDirectory -WindowStyle Hidden -PassThru
    } finally { $env:PATH = $originalPath }
}

function Wait-Exited($Process,[int]$Seconds=40) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        $Process.Refresh()
        if ($Process.HasExited) { $Process.WaitForExit(); return $true }
        Start-Sleep -Milliseconds 100
    }
    return $false
}

function Wait-Ready($Process) {
    $deadline = (Get-Date).AddSeconds(40)
    $statePath = Join-Path $runtime 'launcher-state.json'
    while ((Get-Date) -lt $deadline) {
        $Process.Refresh()
        if ($Process.HasExited) {
            $detail = ''
            $errorPath = Join-Path $runtime 'launcher-error.txt'
            if (Test-Path -LiteralPath $errorPath) { $detail = [IO.File]::ReadAllText($errorPath,$utf8) }
            throw ('Launcher exited before ready: ' + $Process.ExitCode + ' ' + $detail)
        }
        if (Test-Path -LiteralPath $statePath) {
            try { $state = [IO.File]::ReadAllText($statePath,$utf8) | ConvertFrom-Json } catch { $state = $null }
            if ($null -ne $state -and $state.ready -and [int]$state.launcherPid -eq $Process.Id -and [int]$state.port -eq $port) {
                return $state
            }
        }
        Start-Sleep -Milliseconds 100
    }
    throw 'Launcher readiness timed out.'
}

function Request-Stop($Process) {
    $stopPath = Assert-Below $dataRoot (Join-Path $runtime 'launcher-stop')
    [IO.File]::WriteAllText($stopPath,'stop',$utf8)
    Assert-Check 'launcher responds to its own stop request' (Wait-Exited $Process) $Process.Id
    Assert-Check 'launcher exits normally' ($Process.ExitCode -eq 0) $Process.ExitCode
    $deadline = (Get-Date).AddSeconds(5)
    while ((Port-IsListening) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 100 }
    Assert-Check 'isolated listener closes' (-not (Port-IsListening)) $port
    if ($null -ne $ownedChild) {
        Assert-Check 'launcher closes its own game process' (Wait-Exited $ownedChild 5) $ownedChild.Id
    }
}

function Save-Hashes {
    $hashes = @{}
    if (Test-Path -LiteralPath $saveRoot) {
        Get-ChildItem -LiteralPath $saveRoot -File -Recurse | ForEach-Object {
            $path = Assert-Below $saveRoot $_.FullName
            $relative = $path.Substring($saveRoot.Length+1).Replace('\','/')
            $hashes[$relative] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        }
    }
    return $hashes
}

function Same-Hashes($Before,$After) {
    if ($Before.Count -ne $After.Count) { return $false }
    foreach ($name in $Before.Keys) {
        if (-not $After.ContainsKey($name) -or $Before[$name] -cne $After[$name]) { return $false }
    }
    return $true
}

try {
    $null = Assert-Below $buildRoot $testRoot
    Assert-Check 'verification uses a fresh isolated directory' (-not (Test-Path -LiteralPath $testRoot)) $testRoot
    Assert-Check 'release executable matches recorded hash' ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -eq $record.sha256) $record.sha256
    Assert-Check 'manifest count matches release record' ($manifest.Count -eq [int]$record.files -and $manifest.Count -gt 0) $manifest.Count
    $manifestByPath = @{}
    $RuntimeDllPaths = @('concrt140.dll','msvcp140.dll','msvcp140_1.dll','msvcp140_2.dll',
        'msvcp140_atomic_wait.dll','msvcp140_codecvt_ids.dll','vccorlib140.dll',
        'vcruntime140.dll','vcruntime140_1.dll','vcruntime140_threads.dll') | ForEach-Object { 'runtime/'+$_ }
    foreach ($entry in $manifest) {
        $relative = ([string]$entry.path).Replace('\','/')
        $null = Assert-Below $dataRoot (Join-Path $dataRoot $relative)
        $isRuntimeFile = $relative -eq 'runtime/ningzhou_web.exe' -or $relative.StartsWith('runtime/web/',[StringComparison]::Ordinal) -or $RuntimeDllPaths -contains $relative
        $isPrivateFile = $relative -match '(?i)(^|/)(saves|players|feedback)(/|$)|\.(json|jsonl|tsv|log|md|ps1|cmd|bat)$'
        Assert-Check ('payload contains runtime assets only: '+$relative) ($isRuntimeFile -and -not $isPrivateFile -and -not $manifestByPath.ContainsKey($relative))
        $manifestByPath[$relative] = $entry
    }
    Assert-Check 'payload contains the recorded backend' ($manifestByPath.ContainsKey('runtime/ningzhou_web.exe') -and $manifestByPath['runtime/ningzhou_web.exe'].sha256 -eq $record.gameSha256) $record.gameSha256
    New-Item -ItemType Directory -Path $exeDirectory,$dataRoot | Out-Null
    Copy-Item -LiteralPath $exe -Destination $copiedExe
    Assert-Check 'recipient directory contains only the release EXE' (@(Get-ChildItem -LiteralPath $exeDirectory -Force).Count -eq 1 -and (Get-FileHash -LiteralPath $copiedExe -Algorithm SHA256).Hash -eq $record.sha256)

    $launcher = New-Launcher $true
    Assert-Check 'extract only completes without toolchain PATH' (Wait-Exited $launcher 60) $launcher.Id
    Assert-Check 'extract only exits normally' ($launcher.ExitCode -eq 0) $launcher.ExitCode
    $launcher.Dispose(); $launcher = $null
    foreach ($entry in $manifest) {
        $path = Assert-Below $dataRoot (Join-Path $dataRoot ([string]$entry.path))
        $matches = (Test-Path -LiteralPath $path -PathType Leaf) -and (Get-Item -LiteralPath $path).Length -eq [long]$entry.length -and (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -eq $entry.sha256
        Assert-Check ('extracted bytes match manifest: '+$entry.path) $matches $entry.sha256
    }
    $unexpected = @(Get-ChildItem -LiteralPath $dataRoot -File -Recurse -Force | Where-Object {
        $relative = $_.FullName.Substring($dataRoot.Length+1).Replace('\','/')
        $relative -ne '.package-id' -and -not $manifestByPath.ContainsKey($relative)
    })
    Assert-Check 'extraction contains no unlisted files or private metadata' ($unexpected.Count -eq 0) @($unexpected | ForEach-Object Name)
    Assert-Check 'clean package contains no personal saves' ((Save-Hashes).Count -eq 0 -and -not (Test-Path -LiteralPath (Join-Path $runtime 'latest-session.json')))
    Assert-Check 'extracted package marker matches payload' ([IO.File]::ReadAllText((Join-Path $dataRoot '.package-id'),$utf8).Trim() -eq $record.payloadId) $record.payloadId

    $port = Free-Port
    $baseUrl = 'http://127.0.0.1:'+$port
    Assert-Check 'random isolated port is unused' (-not (Port-IsListening)) $port
    $launcher = New-Launcher
    $ready = Wait-Ready $launcher
    $ownedChild = Get-Process -Id ([int]$ready.serverPid)
    Assert-Check 'launcher owns a separate extracted backend' ($ownedChild.Id -ne $launcher.Id -and $ownedChild.Path -eq (Join-Path $runtime 'ningzhou_web.exe')) $ownedChild.Id
    $mode = Invoke-Api '/api/mode'
    Assert-Check 'double click default is local mode' ($mode.local -eq $true) $mode.local
    $cold = Invoke-Api '/api/state'
    Assert-Check 'no cookie or registration is needed to play' ($cold.hasGame -eq $true -and $cold.state.player.name -eq '本地道友') $cold.state.player.name
    $initialSlots = Invoke-Api '/api/slots'
    Assert-Check 'first local game has no preexisting manual character' (@($initialSlots.slots | Where-Object { $_.exists -and -not $_.automatic }).Count -eq 0)
    $localDirectories = @(Get-ChildItem -LiteralPath (Join-Path $saveRoot 'players') -Directory)
    Assert-Check 'only one newly created local player exists' ($localDirectories.Count -eq 1) $localDirectories.Count
    $playerDirectory = Assert-Below $saveRoot $localDirectories[0].FullName
    $sessionPath = Join-Path $playerDirectory 'session.json'
    $slotPath = Join-Path $playerDirectory 'slot2.json'
    Assert-Check 'local play creates no password or login session files' (@(Get-ChildItem -LiteralPath $saveRoot -File -Recurse | Where-Object { $_.Name -in @('password.txt','sessions.tsv') }).Count -eq 0)

    foreach ($endpoint in @(
        @{path='/api/nickinfo?nickname=release-check'; method='GET'},
        @{path='/api/login'; method='POST'}, @{path='/api/logout'; method='POST'},
        @{path='/api/whoami'; method='GET'}, @{path='/api/password'; method='POST'},
        @{path='/api/feedback'; method='POST'}, @{path='/api/leaderboard'; method='GET'},
        @{path='/api/export'; method='GET'}, @{path='/api/tianji/board'; method='GET'},
        @{path='/api/tianji/state'; method='GET'}, @{path='/api/tianji/join'; method='POST'}
    )) {
        $response = if ($endpoint.method -eq 'POST') {
            Invoke-Http $endpoint.path $endpoint.method @{}
        } else { Invoke-Http $endpoint.path $endpoint.method }
        Assert-Check ('local auth or online endpoint is unavailable: '+$endpoint.path) ($response.status -eq 404) $response.status
    }
    foreach ($asset in @(
        @{url='/'; path='runtime/web/index.html'},
        @{url='/styles.css'; path='runtime/web/styles.css'},
        @{url='/app.mjs'; path='runtime/web/app.mjs'},
        @{url='/map.mjs'; path='runtime/web/map.mjs'},
        @{url='/nz-mode.mjs'; path='runtime/web/nz-mode.mjs'},
        @{url='/assets/portraits/player-female.webp'; path='runtime/web/assets/portraits/player-female.webp'},
        @{url='/assets/portraits/beast-4.webp'; path='runtime/web/assets/portraits/beast-4.webp'},
        @{url='/assets/map/ningzhou-atlas.webp'; path='runtime/web/assets/map/ningzhou-atlas.webp'}
    )) {
        Assert-Check ('required runtime asset is packaged: '+$asset.url) ($manifestByPath.ContainsKey($asset.path))
        $response = Invoke-Http $asset.url
        Assert-Check ('HTTP serves exact packaged asset: '+$asset.url) ($response.status -eq 200 -and (Bytes-Hash $response.bytes) -eq $manifestByPath[$asset.path].sha256) $response.bytes.Length
    }

    $startDays = [int]$cold.state.clock.days
    Invoke-Api '/api/plan' 'POST' @{plan='rest'} | Out-Null
    $advanced = Invoke-Api '/api/advance' 'POST' @{days=7; autoStop=$true}
    Assert-Check 'local game advances seven days' ([int]$advanced.state.clock.days -eq $startDays+7) $advanced.state.clock.days
    $saved = Invoke-Api '/api/save' 'POST' @{slot=2}
    $slot = [IO.File]::ReadAllText($slotPath,$utf8) | ConvertFrom-Json
    Assert-Check 'manual slot stores the local character and exact date' ($slot.player.name -eq '本地道友' -and [int]$slot.clock.days -eq [int]$saved.state.clock.days -and [int]$slot.clock.days -eq $startDays+7) $slot.clock.days
    $manualHash = (Get-FileHash -LiteralPath $slotPath -Algorithm SHA256).Hash
    $advancedAgain = Invoke-Api '/api/advance' 'POST' @{days=3; autoStop=$true}
    Assert-Check 'latest progress advances beyond manual checkpoint' ([int]$advancedAgain.state.clock.days -eq $startDays+10) $advancedAgain.state.clock.days
    $before = Invoke-Api '/api/state'
    $beforeStateHash = Json-Hash $before.state
    $beforeSlots = Invoke-Api '/api/slots'
    $beforeSlotListHash = Json-Hash $beforeSlots.slots
    $session = [IO.File]::ReadAllText($sessionPath,$utf8) | ConvertFrom-Json
    Assert-Check 'local session stores latest progress and older manual checkpoint' ($session.player.name -eq '本地道友' -and [int]$session.clock.days -eq $startDays+10 -and [int]$session.checkpoint.days -eq $startDays+7) $session.clock.days
    Assert-Check 'continued play leaves the manual slot unchanged' ((Get-FileHash -LiteralPath $slotPath -Algorithm SHA256).Hash -eq $manualHash) $manualHash
    $saveHashes = Save-Hashes
    Request-Stop $launcher
    $launcher.Dispose(); $launcher = $null
    $ownedChild.Dispose(); $ownedChild = $null
    Assert-Check 'normal shutdown preserves local session and every save' (Same-Hashes $saveHashes (Save-Hashes)) $saveHashes.Count
    Assert-Check 'local shutdown needs no legacy export snapshot' (-not (Test-Path -LiteralPath (Join-Path $runtime 'latest-session.json')))

    $launcher = New-Launcher
    $readyAgain = Wait-Ready $launcher
    $ownedChild = Get-Process -Id ([int]$readyAgain.serverPid)
    $after = Invoke-Api '/api/state'
    Assert-Check 'restart resumes full latest state without login' ($after.hasGame -eq $true -and (Json-Hash $after.state) -eq $beforeStateHash) $beforeStateHash
    $afterSlots = Invoke-Api '/api/slots'
    Assert-Check 'restart preserves every slot summary' ((Json-Hash $afterSlots.slots) -eq $beforeSlotListHash) $beforeSlotListHash
    Assert-Check 'restart preserves exact session and slot bytes' (Same-Hashes $saveHashes (Save-Hashes)) $saveHashes.Count
    Request-Stop $launcher
    $launcher.Dispose(); $launcher = $null
    $ownedChild.Dispose(); $ownedChild = $null

    $repairAsset = $manifestByPath['runtime/web/assets/map/ningzhou-atlas.webp']
    $repairPath = Assert-Below $runtime (Join-Path $dataRoot ([string]$repairAsset.path))
    Remove-Item -LiteralPath $repairPath
    $launcher = New-Launcher $true
    Assert-Check 'missing map repair completes' (Wait-Exited $launcher 60) $launcher.Id
    Assert-Check 'missing map repair exits normally' ($launcher.ExitCode -eq 0) $launcher.ExitCode
    $launcher.Dispose(); $launcher = $null
    Assert-Check 'missing map is restored byte for byte' ((Get-FileHash -LiteralPath $repairPath -Algorithm SHA256).Hash -eq $repairAsset.sha256) $repairAsset.sha256
    Assert-Check 'resource repair preserves every local save byte' (Same-Hashes $saveHashes (Save-Hashes)) $saveHashes.Count
    $launcher = New-Launcher
    $readyAfterRepair = Wait-Ready $launcher
    $ownedChild = Get-Process -Id ([int]$readyAfterRepair.serverPid)
    $repaired = Invoke-Api '/api/state'
    Assert-Check 'repaired release still resumes full latest state' ((Json-Hash $repaired.state) -eq $beforeStateHash) $beforeStateHash
    Request-Stop $launcher
    $launcher.Dispose(); $launcher = $null
    $ownedChild.Dispose(); $ownedChild = $null
    $completed = $true
} catch {
    $errorText = $_.Exception.Message
    throw
} finally {
    # Killing only our Start-Process handle closes the launcher's own Job Object.
    # Never stop processes by name or touch another running game/port.
    if ($null -ne $launcher) {
        try {
            $launcher.Refresh()
            if (-not $launcher.HasExited) {
                if (Test-Path -LiteralPath $runtime -PathType Container) {
                    $stopPath = Assert-Below $dataRoot (Join-Path $runtime 'launcher-stop')
                    [IO.File]::WriteAllText($stopPath,'stop',$utf8)
                }
                if (-not (Wait-Exited $launcher 10)) { $launcher.Kill(); $launcher.WaitForExit() }
            }
        } catch { }
        $launcher.Dispose()
    }
    if ($null -ne $ownedChild) { $ownedChild.Dispose() }
    $client.Dispose()
    $verification = [ordered]@{ok=$completed; startedAt=$startedAt; finishedAt=(Get-Date).ToUniversalTime().ToString('o'); exe=$exe; testRoot=$testRoot; dataRoot=$dataRoot; port=$port; checks=@($checks.ToArray()); error=$errorText}
    [IO.File]::WriteAllText((Join-Path $buildRoot 'local-verification.json'),($verification | ConvertTo-Json -Depth 100),$utf8)
}
Write-Output ($verification | ConvertTo-Json -Depth 100)
