@echo off
setlocal
rem ============================================================================
rem  Smart Blood Donor Finder - share a temporary public link
rem  Save this file next to start.bat (project root), then double-click it.
rem  It runs start.bat (build + server), then opens a Cloudflare quick tunnel.
rem ============================================================================

cd /d "%~dp0"

if not exist "start.bat" (
    echo [ERROR] start.bat not found. share.bat must be in the same folder as start.bat.
    goto :fail
)

where cloudflared >nul 2>&1
if errorlevel 1 (
    echo [ERROR] cloudflared is not installed.
    echo.
    echo   1. Open Command Prompt or PowerShell and run:
    echo        winget install --id Cloudflare.cloudflared
    echo   2. Close that window, then double-click share.bat again.
    goto :fail
)

echo Step 1 of 2: building and starting the server...
call start.bat
if errorlevel 1 (
    echo [ERROR] The server did not start. Fix the problem above first.
    goto :fail
)

echo.
echo Step 2 of 2: opening the public tunnel in a new window...
start "Public tunnel - copy the trycloudflare.com link" cmd /k cloudflared tunnel --url http://localhost:8080

echo.
echo ============================================================================
echo  In the new "Public tunnel" window, find the line with
echo      https://something-random.trycloudflare.com
echo  and copy that link. Anyone with it can use your app.
echo.
echo  The link works only while BOTH windows stay open and this PC stays awake.
echo  Close the two windows (server and tunnel) to stop sharing.
echo ============================================================================
pause
exit /b 0

:fail
echo.
pause
exit /b 1
