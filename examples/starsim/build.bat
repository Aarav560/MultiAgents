@echo off
rem build.bat - builds build\starsim.exe on Windows with gcc, clang or cl (MSVC).
rem   build.bat        build the simulator
rem   build.bat test   build it, then build and run every tests\test_*.c
rem   build.bat run    build it and start it
setlocal enabledelayedexpansion
cd /d "%~dp0"

set LIBSRC=src\world.c src\physics.c src\quadtree.c src\scenario.c src\render.c src\font.c src\camera.c src\trails.c

where gcc >nul 2>nul
if not errorlevel 1 (
    set CC=gcc
    goto :gnu
)
where clang >nul 2>nul
if not errorlevel 1 (
    set CC=clang
    goto :gnu
)
where cl >nul 2>nul
if not errorlevel 1 goto :msvc

echo No C compiler found (looked for gcc, clang and cl).
echo Install one of these, then run build.bat again:
echo   winget install BrechtSanders.WinLibs.POSIX.UCRT     (gcc, MinGW-w64)
echo   winget install LLVM.LLVM                            (clang; also needs the Windows SDK)
echo   Visual Studio Build Tools, then use the "x64 Native Tools Command Prompt" (cl)
exit /b 1

:gnu
set CFLAGS=-std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude
if not exist build mkdir build
echo Building build\starsim.exe with %CC%
rem The platform files select themselves with #if, so every src\*.c can be compiled.
%CC% %CFLAGS% src\*.c -o build\starsim.exe -luser32 -lgdi32
if errorlevel 1 goto :failed
if /i "%~1"=="run" (
    build\starsim.exe
    exit /b 0
)
if /i not "%~1"=="test" goto :ok
set FAILED=0
for %%F in (tests\test_*.c) do (
    set T=%%~nF
    set M=!T:test_=!
    if /i "!T!"=="test_app" (
        %CC% %CFLAGS% -DSTARSIM_HEADLESS %%F src\app.c src\platform_null.c %LIBSRC% -o build\!T!.exe
    ) else (
        %CC% %CFLAGS% %%F src\!M!.c src\world.c -o build\!T!.exe
    )
    if errorlevel 1 (
        set FAILED=1
    ) else (
        echo == !T!
        build\!T!.exe
        if errorlevel 1 set FAILED=1
    )
)
if "!FAILED!"=="1" goto :failed
goto :ok

:msvc
set CFLAGS=/nologo /std:c11 /O2 /W3 /Iinclude /D_CRT_SECURE_NO_WARNINGS
if not exist build mkdir build
echo Building build\starsim.exe with cl
cl %CFLAGS% src\*.c /Fobuild\ /Fe:build\starsim.exe user32.lib gdi32.lib
if errorlevel 1 goto :failed
if /i "%~1"=="run" (
    build\starsim.exe
    exit /b 0
)
if /i not "%~1"=="test" goto :ok
set FAILED=0
for %%F in (tests\test_*.c) do (
    set T=%%~nF
    set M=!T:test_=!
    if not exist build\!T! mkdir build\!T!
    if /i "!T!"=="test_app" (
        cl %CFLAGS% /DSTARSIM_HEADLESS %%F src\app.c src\platform_null.c %LIBSRC% /Fobuild\!T!\ /Fe:build\!T!.exe
    ) else (
        cl %CFLAGS% %%F src\!M!.c src\world.c /Fobuild\!T!\ /Fe:build\!T!.exe
    )
    if errorlevel 1 (
        set FAILED=1
    ) else (
        echo == !T!
        build\!T!.exe
        if errorlevel 1 set FAILED=1
    )
)
if "!FAILED!"=="1" goto :failed
goto :ok

:failed
echo BUILD OR TESTS FAILED
exit /b 1

:ok
echo OK
exit /b 0
