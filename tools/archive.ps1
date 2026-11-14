[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$InputDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw 'Use 64-bit PowerShell 7 on Windows.' }
Add-Type -AssemblyName System.IO.Compression

function Assert-NoReparse([string]$Path) {
    $current = (Get-Item -LiteralPath $Path -Force).FullName
    while ($null -ne $current) {
        $item = Get-Item -LiteralPath $current -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse point not allowed: $($item.FullName)" }
        $current = [IO.Path]::GetDirectoryName($current)
    }
}
function Assert-NoDuplicateJsonKeys([System.Text.Json.JsonElement]$Element) {
    if ($Element.ValueKind -eq [System.Text.Json.JsonValueKind]::Object) {
        $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($property in $Element.EnumerateObject()) {
            if (-not $seen.Add($property.Name)) { throw "Duplicate JSON key: $($property.Name)" }
            Assert-NoDuplicateJsonKeys $property.Value
        }
    } elseif ($Element.ValueKind -eq [System.Text.Json.JsonValueKind]::Array) {
        foreach ($value in $Element.EnumerateArray()) { Assert-NoDuplicateJsonKeys $value }
    }
}
function Assert-SafeName([string]$Path) {
    if ([string]::IsNullOrEmpty($Path) -or $Path.Contains('\') -or $Path -match '(^/|//|[\x00-\x1f<>:"|?*])') { throw "Unsafe path: $Path" }
    $parts = $Path.Split('/')
    foreach ($part in $parts) {
        if ($part -in @('', '.', '..') -or $part.EndsWith('.') -or $part.EndsWith(' ') -or $part -match '^(?i:con|prn|aux|nul|com[1-9]|lpt[1-9])(\.|$)') { throw "Unsafe component: $Path" }
        if ($part -match '(?i)^(\.local|\.idea|\.git|obj|bin|artifacts|generatedinterop|interop|logs?|credentials?|secrets?|execplan|\.env)(\.|$)') { throw "Excluded content: $Path" }
    }
    if ($parts[0] -cnotin @('launcher','lobby','bridge','licenses')) { throw "Unexpected package root: $Path" }
    $name = $parts[-1]
    if ($name -match '(?i)^(GameAssembly\.dll|UnityPlayer\.dll|global-metadata\.dat|WarhammerSurvivors.*\.exe)$' -or $name -match '(?i)\.(pdb|zip|7z|rar|log|pem|pfx|key)$') { throw "Excluded file: $Path" }
    if ($name -match '(?i)\.exe$' -and $name -notin @('createdump.exe','Wsm.App.exe','Wsm.Launcher.exe')) { throw "Unexpected executable: $Path" }
}
function Get-TreeFiles([string]$Directory, [string]$Root) {
    foreach ($child in Get-ChildItem -LiteralPath $Directory -Force) {
        if (($child.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse point not allowed: $($child.FullName)" }
        $relative = [IO.Path]::GetRelativePath($Root, $child.FullName).Replace('\','/')
        if ($child.PSIsContainer) {
            Assert-SafeName $relative
            Get-TreeFiles $child.FullName $Root
        } else {
            if ($relative -cne 'package-manifest.json') { Assert-SafeName $relative }
            $relative
        }
    }
}
function Add-ZipFile($Zip, [string]$EntryName, [string]$Path) {
    $entry = $Zip.CreateEntry($EntryName, [IO.Compression.CompressionLevel]::Optimal)
    $inputStream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try { $stream = $entry.Open(); try { $inputStream.CopyTo($stream) } finally { $stream.Dispose() } } finally { $inputStream.Dispose() }
}
$inputPath = [IO.Path]::GetFullPath($InputDirectory).TrimEnd('\','/')
$outputPath = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\','/')
if (-not (Test-Path -LiteralPath $inputPath -PathType Container)) { throw 'Input directory does not exist.' }
Assert-NoReparse $inputPath
$parent = [IO.Path]::GetDirectoryName($outputPath)
if (-not (Test-Path -LiteralPath $parent -PathType Container)) { throw 'Output parent must exist.' }
Assert-NoReparse $parent
if ($outputPath.Equals($inputPath,[StringComparison]::OrdinalIgnoreCase) -or $outputPath.StartsWith($inputPath + '\',[StringComparison]::OrdinalIgnoreCase) -or $inputPath.StartsWith($outputPath + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Input and output must not overlap.' }
if (Test-Path -LiteralPath $outputPath) { throw 'Output already exists.' }
$manifestPath = Join-Path $inputPath 'package-manifest.json'
Assert-NoReparse $manifestPath
$manifestBytes = [IO.File]::ReadAllBytes($manifestPath)
$manifestText = [Text.Encoding]::UTF8.GetString($manifestBytes).TrimStart([char]0xFEFF)
$json = [System.Text.Json.JsonDocument]::Parse($manifestText)
try { Assert-NoDuplicateJsonKeys $json.RootElement } finally { $json.Dispose() }
$manifest = $manifestText | ConvertFrom-Json -AsHashtable
if ($manifest.target -cne 'win-x64' -or $manifest.selfContainedDotNet -isnot [bool] -or $manifest.selfContainedDotNet -ne $true -or $manifest.playableMultiplayer -isnot [bool] -or $manifest.playableMultiplayer -ne $false -or $manifest.files -isnot [array] -or $manifest.files.Count -eq 0) { throw 'Unsupported package manifest.' }
$expected = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($file in $manifest.files) {
    if ($file.path -isnot [string]) { throw 'Invalid manifest path.' }
    Assert-SafeName $file.path
    if ($file.bytes -isnot [long] -and $file.bytes -isnot [int]) { throw "Invalid size: $($file.path)" }
    if ($file.bytes -lt 0 -or $file.sha256 -isnot [string] -or $file.sha256 -cnotmatch '^[a-f0-9]{64}$') { throw "Invalid metadata: $($file.path)" }
    if (-not $expected.TryAdd($file.path, $file)) { throw "Duplicate path: $($file.path)" }
}
if (-not @($expected.Keys | Where-Object { $_ -like 'licenses/*/LICENSE*' }).Count) { throw 'Missing runtime license.' }
$found = @(Get-TreeFiles $inputPath $inputPath)
if ($found.Count -ne $expected.Count + 1) { throw 'Package file count does not match manifest.' }
foreach ($name in $found) {
    if ($name -ceq 'package-manifest.json') { continue }
    if (-not $expected.ContainsKey($name) -or $expected[$name].path -cne $name) { throw "Unexpected file or case mismatch: $name" }
    $item = Get-Item -LiteralPath (Join-Path $inputPath $name) -Force
    if ($item.Length -ne $expected[$name].bytes -or (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash -cne $expected[$name].sha256.ToUpperInvariant()) { throw "Integrity mismatch: $name" }
}
$created = $false
$owned = [Collections.Generic.List[string]]::new()
try {
    New-Item -ItemType Directory -Path $outputPath -ErrorAction Stop | Out-Null
    $created = $true
    foreach ($variant in @('portable','vortex')) {
        $archivePath = Join-Path $outputPath "WSM-$variant-win-x64.zip"
        $fs = [IO.File]::Open($archivePath, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        $owned.Add($archivePath)
        try {
            $zip = [IO.Compression.ZipArchive]::new($fs, [IO.Compression.ZipArchiveMode]::Create, $true)
            try {
                $prefix = if ($variant -eq 'portable') { 'WSM/' } else { 'payload/' }
                foreach ($name in ($expected.Keys | Sort-Object)) { Add-ZipFile $zip ($prefix + $name) (Join-Path $inputPath $name) }
                $entry = $zip.CreateEntry(($prefix + 'package-manifest.json'), [IO.Compression.CompressionLevel]::Optimal)
                $stream = $entry.Open(); try { $stream.Write($manifestBytes, 0, $manifestBytes.Length) } finally { $stream.Dispose() }
                Add-ZipFile $zip ($prefix + 'README.txt') (Join-Path $PSScriptRoot '../packaging/README.txt')
                if ($variant -eq 'vortex') {
                    Add-ZipFile $zip 'fomod/ModuleConfig.xml' (Join-Path $PSScriptRoot '../packaging/ModuleConfig.xml')
                    Add-ZipFile $zip 'fomod/info.xml' (Join-Path $PSScriptRoot '../packaging/info.xml')
                }
            } finally { $zip.Dispose() }
        } finally { $fs.Dispose() }
        # Validate bytes actually written, detecting modifications between validation and archive reads.
        $zip = [IO.Compression.ZipFile]::OpenRead($archivePath)
        try {
            foreach ($name in $expected.Keys) {
                $entry = $zip.GetEntry($prefix + $name)
                if ($null -eq $entry -or $entry.Length -ne $expected[$name].bytes) { throw "Archived size mismatch: $name" }
                $stream = $entry.Open(); $sha = [Security.Cryptography.SHA256]::Create()
                try { $hash = [Convert]::ToHexString($sha.ComputeHash($stream)).ToLowerInvariant() } finally { $sha.Dispose(); $stream.Dispose() }
                if ($hash -cne $expected[$name].sha256) { throw "Archived hash mismatch: $name" }
            }
        } finally { $zip.Dispose() }
    }
    Write-Host "Archives created in $outputPath"
} catch {
    foreach ($file in $owned) { Remove-Item -LiteralPath $file -Force -ErrorAction SilentlyContinue }
    if ($created) { [IO.Directory]::Delete($outputPath, $false) }
    throw
}
