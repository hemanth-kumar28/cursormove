@echo off
setlocal enabledelayedexpansion

set GCC=C:\MinGW\bin\g++.exe
set WINDRES=C:\MinGW\bin\windres.exe

set CFLAGS=-std=c++14 -Wall -Wextra
set CFLAGS=%CFLAGS% -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN
set CFLAGS=%CFLAGS% -DWINVER=0x0601 -D_WIN32_WINNT=0x0601
set CFLAGS=%CFLAGS% -I./src -I./lib

set LDFLAGS=-static -mwindows
set LDFLAGS=%LDFLAGS% -lcomctl32 -lole32 -lshell32 -lgdi32 -luser32 -lwtsapi32

if not exist build mkdir build

if "%1"=="debug" (
    echo === DEBUG BUILD ===
    set CFLAGS=!CFLAGS! -g -O0 -DDEBUG
    set OUT=CursorMove_debug.exe
) else (
    echo === RELEASE BUILD ===
    set CFLAGS=!CFLAGS! -O2 -DNDEBUG -s
    set OUT=CursorMove.exe
)

echo.
echo [1/10] Compiling resources...
%WINDRES% res/resource.rc -o build/resource.o
if %errorlevel% neq 0 (
    echo [FAIL] Resource compilation failed
    exit /b 1
)

echo [2/10] Compiling util.cpp...
%GCC% %CFLAGS% -c src/util.cpp -o build/util.o
if %errorlevel% neq 0 (
    echo [FAIL] util.cpp failed
    exit /b 1
)

echo [3/10] Compiling keys.cpp...
%GCC% %CFLAGS% -c src/keys.cpp -o build/keys.o
if %errorlevel% neq 0 (
    echo [FAIL] keys.cpp failed
    exit /b 1
)

echo [4/10] Compiling tray.cpp...
%GCC% %CFLAGS% -c src/tray.cpp -o build/tray.o
if %errorlevel% neq 0 (
    echo [FAIL] tray.cpp failed
    exit /b 1
)

echo [5/10] Compiling hook.cpp...
%GCC% %CFLAGS% -c src/hook.cpp -o build/hook.o
if %errorlevel% neq 0 (
    echo [FAIL] hook.cpp failed
    exit /b 1
)

echo [6/10] Compiling motion.cpp...
%GCC% %CFLAGS% -c src/motion.cpp -o build/motion.o
if %errorlevel% neq 0 (
    echo [FAIL] motion.cpp failed
    exit /b 1
)

echo [7/10] Compiling config.cpp...
%GCC% %CFLAGS% -c src/config.cpp -o build/config.o
if %errorlevel% neq 0 (
    echo [FAIL] config.cpp failed
    exit /b 1
)

echo [8/10] Compiling hotkey.cpp...
%GCC% %CFLAGS% -c src/hotkey.cpp -o build/hotkey.o
if %errorlevel% neq 0 (
    echo [FAIL] hotkey.cpp failed
    exit /b 1
)

echo [9/10] Compiling ui.cpp...
%GCC% %CFLAGS% -c src/ui.cpp -o build/ui.o
if %errorlevel% neq 0 (
    echo [FAIL] ui.cpp failed
    exit /b 1
)

echo [10/10] Compiling main.cpp...
%GCC% %CFLAGS% -c src/main.cpp -o build/main.o
if %errorlevel% neq 0 (
    echo [FAIL] main.cpp failed
    exit /b 1
)

echo.
echo Linking %OUT%...
%GCC% build/main.o build/util.o build/keys.o build/tray.o build/hook.o build/motion.o build/config.o build/hotkey.o build/ui.o build/resource.o %LDFLAGS% -o %OUT%
if %errorlevel% neq 0 (
    echo [FAIL] Linking failed
    exit /b 1
)

echo.
echo [OK] Build successful: %OUT%
for %%F in (%OUT%) do echo     Size: %%~zF bytes
echo.
