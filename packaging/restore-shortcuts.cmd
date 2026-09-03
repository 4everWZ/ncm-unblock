@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" -RestoreShortcuts
if errorlevel 1 pause
exit /b %errorlevel%
