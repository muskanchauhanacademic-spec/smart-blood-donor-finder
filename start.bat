@echo off
setlocal enabledelayedexpansion
rem ============================================================================
rem  Smart Blood Donor Finder - one-click start
rem  Save this file in the PROJECT ROOT (the folder that contains "backend" and
rem  "frontend"), then double-click it.
rem  Use "start.bat empty" to start with no sample records.
rem ============================================================================

rem Always work from the folder this file is in (the server needs ./frontend).
cd /d "%~dp0"

set "DEMO=--demo"
if /i "%~1"=="empty" set "DEMO="

rem MSYS2 UCRT64 compiler location. Change this line if MSYS2 is installed elsewhere.
set "MSYS_BIN=C:\msys64\ucrt64\bin"
if exist "%MSYS_BIN%\gcc.exe" set "PATH=%MSYS_BIN%;%PATH%"

where gcc >nul 2>&1
if errorlevel 1 (
    echo [ERROR] gcc was not found.
    echo         Install MSYS2 UCRT64 or edit the MSYS_BIN line in start.bat.
    goto :fail
)

if not exist "backend\" (
    echo [ERROR] The "backend" folder is missing. start.bat must be in the project root.
    goto :fail
)
if not exist "frontend\index.html" (
    echo [ERROR] frontend\index.html is missing.
    goto :fail
)

rem A running server locks sbdf.exe and would make the rebuild fail, so stop it first.
taskkill /f /im sbdf.exe >nul 2>&1

rem Build the list of source files (avoids relying on gcc wildcard expansion).
set "SRC="
for %%f in (backend\*.c) do set "SRC=!SRC! backend\%%~nxf"

echo Building...
gcc -std=c11 -Wall -Wextra -pedantic -g -o sbdf.exe !SRC! -lws2_32
if errorlevel 1 (
    echo.
    echo [ERROR] Build failed. Copy the messages above and send them to your orchestrator.
    goto :fail
)
echo Build OK.

echo Starting the server in a new window (close that window to stop the server)...
start "Smart Blood Donor Finder - server" cmd /k sbdf.exe %DEMO%

rem Give the server a moment to start listening, then open the page.
timeout /t 2 /nobreak >nul
start "" "http://localhost:8080"

echo.
echo Browser opened at http://localhost:8080
echo If the page says it cannot reach the backend, look at the server window for errors.
timeout /t 5 >nul
exit /b 0

:fail
echo.
pause
exit /b 1