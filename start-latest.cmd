@echo off
setlocal
cd /d "%~dp0"
where pwsh.exe >nul 2>&1
if errorlevel 1 (
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Web
) else (
    pwsh.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Web
)
set "NZ_EXIT=%ERRORLEVEL%"
if not "%NZ_EXIT%"=="0" pause
exit /b %NZ_EXIT%
