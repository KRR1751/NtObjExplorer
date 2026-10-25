@echo off
rem ============================================================
rem  NtObjDrv installer - creates the kernel service with
rem  START = 3 (SERVICE_DEMAND_START) and starts it on demand.
rem  Run as Administrator in the test VM.
rem ============================================================
net session >nul 2>&1
if errorlevel 1 (
    echo This script must be run as Administrator.
    exit /b 1
)

set SYS=
if exist "%~dp0..\x64\Release\NtObjDrv.sys" set SYS=%~dp0..\x64\Release\NtObjDrv.sys
if exist "%~dp0..\x64\Debug\NtObjDrv.sys"   set SYS=%~dp0..\x64\Debug\NtObjDrv.sys
if not defined SYS (
    echo NtObjDrv.sys not found - build the driver first, e.g. scripts\build.cmd Release
    exit /b 1
)

rem --- clean up any previous instance -------------------------
sc stop NtObjDrv >nul 2>&1
sc delete NtObjDrv >nul 2>&1

rem --- create with START = 3 (demand) --------------------------
sc create NtObjDrv type= kernel start= demand binPath= "%SYS%" displayName= "NT Object Namespace Driver"
if errorlevel 1 (
    echo sc create failed.
    exit /b 1
)
sc description NtObjDrv "Provides privileged NT object namespace operations for ntobj.exe"

echo Starting driver...
sc start NtObjDrv
sc query NtObjDrv

echo.
echo Done. Service NtObjDrv uses START = 3 (demand start):
echo   - it does NOT load at boot,
echo   - "sc start NtObjDrv" loads it, "sc stop NtObjDrv" unloads it.
echo Registry value if you want to check: HKLM\SYSTEM\CurrentControlSet\Services\NtObjDrv : Start = 3
echo (Do NOT set Start to 0 or 1 for this test driver - there is no reason to
echo  load it at boot time, and a failing boot-start driver complicates the VM.)

rem [v1.1 resync 2026-10-23] current build-fix version
