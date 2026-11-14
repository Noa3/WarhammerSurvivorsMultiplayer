[CmdletBinding()]
param([string]$OutputDirectory = '', [string]$BuildDirectory = '')

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell 7 on Windows.' }
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = Join-Path $root 'distribution/win-x64' }
$destination = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $destination) { throw 'Output already exists. Choose a new empty destination; no existing files are overwritten.' }
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) { $BuildDirectory = Join-Path $root ('.local/package-ingame-v1-build-' + [Guid]::NewGuid().ToString('N')) }
$build = [IO.Path]::GetFullPath($BuildDirectory)
if (Test-Path -LiteralPath $build) { throw 'Build directory already exists. Choose a new private build directory.' }
if ($build.Equals($destination, [StringComparison]::OrdinalIgnoreCase) -or $build.StartsWith($destination + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or $destination.StartsWith($build + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Build and output directories must not overlap.' }
function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE." }
}
New-Item -ItemType Directory -Path $build -ErrorAction Stop | Out-Null
$artifacts = Join-Path $build 'dotnet'
$native = Join-Path $build 'native'
$stage = Join-Path $build 'publish'
Push-Location $root
try {
    Invoke-Checked dotnet @('publish', 'src/Wsm.App', '-c', 'Release', '-r', 'win-x64', '--self-contained', 'true', '--artifacts-path', $artifacts, '-p:DebugType=None', '-p:DebugSymbols=false', '-o', "$stage/lobby")
    Invoke-Checked dotnet @('publish', 'src/Wsm.Launcher', '-c', 'Release', '-r', 'win-x64', '--self-contained', 'true', '--artifacts-path', $artifacts, '-p:DebugType=None', '-p:DebugSymbols=false', '-o', "$stage/launcher")
    Invoke-Checked dotnet @('publish', 'src/Wsm.InGame.Runtime', '-c', 'Release', '-r', 'win-x64', '--self-contained', 'true', '--artifacts-path', $artifacts, '-p:DebugType=None', '-p:DebugSymbols=false', '-o', "$stage/runtime")
    Invoke-Checked cmake @('-S', 'native', '-B', $native, '-A', 'x64', '-DBUILD_TESTING=OFF')
    Invoke-Checked cmake @('--build', $native, '--config', 'Release', '--target', 'wsm_bridge')
    foreach ($required in @("$stage/lobby/Wsm.App.exe", "$stage/launcher/Wsm.Launcher.exe", "$stage/runtime/Wsm.InGame.Runtime.dll", "$native/Release/Wsm.Bridge.dll", "$PSScriptRoot/package-templates/Start-WSM.cmd")) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf) -or (Get-Item -LiteralPath $required).Length -eq 0) { throw "Missing completed publish artifact: $required" }
    }
    New-Item -ItemType Directory -Path $destination -ErrorAction Stop | Out-Null
    Copy-Item -LiteralPath "$stage/lobby" -Destination "$destination/lobby" -Recurse
    Copy-Item -LiteralPath "$stage/launcher" -Destination "$destination/launcher" -Recurse
    New-Item -ItemType Directory -Path "$destination/bridge" | Out-Null
    Copy-Item -LiteralPath "$native/Release/Wsm.Bridge.dll" -Destination "$destination/bridge/Wsm.Bridge.dll"
    Copy-Item -LiteralPath "$stage/runtime/Wsm.InGame.Runtime.dll" -Destination "$destination/bridge/Wsm.InGame.Runtime.dll"
    Copy-Item -LiteralPath "$PSScriptRoot/package-templates/Start-WSM.cmd" -Destination "$destination/Start-WSM.cmd"
    # Preserve license notices from the exact runtime packs resolved by this publish.
    foreach ($project in @('Wsm.App', 'Wsm.Launcher', 'Wsm.InGame.Runtime')) {
    $assets = Get-Content -LiteralPath "$artifacts/obj/$project/project.assets.json" -Raw | ConvertFrom-Json -AsHashtable
    $packageRoot = @($assets.packageFolders.Keys)[0]
    foreach ($framework in $assets.project.frameworks.Values) {
        foreach ($dependency in $framework.downloadDependencies) {
            if ($dependency.name -notmatch '^Microsoft\.(NETCore|AspNetCore|WindowsDesktop)\.App\.Runtime\.(NativeAOT\.)?win-x64$') { continue }
            if ($dependency.version -notmatch '^\[([^,]+),\s*([^\]]+)\]$' -or $Matches[1] -ne $Matches[2]) { throw 'Runtime pack version is not exact.' }
            $version = $Matches[1]
            $package = Join-Path $packageRoot ($dependency.name.ToLowerInvariant() + '/' + $version)
            $legal = @(Get-ChildItem -LiteralPath $package -File | Where-Object Name -Match '^(LICENSE|THIRD-PARTY-NOTICES)(\..*)?$')
            if (-not ($legal | Where-Object Name -Match '^LICENSE')) { throw "Missing runtime license: $($dependency.name)" }
            $legalTarget = Join-Path $destination ("licenses/" + $dependency.name + '/' + $version)
            New-Item -ItemType Directory -Path $legalTarget -Force | Out-Null
            foreach ($file in $legal) { Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $legalTarget $file.Name) -Force }
        }
    }
    }
    $files = @(Get-ChildItem -LiteralPath $destination -Recurse -File | Sort-Object FullName | ForEach-Object {
        if ($_.Name -match '^(?i:GameAssembly\.dll|UnityPlayer\.dll|WarhammerSurvivors.*\.exe|global-metadata\.dat|MelonLoader\.dll)$' -or $_.Extension -in @('.pdb', '.lib')) {
            throw "Excluded file in publish output: $($_.Name)"
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
        inGameConnectionUi = $true
        playableMultiplayer = $false
        builtUtc = [DateTime]::UtcNow.ToString('O')
        files = $files
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$destination/package-manifest.json" -Encoding utf8NoBOM
    Write-Host "Packaged in-game connection UI and legacy development tools: $destination"
    Write-Host 'Shared-world multiplayer gameplay is not implemented.'
} finally { Pop-Location }
