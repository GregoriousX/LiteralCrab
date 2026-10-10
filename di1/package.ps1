param(
    [string]$BuildDirectory = 'build-di1\Release',
    [string]$OutputDirectory = 'dist-di1',
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-f]{40}$')][string]$SourceRevision
)
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$buildPath = if ([IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory } else { Join-Path $repository $BuildDirectory }
$outputPath = if ([IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory } else { Join-Path $repository $OutputDirectory }
$packagePath = Join-Path $outputPath 'LiteralCrab-DI1-0.1.0-x86'
New-Item -ItemType Directory -Path $packagePath -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $buildPath 'LiteralCrab.asi') -Destination $packagePath -Force
foreach ($name in @('LiteralCrab.ini','install.ps1','README.md','profile.md','VALIDATION.md','NOTICE.txt')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $packagePath -Force
}
Copy-Item -LiteralPath (Join-Path $repository 'LICENSE') -Destination $packagePath -Force
@"
LiteralCrab 0.1.0 — Disney Infinity 1.0 Gold PC, Windows x86 ASI
Corresponding source revision: $SourceRevision
Source: https://github.com/GregoriousX/LiteralCrab/tree/$SourceRevision
Archive: https://github.com/GregoriousX/LiteralCrab/archive/$SourceRevision.zip
Upstream dev: 7f251e8eec5de534a2ed9b8efa0694f259706f0f
Build instructions: README.md
Licence: GPL-3.0-only, with retained third-party notices in NOTICE.txt.
The package contains the ASI only; install a compatible 32-bit ASI loader separately.
"@ | Set-Content -LiteralPath (Join-Path $packagePath 'SOURCE.txt') -Encoding utf8
Get-ChildItem -LiteralPath $packagePath -File | Where-Object Name -ne 'SHA256SUMS.txt' | Sort-Object Name | ForEach-Object {
    "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $($_.Name)"
} | Set-Content -LiteralPath (Join-Path $packagePath 'SHA256SUMS.txt') -Encoding ascii
$archive = Join-Path $outputPath 'LiteralCrab-DI1-0.1.0-x86.zip'
Compress-Archive -LiteralPath $packagePath -DestinationPath $archive -Force
Write-Output $archive
