param(
    [string]$StarfieldRoot = $env:STARFIELD_ROOT,
    [string]$PackageRoot
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($StarfieldRoot)) {
    throw "StarfieldRoot is required. Pass -StarfieldRoot or set the STARFIELD_ROOT environment variable."
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$packageRoot = if ($PackageRoot) { $PackageRoot } else { Join-Path $repoRoot "contrib\PluginRelease" }
$payloadData = Join-Path $packageRoot "Data"
$targetData = Join-Path $StarfieldRoot "Data"

if (!(Test-Path $payloadData)) {
    throw "Payload Data folder not found: $payloadData"
}

if (!(Test-Path $StarfieldRoot)) {
    throw "Starfield root not found: $StarfieldRoot"
}

New-Item -ItemType Directory -Force -Path $targetData | Out-Null
Copy-Item -Path (Join-Path $payloadData "*") -Destination $targetData -Recurse -Force

Write-Host "Deployed payload:"
Write-Host "  From: $payloadData"
Write-Host "  To:   $targetData"
