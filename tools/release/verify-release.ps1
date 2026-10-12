param([Parameter(Mandatory=$true)][string]$ReleaseRecordPath)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$utf8 = [Text.UTF8Encoding]::new($false)
$recordPath = (Resolve-Path -LiteralPath $ReleaseRecordPath).Path
$record = [IO.File]::ReadAllText($recordPath,$utf8) | ConvertFrom-Json
$buildRoot = [IO.Path]::GetFullPath([string]$record.buildRoot)
$exe = (Resolve-Path -LiteralPath ([string]$record.exe)).Path
$manifest = @([IO.File]::ReadAllText((Join-Path $buildRoot 'manifest.json'),$utf8) | ConvertFrom-Json)
$dataRoot = Join-Path $buildRoot ('验收 中文空格 ' + [Guid]::NewGuid().ToString('N'))
$runtime = Join-Path $dataRoot 'runtime'
$port = 18109
$baseUrl = 'http://127.0.0.1:' + $port
$checks = [Collections.Generic.List[object]]::new()
$launcher = $null
$serverId = 0
$errorText = $null
$completed = $false
$startedAt = (Get-Date).ToUniversalTime().ToString('o')

function Assert-Check([string]$Name,[bool]$Condition,$Evidence) {
    $checks.Add([ordered]@{name=$Name; passed=$Condition; evidence=$Evidence})
    if (-not $Condition) { throw ('Release verification failed: ' + $Name) }
}

function Port-IsListening {
    $client = [Net.Sockets.TcpClient]::new()
    try {
        $pending = $client.ConnectAsync('127.0.0.1',$port)
        if (-not $pending.Wait(500)) { return $false }
        return $client.Connected
    } catch { return $false }
    finally { $client.Dispose() }
}

function Invoke-Api([string]$Path,[string]$Method='GET',$Body=$null) {
    $arguments = @{Uri=($baseUrl+$Path); Method=$Method; TimeoutSec=10; UseBasicParsing=$true}
    if ($null -ne $Body) {
        $arguments.Body = $utf8.GetBytes(($Body | ConvertTo-Json -Depth 100 -Compress))
        $arguments.ContentType = 'application/json; charset=utf-8'
    }
    $response = Invoke-WebRequest @arguments
    $result = $response.Content | ConvertFrom-Json
    if (-not $result.ok) { throw ('API failed: ' + $Path + ' ' + $response.Content) }
    return $result
}

function New-Launcher([bool]$ExtractOnly=$false) {
    # Start-Process joins ArgumentList into a command line: retain quotes around this
    # controlled path containing spaces. No shell evaluates any argument.
    if ($dataRoot.Contains('"') -or $dataRoot.EndsWith('\')) { throw 'Invalid verification directory.' }
    $arguments = @('--data-dir',('"'+$dataRoot+'"'),'--no-open','--headless')
    if ($ExtractOnly) { $arguments += '--extract-only' }
    else { $arguments += @('--port',[string]$port) }
    return Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $buildRoot -WindowStyle Hidden -PassThru
}

function Wait-Exited($Process,[int]$Seconds=40) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        $Process.Refresh()
        if ($Process.HasExited) { $Process.WaitForExit(); return $true }
        Start-Sleep -Milliseconds 200
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
            try {
                $state = [IO.File]::ReadAllText($statePath,$utf8) | ConvertFrom-Json
                if ($state.ready -and [int]$state.launcherPid -eq $Process.Id -and [int]$state.port -eq $port) { return $state }
            } catch { }
        }
        Start-Sleep -Milliseconds 200
    }
    throw 'Launcher readiness timed out.'
}

function Request-Stop($Process) {
    [IO.File]::WriteAllText((Join-Path $runtime 'launcher-stop'),'stop',$utf8)
    if (-not (Wait-Exited $Process 40)) { throw 'Launcher did not close after the controlled stop request.' }
    Assert-Check 'launcher exits normally' ($Process.ExitCode -eq 0) $Process.ExitCode
    $portDeadline = (Get-Date).AddSeconds(5)
    while ((Port-IsListening) -and (Get-Date) -lt $portDeadline) { Start-Sleep -Milliseconds 100 }
    Assert-Check 'test port is closed' (-not (Port-IsListening)) $port
    $child = Get-Process -Id $serverId -ErrorAction SilentlyContinue
    $isOwnChild = $null -ne $child -and $child.Path -eq (Join-Path $runtime 'ningzhou_web.exe')
    Assert-Check 'owned game process is closed' (-not $isOwnChild) $serverId
}

function Slot-Hashes {
    $saveRoot = Join-Path $runtime 'saves'
    $hashes = @{}
    if (Test-Path -LiteralPath $saveRoot) {
        Get-ChildItem -LiteralPath $saveRoot -File -Filter 'slot*.json' | ForEach-Object {
            $hashes[$_.Name] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        }
    }
    return $hashes
}

