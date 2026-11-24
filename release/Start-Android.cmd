@echo off
setlocal
cd /d "%~dp0"
if exist "platform-tools\adb.exe" set "PATH=%CD%\platform-tools;%PATH%"
where adb.exe >nul 2>&1
if errorlevel 1 (
  echo Extract Google's Android SDK Platform-Tools here so platform-tools\adb.exe exists.
  pause
  exit /b 1
)
adb -d install -r Lite2Control-0.3.15.apk
if errorlevel 1 (
  echo Connect one tablet via USB and accept the debugging prompt.
  pause
  exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\start-y700.ps1" -Binary ".\lite2-pointer-bridge"
set "lite2_result=%ERRORLEVEL%"
pause
exit /b %lite2_result%
