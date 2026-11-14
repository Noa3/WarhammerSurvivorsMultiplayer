@echo off
setlocal DisableDelayedExpansion
set "WSM_ROOT=%~dp0"
set "WSM_GAME="
echo Warhammer Survivors Multiplayer - in-game connection UI
echo This is experimental: connecting does NOT provide shared-world gameplay.
echo Your original installation will not be modified, but normal saves and Steam Cloud
 echo are NOT isolated. Back up your saves before continuing.
echo.
set /p "WSM_GAME=Paste your installed Warhammer Survivors game folder (blank to cancel): "
if not defined WSM_GAME exit /b 1
echo.
choice /c YN /n /d N /t 60 /m "Continue with non-isolated saves and Steam Cloud? [y/N] "
if errorlevel 2 exit /b 1
rem Pass user input via environment variables, never as cmd or PowerShell source.
powershell.exe -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; try { $game=$env:WSM_GAME.Trim().Trim([char]34); if (!$game -or $game.Contains([char]34)) { throw 'Invalid game directory.' }; $game=[IO.Path]::GetFullPath($game); if (!$env:LOCALAPPDATA) { throw 'LOCALAPPDATA is not set.' }; $base=Join-Path $env:LOCALAPPDATA 'WarhammerSurvivorsMultiplayer'; $launcher=Join-Path $env:WSM_ROOT 'launcher\Wsm.Launcher.exe'; $bridge=Join-Path $env:WSM_ROOT 'bridge\Wsm.Bridge.dll'; $cache=Join-Path $base 'cache'; $logs=Join-Path $base 'logs'; $i=New-Object Diagnostics.ProcessStartInfo; $i.FileName=$launcher; $i.UseShellExecute=$false; $q=[char]34; $arguments=@($game,$cache,$bridge,$logs) | ForEach-Object { $q + ($_ -replace '(\\*)$', '$1$1') + $q }; $i.Arguments='play ' + ($arguments -join ' '); $p=[Diagnostics.Process]::Start($i); $p.WaitForExit(); exit $p.ExitCode } catch { [Console]::Error.WriteLine('WSM: ' + $_.Exception.Message); exit 1 }"
set "WSM_EXIT=%ERRORLEVEL%"
echo.
echo Launcher exited with code %WSM_EXIT%.
pause
exit /b %WSM_EXIT%
