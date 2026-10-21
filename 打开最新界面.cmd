@echo off
setlocal
call "%~dp0start-full-server.cmd"
exit /b %ERRORLEVEL%
