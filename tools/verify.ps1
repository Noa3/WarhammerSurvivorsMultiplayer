[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $IsWindows -or -not [Environment]::Is64BitProcess) {
    throw 'Run verification with 64-bit PowerShell 7 on Windows.'
}
$root = Split-Path -Parent $PSScriptRoot
function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE." }
}
Push-Location $root
try {
    Invoke-Checked cmake @('-S', 'native', '-B', 'artifacts/native-build', '-A', 'x64')
    Invoke-Checked cmake @('--build', 'artifacts/native-build', '--config', 'Release')
    Invoke-Checked ctest @('--test-dir', 'artifacts/native-build', '-C', 'Release', '--output-on-failure')
    Invoke-Checked dotnet @('build', 'WarhammerSurvivorsMultiplayer.slnx', '-c', 'Release')
    Invoke-Checked dotnet @('run', '--project', 'tests/Wsm.Core.Tests', '-c', 'Release', '--no-build')
    Invoke-Checked dotnet @('run', '--project', 'tests/Wsm.Lobby.Tests', '-c', 'Release', '--no-build')
    Invoke-Checked dotnet @('run', '--project', 'tests/Wsm.App.Smoke', '-c', 'Release', '--no-build')
    $windowFixture = "$root/tests/Wsm.OwnedWindowFixture/bin/Release/net10.0-windows/Wsm.OwnedWindowFixture.exe"
    Invoke-Checked dotnet @('run', '--project', 'tests/Wsm.App.Smoke', '-c', 'Release', '--no-build', '--', '--owned-stop-fixture', $windowFixture)
    if (-not [string]::IsNullOrWhiteSpace($env:WSM_UNITY_FIXTURE_INSTALLATION)) {
        $contextWork = "$root/artifacts/context-fixture-$([Guid]::NewGuid().ToString('N'))"
        Invoke-Checked dotnet @('run', '--project', 'tests/Wsm.App.Smoke', '-c', 'Release', '--no-build', '--', '--context-fixture', $env:WSM_UNITY_FIXTURE_INSTALLATION, $contextWork)
    }
    Invoke-Checked python @('-m', 'unittest', 'discover', '-s', 'tests/Packaging', '-v')
    Invoke-Checked dotnet @('run', '--project', 'tests/Wsm.Launcher.Tests', '-c', 'Release', '--no-build', '--', "$root/artifacts/native-build/Release")
    Write-Host 'All automated development checks passed. Live game and multiplayer gameplay are separate acceptance gates.'
} finally {
    Pop-Location
}
