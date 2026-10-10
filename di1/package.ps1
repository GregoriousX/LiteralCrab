param(
    [string]$BuildDirectory = 'build-di1\Release',
    [string]$OutputDirectory = 'dist-di1',
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-f]{40}$')][string]$SourceRevision
)
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$buildPath = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $repository $BuildDirectory }
$outputPath = if ([IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory } else { Join-Path $repository $OutputDirectory }
$version = (Get-Content -LiteralPath (Join-Path $repository 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'VERSION must contain a semantic version such as 1.0.0' }
$files = @(
    (Join-Path $buildPath 'LiteralCrab.asi'),
    (Join-Path $PSScriptRoot 'LiteralCrab.ini'),
    (Join-Path $repository 'README.md')
)
foreach ($path in $files) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing package input: $path" }
}
# Refuse to package a stale or incorrectly configured build.
$binary = [IO.File]::ReadAllBytes($files[0])
if ($binary.Length -lt 64 -or $binary[0] -ne 0x4d -or $binary[1] -ne 0x5a) { throw 'ASI has no DOS header' }
$peOffset = [BitConverter]::ToInt32($binary, 0x3c)
if ($peOffset -lt 0 -or $peOffset -gt ($binary.Length - 26) -or
    [BitConverter]::ToUInt32($binary, $peOffset) -ne 0x4550 -or
    [BitConverter]::ToUInt16($binary, $peOffset + 4) -ne 0x14c -or
    [BitConverter]::ToUInt16($binary, $peOffset + 24) -ne 0x10b -or
    ([BitConverter]::ToUInt16($binary, $peOffset + 22) -band 0x2000) -eq 0) {
    throw 'LiteralCrab.asi must be a PE32 x86 DLL'
}
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
$archive = Join-Path $outputPath "LiteralCrab-DI1-$version-x86.zip"
Compress-Archive -LiteralPath $files -DestinationPath $archive -Force

# Check the actual archive, including the untouched root README bytes.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
$sha = [Security.Cryptography.SHA256]::Create()
try {
    $names = @($zip.Entries | ForEach-Object FullName | Sort-Object)
    if (($names -join '|') -ne 'LiteralCrab.asi|LiteralCrab.ini|README.md') { throw "Unexpected ZIP entries: $($names -join ', ')" }
    foreach ($path in $files) {
        $entry = $zip.GetEntry([IO.Path]::GetFileName($path))
        $stream = $entry.Open()
        try { $digest = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
        finally { $stream.Dispose() }
        if ($digest -ne (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash) { throw "ZIP content mismatch: $path" }
    }
}
finally { $sha.Dispose(); $zip.Dispose() }

# These accompany distribution separately; they are never entries in the user ZIP.
Copy-Item -LiteralPath (Join-Path $repository 'LICENSE') -Destination $outputPath -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'NOTICE.txt') -Destination $outputPath -Force
@"
LiteralCrab $version corresponding source: $SourceRevision
Source: https://github.com/GregoriousX/LiteralCrab/tree/$SourceRevision
Download: https://github.com/GregoriousX/LiteralCrab/archive/$SourceRevision.zip
Build instructions are in the source README.md. Windows system libraries are supplied by the Windows SDK.
GPL-3.0-only; distribute LICENSE and NOTICE.txt alongside the binary download.
The three-file user ZIP alone omits the full licence and third-party notices.
"@ | Set-Content -LiteralPath (Join-Path $outputPath 'SOURCE.txt') -Encoding utf8
Write-Output $archive
