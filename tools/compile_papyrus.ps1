param(
    [string]$StarfieldRoot = $env:STARFIELD_ROOT,
    [string]$PackageRoot,
    [switch]$Release,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($StarfieldRoot)) {
    throw "StarfieldRoot is required. Pass -StarfieldRoot or set the STARFIELD_ROOT environment variable."
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$projectSourceCandidates = @(
    (Join-Path $repoRoot "projects\AbsoluteHOTAS\Data\Scripts\Source"),
    (Join-Path $repoRoot "Data\Scripts\Source")
)
$projectSource = $projectSourceCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
$outputDir = Join-Path $repoRoot "Data\Scripts"
$packageRoot = if ($PackageRoot) { $PackageRoot } else { Join-Path $repoRoot "contrib\PluginRelease" }
$packageScriptDir = Join-Path $packageRoot "Data\Scripts"
$packageScriptSourceDir = Join-Path $packageRoot "Data\Scripts\Source"
$compiler = Join-Path $StarfieldRoot "Tools\Papyrus Compiler\PapyrusCompiler.exe"
$contentResources = Join-Path $StarfieldRoot "Tools\ContentResources.zip"
$cachedImportRoot = Join-Path $repoRoot ".papyrus_imports\Scripts\Source"

function Expand-PapyrusImports {
    param(
        [string]$ArchivePath,
        [string]$Destination
    )

    if (!(Test-Path $ArchivePath)) {
        return
    }

    $scriptObject = Join-Path $Destination "ScriptObject.psc"
    if (Test-Path $scriptObject) {
        return
    }

    Write-Host "Extracting Papyrus imports from $ArchivePath..."
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null

    $zip = [IO.Compression.ZipFile]::OpenRead($ArchivePath)
    try {
        foreach ($entry in $zip.Entries) {
            if ($entry.FullName -notmatch '^Scripts/Source/.+\.(psc|flg)$') {
                continue
            }

            $relative = $entry.FullName.Substring("Scripts/Source/".Length) -replace '/', '\'
            $target = Join-Path $Destination $relative
            $targetDir = Split-Path -Parent $target
            New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $true)
        }
    } finally {
        $zip.Dispose()
    }
}

$importCandidates = @(
    $projectSource,
    $cachedImportRoot,
    (Join-Path $StarfieldRoot "Data\Scripts\Source"),
    (Join-Path $StarfieldRoot "Data\Source"),
    (Join-Path $StarfieldRoot "Tools\Papyrus Compiler\Scripts\Source")
)

$flagCandidates = @(
    (Join-Path $cachedImportRoot "Starfield_Papyrus_Flags.flg"),
    (Join-Path $cachedImportRoot "TESV_Papyrus_Flags.flg"),
    (Join-Path $projectSource "Starfield_Papyrus_Flags.flg"),
    (Join-Path $projectSource "TESV_Papyrus_Flags.flg"),
    (Join-Path $StarfieldRoot "Data\Scripts\Source\Starfield_Papyrus_Flags.flg"),
    (Join-Path $StarfieldRoot "Data\Scripts\Source\TESV_Papyrus_Flags.flg"),
    (Join-Path $StarfieldRoot "Data\Source\Starfield_Papyrus_Flags.flg"),
    (Join-Path $StarfieldRoot "Data\Source\TESV_Papyrus_Flags.flg"),
    (Join-Path $StarfieldRoot "Tools\Papyrus Compiler\Starfield_Papyrus_Flags.flg"),
    (Join-Path $StarfieldRoot "Tools\Papyrus Compiler\TESV_Papyrus_Flags.flg")
)

if (!(Test-Path $compiler)) {
    throw "Papyrus compiler not found: $compiler"
}

if (!$projectSource -or !(Test-Path $projectSource)) {
    throw "Project Papyrus source directory not found: $projectSource"
}

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
New-Item -ItemType Directory -Force -Path $packageScriptDir | Out-Null
New-Item -ItemType Directory -Force -Path $packageScriptSourceDir | Out-Null
Expand-PapyrusImports -ArchivePath $contentResources -Destination $cachedImportRoot

if ($Clean) {
    Get-ChildItem -Path $outputDir -Filter "AbsoluteHOTAS*.pex" -File -ErrorAction SilentlyContinue |
        Remove-Item -Force
    Get-ChildItem -Path $packageScriptDir -Filter "AbsoluteHOTAS*.pex" -File -ErrorAction SilentlyContinue |
        Remove-Item -Force
    Get-ChildItem -Path $packageScriptSourceDir -Filter "AbsoluteHOTAS*.psc" -File -ErrorAction SilentlyContinue |
        Remove-Item -Force
}

$imports = $importCandidates | Where-Object { Test-Path $_ } | ForEach-Object { (Resolve-Path $_).Path }
$importArg = "-import=$($imports -join ';')"
$flags = $flagCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

Write-Host "Papyrus compiler: $compiler"
Write-Host "Output: $outputDir"
Write-Host "Package scripts: $packageScriptDir"
Write-Host "Imports:"
$imports | ForEach-Object { Write-Host "  $_" }
if ($flags) {
    Write-Host "Flags: $flags"
} else {
    Write-Warning "No Starfield_Papyrus_Flags.flg or TESV_Papyrus_Flags.flg found. Compilation may fail if scripts use custom flags."
}

$scripts = @(
    "AbsoluteHOTAS.psc",
    "AbsoluteHOTASPlayer.psc"
)

$commonArgs = @(
    "-output=$outputDir",
    $importArg,
    "-ignorecwd",
    "-optimize"
)

if ($Release) {
    $commonArgs += "-release"
}

if ($flags) {
    $commonArgs += "-flags=$flags"
}

$failed = $false
foreach ($script in $scripts) {
    $scriptPath = Join-Path $projectSource $script
    if (!(Test-Path $scriptPath)) {
        Write-Error "Missing script source: $scriptPath"
        $failed = $true
        continue
    }

    Copy-Item -LiteralPath $scriptPath -Destination $packageScriptSourceDir -Force

    Write-Host ""
    Write-Host "Compiling $script..."
    & $compiler $scriptPath @commonArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Papyrus compile failed for $script with exit code $LASTEXITCODE"
        $failed = $true
    }
}

if ($failed) {
    exit 1
}

Write-Host ""
Write-Host "Compiled Papyrus outputs:"
Get-ChildItem -Path $outputDir -Filter "AbsoluteHOTAS*.pex" -File |
    ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $packageScriptDir -Force
        Write-Host "  $($_.FullName)"
    }

Write-Host ""
Write-Host "Packaged Papyrus outputs:"
Get-ChildItem -Path $packageScriptDir -Filter "AbsoluteHOTAS*.pex" -File |
    ForEach-Object { Write-Host "  $($_.FullName)" }