try {
    Assert-Check 'isolated port was unused' (-not (Port-IsListening)) $port
    Assert-Check 'release executable hash' ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -eq $record.sha256) $record.sha256
    Assert-Check 'manifest has every payload file' ($manifest.Count -eq [int]$record.files) $manifest.Count
    Assert-Check 'verification directory is below this release build' ($dataRoot.StartsWith($buildRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) $dataRoot
    New-Item -ItemType Directory -Path $dataRoot | Out-Null

    $launcher = New-Launcher $true
    Assert-Check 'extract only completes' (Wait-Exited $launcher 60) $launcher.Id
    Assert-Check 'extract only exits zero' ($launcher.ExitCode -eq 0) $launcher.ExitCode
    $launcher = $null
    $allCorrect = $true
    foreach ($entry in $manifest) {
        $file = Join-Path $dataRoot ([string]$entry.path)
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { $allCorrect = $false; break }
        if ((Get-Item -LiteralPath $file).Length -ne [long]$entry.length -or
            (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne [string]$entry.sha256) { $allCorrect = $false; break }
    }
    Assert-Check 'every extracted file matches manifest bytes' $allCorrect $manifest.Count
    $initialFiles = @(Get-ChildItem -LiteralPath $dataRoot -File -Recurse)
    $unexpected = @($initialFiles | Where-Object {
        $relative = [IO.Path]::GetRelativePath($dataRoot,$_.FullName).Replace('\','/')
        $relative -ne '.package-id' -and $relative -notin @($manifest.path)
    })
    Assert-Check 'no unlisted metadata or personal saves are extracted' ($unexpected.Count -eq 0) @($unexpected | ForEach-Object FullName)
    Assert-Check 'clean package has no save slots or resumed session' ((Slot-Hashes).Count -eq 0 -and -not (Test-Path -LiteralPath (Join-Path $runtime 'latest-session.json'))) $initialFiles.Count
    Assert-Check 'package marker identifies this payload' ([IO.File]::ReadAllText((Join-Path $dataRoot '.package-id'),$utf8).Trim() -eq $record.payloadId) $record.payloadId

    $launcher = New-Launcher
    $ready = Wait-Ready $launcher
    $serverId = [int]$ready.serverPid
    Assert-Check 'first launch has independent child process' ($serverId -gt 0 -and $serverId -ne $launcher.Id) $ready
    $cold = Invoke-Api '/api/state'
    Assert-Check 'first launch opens character creation' (-not $cold.hasGame) $cold
    $slots = Invoke-Api '/api/slots'
    Assert-Check 'first launch has no private save slots' (@($slots.slots | Where-Object exists).Count -eq 0) $slots.slots
    foreach ($assetPath in @('/','/styles.css','/artifacts.mjs','/assets/portraits/player-female.webp','/assets/portraits/beast-4.webp','/assets/map/ningzhou-atlas.webp')) {
        $response = Invoke-WebRequest -Uri ($baseUrl+$assetPath) -TimeoutSec 10 -UseBasicParsing
        Assert-Check ('runtime asset loads: '+$assetPath) ($response.StatusCode -eq 200 -and $response.RawContentLength -gt 0) $response.RawContentLength
    }
    $created = Invoke-Api '/api/new' 'POST' @{name='发行验收'; seed=121009; traits=@('root','steady')}
    Assert-Check 'new character uses requested Unicode name' ($created.state.player.name -eq '发行验收') $created.state.player.name
    Invoke-Api '/api/plan' 'POST' @{plan='rest'} | Out-Null
    $startDays = [int]$created.state.clock.days
    $advanced = Invoke-Api '/api/advance' 'POST' @{days=7; autoStop=$true}
    Assert-Check 'playable game advances seven days' ([int]$advanced.state.clock.days -eq $startDays+7) $advanced.report
    Invoke-Api '/api/save' 'POST' @{slot=2} | Out-Null
    $manual = Invoke-Api '/api/slots'
    Assert-Check 'manual slot is written in isolated directory' (@($manual.slots | Where-Object { $_.slot -eq 2 -and $_.exists -and $_.name -eq '发行验收' }).Count -eq 1) $manual.slots
    $savedSlots = Slot-Hashes
    Assert-Check 'manual slot 2 exists on disk' ($savedSlots.ContainsKey('slot2.json')) $savedSlots
    $advancedAgain = Invoke-Api '/api/advance' 'POST' @{days=3; autoStop=$true}
    Assert-Check 'progress continues beyond the manual checkpoint' ([int]$advancedAgain.state.clock.days -eq $startDays+10) $advancedAgain.report
    $before = Invoke-Api '/api/export'
    $beforeGame = $before.game | ConvertFrom-Json
    Assert-Check 'latest progress retains an older manual checkpoint' ([int]$beforeGame.checkpoint.days -eq $startDays+7 -and [int]$beforeGame.clock.days -eq $startDays+10) $beforeGame.checkpoint.days

    Request-Stop $launcher
    $launcher = $null
    $sessionPath = Join-Path $runtime 'latest-session.json'
    $session = [IO.File]::ReadAllText($sessionPath,$utf8) | ConvertFrom-Json
    Assert-Check 'normal shutdown saves the exact latest game and legacy' ($session.game -ceq $before.game -and $session.legacy -ceq $before.legacy) $session.name

    $launcher = New-Launcher
    $readyAgain = Wait-Ready $launcher
    $serverId = [int]$readyAgain.serverPid
    Assert-Check 'second launch resumes the last session' ([bool]$readyAgain.restored) $readyAgain
    $after = Invoke-Api '/api/export'
    Assert-Check 'automatic resume preserves exact game and legacy' ($after.game -ceq $before.game -and $after.legacy -ceq $before.legacy) $after.name
    $resumedState = Invoke-Api '/api/state'
    Assert-Check 'resumed state has the latest character and date' ($resumedState.hasGame -and $resumedState.state.player.name -eq '发行验收' -and [int]$resumedState.state.clock.days -eq $startDays+10) $resumedState.state.clock.days
    $slotHashesAfter = Slot-Hashes
    $slotsUnchanged = $savedSlots.Count -eq $slotHashesAfter.Count
    foreach ($name in $savedSlots.Keys) {
        if (-not $slotHashesAfter.ContainsKey($name) -or $slotHashesAfter[$name] -ne $savedSlots[$name]) { $slotsUnchanged = $false }
    }
    Assert-Check 'automatic resume leaves manual and death checkpoint slots unchanged' $slotsUnchanged $slotHashesAfter
    Request-Stop $launcher
    $launcher = $null

    $sessionHash = (Get-FileHash -LiteralPath $sessionPath -Algorithm SHA256).Hash
    $image = $manifest | Where-Object { $_.path -like 'runtime/web/assets/portraits/*.webp' } | Select-Object -First 1
    if ($null -eq $image) { throw 'No packaged portrait is available for recovery verification.' }
    $imagePath = [IO.Path]::GetFullPath((Join-Path $dataRoot ([string]$image.path)))
    Assert-Check 'repair removes only one file inside isolated runtime' ($imagePath.StartsWith($runtime+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) $image.path
    Remove-Item -LiteralPath $imagePath
    $launcher = New-Launcher $true
    Assert-Check 'missing asset repair completes' (Wait-Exited $launcher 60) $launcher.Id
    Assert-Check 'missing asset repair exits zero' ($launcher.ExitCode -eq 0) $launcher.ExitCode
    $launcher = $null
    Assert-Check 'missing portrait is restored exactly' ((Get-FileHash -LiteralPath $imagePath -Algorithm SHA256).Hash -eq $image.sha256) $image.path
    Assert-Check 'asset repair preserves last session' ((Get-FileHash -LiteralPath $sessionPath -Algorithm SHA256).Hash -eq $sessionHash) $sessionHash
    $repairSlotHashes = Slot-Hashes
    $repairPreservedSlots = $savedSlots.Count -eq $repairSlotHashes.Count
    foreach ($name in $savedSlots.Keys) {
        if (-not $repairSlotHashes.ContainsKey($name) -or $repairSlotHashes[$name] -ne $savedSlots[$name]) { $repairPreservedSlots = $false }
    }
    Assert-Check 'asset repair preserves manual and automatic save slots' $repairPreservedSlots $repairSlotHashes
    $completed = $true
} catch {
    $errorText = $_.Exception.Message
    throw
} finally {
    # This object is the launcher created by this script. Its Job Object only owns
    # its own game process; there is no process-name kill and no access to 8107.
    if ($null -ne $launcher) {
        try {
            $launcher.Refresh()
            if (-not $launcher.HasExited) {
                if (Test-Path -LiteralPath $runtime -PathType Container) { [IO.File]::WriteAllText((Join-Path $runtime 'launcher-stop'),'stop',$utf8) }
                if (-not (Wait-Exited $launcher 10)) { $launcher.Kill(); $launcher.WaitForExit() }
            }
        } catch { }
    }
    $verification = [ordered]@{ok=$completed; startedAt=$startedAt; finishedAt=(Get-Date).ToUniversalTime().ToString('o'); exe=$exe; dataRoot=$dataRoot; port=$port; checks=@($checks.ToArray()); error=$errorText}
    [IO.File]::WriteAllText((Join-Path $buildRoot 'verification.json'),($verification | ConvertTo-Json -Depth 100),$utf8)
}
Write-Output ($verification | ConvertTo-Json -Depth 100)
