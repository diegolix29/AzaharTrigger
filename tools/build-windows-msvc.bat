@echo off
rem AzalonDS - configure and build with MSVC (Azahar) + Visual Studio's clang (DS core).
rem Usage: tools\build-windows-msvc.bat [x64-release|x64-debug] [extra cmake -D options...]
rem Needs Visual Studio with "Desktop development with C++" and "C++ Clang tools for Windows".
setlocal

rem Resolve the repo root before any shift (shift also moves %0).
set "ROOT=%~dp0.."

set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=x64-release"
set "EXTRA="
:collect
shift
if "%~1"=="" goto collected
set "EXTRA=%EXTRA% %1"
goto collect
:collected

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIR="
for /f "usebackq tokens=*" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if "%VSDIR%"=="" (
    echo Visual Studio with the C++ workload was not found.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

rem vcvars puts Visual Studio's CMake and Ninja on PATH.
cd /d "%ROOT%" || exit /b 1
cmake --preset %PRESET% -DENABLE_FFMPEG=OFF %EXTRA% || exit /b 1
cmake --build out\build\%PRESET% || exit /b 1
echo.
echo Built: out\build\%PRESET%\bin\
