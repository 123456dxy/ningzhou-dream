param([Parameter(Mandatory = $true)][string]$Path)
$ErrorActionPreference = 'Stop'
# Match CMake's explicit UTF-8 decoder; even an 8.3 path can retain CJK names.
# Emit UTF-8 without a BOM so the first character remains part of the drive path.
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
# Existing Windows fixtures pass native paths across a UTF-8 API boundary.
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class NingzhouTestPath {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern uint GetShortPathName(string path, StringBuilder result, uint size);
}
'@
$taskBuffer = New-Object System.Text.StringBuilder 32768
$taskLength = [NingzhouTestPath]::GetShortPathName($Path, $taskBuffer, 32768)
if ($taskLength -gt 0 -and $taskLength -lt 32768) {
    Write-Output $taskBuffer.ToString()
} else {
    Write-Output $Path
}
