@echo off
rem ============================================================
rem  Command-line build of the whole solution (x64).
rem  Usage:  scripts\build.cmd [Release^|Debug]
rem  Requires: Visual Studio 2022 (+ WDK for the driver project).
rem ============================================================
setlocal
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release

set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% (
    echo vswhere.exe not found - is Visual Studio 2022 installed?
    exit /b 1
)

set MSBUILD=
for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set MSBUILD=%%i
if not defined MSBUILD (
    echo MSBuild not found.
    exit /b 1
)

echo Building %CONFIG% x64 with "%MSBUILD%" ...
"%MSBUILD%" "%~dp0..\NtObjExplorer.sln" /p:Configuration=%CONFIG% /p:Platform=x64 /m /v:m
endlocal

rem [v1.1 resync 2026-10-23] current build-fix version
