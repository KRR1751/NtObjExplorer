@echo off
rem Uninstalls the NtObjDrv kernel service. Run as Administrator.
net session >nul 2>&1
if errorlevel 1 (
    echo This script must be run as Administrator.
    exit /b 1
)
echo Stopping NtObjDrv...
sc stop NtObjDrv
timeout /t 2 /nobreak >nul
sc delete NtObjDrv
echo Done.

rem [v1.1 resync 2026-10-23] current build-fix version
