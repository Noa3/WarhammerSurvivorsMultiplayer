[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Archive,
    [Parameter(Mandatory)][string]$Schema,
    [Parameter(Mandatory)][string]$BaseSchema
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
# The published FO3 schema redefines the GEMM schema via HTTP. Resolve the
# already-downloaded exact base schema locally; do not contact the network.
$baseUri = [Uri]::new([IO.Path]::GetFullPath($BaseSchema)).AbsoluteUri
$schemaText = [IO.File]::ReadAllText([IO.Path]::GetFullPath($Schema)).Replace('http://qconsulting.ca/gemm/ModConfig5.0.xsd', $baseUri)
$settings = [Xml.XmlReaderSettings]::new()
$settings.XmlResolver = [Xml.XmlUrlResolver]::new()
$reader = [Xml.XmlReader]::Create([IO.StringReader]::new($schemaText), $settings)
try {
    $schemas = [Xml.Schema.XmlSchemaSet]::new()
    $schemas.XmlResolver = [Xml.XmlUrlResolver]::new()
    [void]$schemas.Add('', $reader)
    $schemas.Compile()
} finally { $reader.Dispose() }
$zip = [IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($Archive))
try {
    $entry = $zip.GetEntry('fomod/ModuleConfig.xml')
    if ($null -eq $entry -or $null -eq $zip.GetEntry('fomod/info.xml')) { throw 'FOMOD detection files are missing.' }
    $xmlSettings = [Xml.XmlReaderSettings]::new()
    $xmlSettings.XmlResolver = $null
    $xmlSettings.ValidationType = [Xml.ValidationType]::Schema
    $xmlSettings.Schemas = $schemas
    $xmlSettings.add_ValidationEventHandler({ param($sender, $event) throw $event.Message })
    $stream = $entry.Open()
    try {
        $xmlReader = [Xml.XmlReader]::Create($stream, $xmlSettings)
        try { while ($xmlReader.Read()) {} } finally { $xmlReader.Dispose() }
    } finally { $stream.Dispose() }
} finally { $zip.Dispose() }
Write-Host 'FOMOD ModuleConfig.xml validates against ModConfig5.0 and its GEMM base.'
