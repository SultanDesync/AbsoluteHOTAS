[CmdletBinding()]
param(
    [string]$StageRoot = (Join-Path $PSScriptRoot '../contrib/PluginRelease/Data'),
    [string]$BuiltDll = (Join-Path $PSScriptRoot '../build/windows/x64/releasedbg/AbsoluteHOTAS.dll'),
    [string]$ArchivePath = (Join-Path $PSScriptRoot '../build/AbsoluteHOTAS-validation.zip')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$stage = (Resolve-Path -LiteralPath $StageRoot).Path
$expected = @('SFSE/Plugins/AbsoluteHOTAS.dll', 'SFSE/Plugins/AbsoluteHOTAS.ini')
$actual = @(Get-ChildItem -LiteralPath $stage -File -Recurse -Force | ForEach-Object {
    [System.IO.Path]::GetRelativePath($stage, $_.FullName).Replace('\', '/')
})
if (@(Compare-Object $expected $actual -CaseSensitive).Count -ne 0) {
    throw "Package must contain exactly the DLL and default INI. Found: $($actual -join ', ')"
}

$defaultIni = Join-Path $PSScriptRoot '../config/AbsoluteHOTAS.ini'
foreach ($pair in @(
    @($BuiltDll, (Join-Path $stage $expected[0])),
    @($defaultIni, (Join-Path $stage $expected[1]))
)) {
    if ((Get-FileHash -LiteralPath $pair[0]).Hash -ne (Get-FileHash -LiteralPath $pair[1]).Hash) {
        throw "Staged file does not match its build/source: $($pair[1])"
    }
}

$archive = [System.IO.Path]::GetFullPath($ArchivePath)
$zip = [System.IO.Compression.ZipArchive]::new(
    [System.IO.File]::Open($archive, [System.IO.FileMode]::Create),
    [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($entry in $expected) {
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $zip, (Join-Path $stage $entry), $entry) | Out-Null
    }
} finally {
    $zip.Dispose()
}
$zip = [System.IO.Compression.ZipFile]::OpenRead($archive)
try {
    if (@(Compare-Object $expected @($zip.Entries.FullName) -CaseSensitive).Count -ne 0) {
        throw 'ZIP entry layout does not match the two-file release contract.'
    }
    foreach ($entry in $zip.Entries) {
        $stream = $entry.Open()
        $sha = [System.Security.Cryptography.SHA256]::Create()
        try {
            $zipHash = [System.Convert]::ToHexString($sha.ComputeHash($stream))
            if ($zipHash -ne (Get-FileHash -LiteralPath (Join-Path $stage $entry.FullName)).Hash) {
                throw "ZIP content mismatch: $($entry.FullName)"
            }
        } finally {
            $sha.Dispose()
            $stream.Dispose()
        }
    }
} finally {
    $zip.Dispose()
}
Write-Output "Verified two-file package, source hashes, and ZIP contents: $archive"
