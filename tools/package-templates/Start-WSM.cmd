@echo off
setlocal DisableDelayedExpansion

set "ERRORLEVEL="
echo Warhammer Survivors Multiplayer - in-game connection UI
echo This is experimental: connecting does NOT provide shared-world gameplay.
echo Your original installation will not be modified, but normal saves and Steam Cloud
 echo are NOT isolated. Back up your saves before continuing.
echo.
"%SystemRoot%\System32\choice.exe" /c YN /n /d N /t 60 /m "Continue with non-isolated saves and Steam Cloud? [y/N] "
if errorlevel 2 exit /b 1
if not errorlevel 1 exit /b 1
rem The launcher reads game paths directly; never interpolate them into shell source.
"%~dp0launcher\Wsm.Launcher.exe" play-installed
set "WSM_EXIT=%ERRORLEVEL%"
echo.
echo Launcher exited with code %WSM_EXIT%.
pause
exit /b %WSM_EXIT%
