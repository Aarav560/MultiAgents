@echo off
rem build.bat - build orbit on Windows without make.
rem   build.bat         builds build\orbit.exe
rem   build.bat test    also builds and runs every unit test
rem Uses gcc or clang (MinGW-w64, MSYS2, WinLibs, LLVM), falling back to MSVC's cl.
setlocal enabledelayedexpansion
cd /d "%~dp0"
if not exist build mkdir build

set CC=
where gcc >nul 2>nul && set CC=gcc
if not defined CC where clang >nul 2>nul && set CC=clang
if not defined CC where cl >nul 2>nul && set CC=cl
if not defined CC (
    echo No C compiler found. Install one of:
    echo   winget install BrechtSanders.WinLibs.POSIX.UCRT   ^(gcc^)
    echo   winget install LLVM.LLVM                         ^(clang^)
    echo or run this from a "Developer Command Prompt for VS".
    exit /b 1
)

set SRCS=
for %%f in (src\*.c) do if /i not "%%~nf"=="main" set SRCS=!SRCS! %%f

if "%CC%"=="cl" (
    set BUILD=cl /nologo /std:c11 /O2 /W3 /Iinclude /D_CRT_SECURE_NO_WARNINGS /Fobuild\
    set OUT=/Fe:
) else (
    set BUILD=%CC% -std=c11 -O2 -Wall -Wextra -Iinclude
    set OUT=-o
)

echo Building build\orbit.exe with %CC% ...
%BUILD% src\main.c %SRCS% %OUT%build\orbit.exe || exit /b 1
echo OK: build\orbit.exe

if /i not "%1"=="test" goto done
set PASS=0
for %%t in (tests\test_*.c) do (
    %BUILD% %%t %SRCS% %OUT%build\%%~nt.exe >nul || (echo FAIL compile %%t & exit /b 1)
    build\%%~nt.exe >build\%%~nt.log 2>&1 || (type build\%%~nt.log & echo FAIL %%t & exit /b 1)
    echo PASS %%t
    set /a PASS+=1
)
echo tests: !PASS! passed, 0 failed

:done
echo.
echo Try:  build\orbit.exe --scenario solar --ascii
endlocal
