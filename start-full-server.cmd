@echo off
setlocal
cd /d "%~dp0"
%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\start-full-server.ps1" %*
set "taskExit=%errorlevel%"
if not "%taskExit%"=="0" if "%~1"=="" pause
exit /b %taskExit%
