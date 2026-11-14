[CmdletBinding()]
param([string]$OutputDirectory = '')

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell 7 on Windows.' }
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = Join-Path $root 'distribution/win-x64' }
$destination = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $destination) { throw 'Output already exists. Choose a new empty destination; no existing files are overwritten.' }
function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE." }
}
Push-Location $root
try {
    Invoke-Checked dotnet @('publish', 'src/Wsm.App', '-c', 'Release', '-r', 'win-x64', '--self-contained', 'true', '-p:DebugType=None', '-p:DebugSymbols=false', '-o', "$destination/lobby")
    Invoke-Checked dotnet @('publish', 'src/Wsm.Launcher', '-c', 'Release', '-r', 'win-x64', '--self-contained', 'true', '-p:DebugType=None', '-p:DebugSymbols=false', '-o', "$destination/launcher")
    Invoke-Checked cmake @('-S', 'native', '-B', 'artifacts/native-build', '-A', 'x64')
    Invoke-Checked cmake @('--build', 'artifacts/native-build', '--config', 'Release', '--target', 'wsm_bridge')
    New-Item -ItemType Directory -Path "$destination/bridge" | Out-Null
    Copy-Item -LiteralPath "$root/artifacts/native-build/Release/Wsm.Bridge.dll" -Destination "$destination/bridge/Wsm.Bridge.dll"
    # Preserve license notices from the exact runtime packs resolved by this publish.
    $assets = Get-Content -LiteralPath "$root/src/Wsm.App/obj/project.assets.json" -Raw | ConvertFrom-Json -AsHashtable
    $packageRoot = @($assets.packageFolders.Keys)[0]
    foreach ($framework in $assets.project.frameworks.Values) {
        foreach ($dependency in $framework.downloadDependencies) {
            if ($dependency.name -notmatch '^Microsoft\.(NETCore|AspNetCore|WindowsDesktop)\.App\.Runtime\.win-x64$') { continue }
            if ($dependency.version -notmatch '^\[([^,]+),\s*([^\]]+)\]$' -or $Matches[1] -ne $Matches[2]) { throw 'Runtime pack version is not exact.' }
            $version = $Matches[1]
            $package = Join-Path $packageRoot ($dependency.name.ToLowerInvariant() + '/' + $version)
            $legal = @(Get-ChildItem -LiteralPath $package -File | Where-Object Name -Match '^(LICENSE|THIRD-PARTY-NOTICES)(\..*)?$')
            if (-not ($legal | Where-Object Name -Match '^LICENSE')) { throw "Missing runtime license: $($dependency.name)" }
            $legalTarget = Join-Path $destination ("licenses/" + $dependency.name + '/' + $version)
            New-Item -ItemType Directory -Path $legalTarget -Force | Out-Null
            foreach ($file in $legal) { Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $legalTarget $file.Name) }
        }
    }
    $files = @(Get-ChildItem -LiteralPath $destination -Recurse -File | Sort-Object FullName | ForEach-Object {
        if ($_.Name -in @('GameAssembly.dll', 'UnityPlayer.dll', 'WarhammerSurvivors.exe', 'global-metadata.dat')) {
            throw "Proprietary installation file in publish output: $($_.Name)"
        }
        [ordered]@{
            path = [IO.Path]::GetRelativePath($destination, $_.FullName).Replace('\', '/')
            bytes = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    })
    [ordered]@{
        target = 'win-x64'
        selfContainedDotNet = $true
        playableMultiplayer = $false
        builtUtc = [DateTime]::UtcNow.ToString('O')
        files = $files
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$destination/package-manifest.json" -Encoding utf8NoBOM
    Write-Host "Packaged development tools: $destination"
    Write-Host 'This is a working lobby and diagnostic launcher, not a playable multiplayer release.'
} finally { Pop-Location }
