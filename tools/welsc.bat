@echo off
REM welsc.bat - WELS-C build script (Windows)

setlocal

if "%~1"=="" (
    echo Usage: welsc ^<file.wec^> [-o output]
    exit /b 1
)

set "WELS_ROOT=%~dp0.."
set "COMPILER=%WELS_ROOT%\compiler\wescc.exe"
set "RUNTIME=%WELS_ROOT%\runtime\wels_rt_win.o"
set "STDLIB=%WELS_ROOT%\stdlib"

set "IN=%~1"
shift

set "OUT="
:parse
if "%~1"=="" goto run
if "%~1"=="-o" (
    set "OUT=%~2"
    shift
    shift
    goto parse
)
echo Unknown argument: %~1
exit /b 1

:run
if "%OUT%"=="" (
    for %%F in ("%IN%") do set "OUT=%%~nF"
)

set "TMP_S=%TEMP%\welsc-%RANDOM%.s"

echo [1/2] wescc %IN% -^> asm
"%COMPILER%" "%IN%" -o "%TMP_S%" -target windows -runtime wels -I "%STDLIB%"
if errorlevel 1 exit /b 1

echo [2/2] gcc asm -^> %OUT%
gcc -nostdlib "%TMP_S%" "%RUNTIME%" -lkernel32 -o "%OUT%"
if errorlevel 1 exit /b 1

del "%TMP_S%" 2>nul
echo Done: %OUT%
endlocal
