@echo off
cd /d "%~dp0"
Lite2Updater.exe
set "lite2_result=%ERRORLEVEL%"
pause
exit /b %lite2_result%
