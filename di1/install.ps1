param(
    [Parameter(Mandatory=$true)][string]$GameDirectory,
    [string]$ArtifactDirectory = $PSScriptRoot
)
$ErrorActionPreference = 'Stop'
$gamePath = (Resolve-Path -LiteralPath $GameDirectory).Path
$exe = Join-Path $gamePath 'DisneyInfinity1.exe'
if (!(Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'DisneyInfinity1.exe was not found in the selected directory.' }
if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne '856E9D3C38F74FCD778109FA46591B092E431989DD8AB269ECCF4A7F7D48474F') {
    throw 'The game executable differs from the measured DI1 Gold PC build. No files were changed.'
}
if (Get-Process -Name DisneyInfinity1 -ErrorAction SilentlyContinue) { throw 'Close Disney Infinity before installing.' }
$artifact = Join-Path $ArtifactDirectory 'LiteralCrab.asi'
$ini = Join-Path $ArtifactDirectory 'LiteralCrab.ini'
if (!(Test-Path -LiteralPath $artifact -PathType Leaf) -or !(Test-Path -LiteralPath $ini -PathType Leaf)) {
    throw 'LiteralCrab.asi and LiteralCrab.ini are required in ArtifactDirectory.'
}
if (!(Test-Path -LiteralPath (Join-Path $gamePath 'dinput8.dll') -PathType Leaf)) {
    throw 'Install a compatible 32-bit ASI loader first (the tested installation uses Ultimate ASI Loader as dinput8.dll).'
}
$oldMod = Join-Path $gamePath 'DisneyInfinity4K.asi'
$legacyAsi = Join-Path $gamePath 'CrabeLoaderDI1.asi'
$knownLegacyHashes = @(
    '57004C0EE9B7D9303E7DC4CF00EFDB912C06EF1D3621FC1F28DAE17C12E8501A',
    'F80D9C725B8B51771C5792814A29D8BAF3F9E422089D439CC082280D0AFC24D7'
)
if ((Test-Path -LiteralPath $legacyAsi -PathType Leaf) -and
    (Get-FileHash -LiteralPath $legacyAsi -Algorithm SHA256).Hash -notin $knownLegacyHashes) {
    throw 'An unrecognized CrabeLoaderDI1.asi is present. Move it aside before installing LiteralCrab.'
}
if ((Test-Path -LiteralPath $oldMod -PathType Leaf) -and
    (Get-FileHash -LiteralPath $oldMod -Algorithm SHA256).Hash -ne 'A532CA12FC6475453D97504D67F8940C3B127E1C163D85B1DD558D136E6CDDAC') {
    throw 'An unrecognized DisneyInfinity4K.asi is present. Move it aside before installing this display patch.'
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backups = @()
$disabled = @()
foreach ($name in @('LiteralCrab.asi')) {
    $target = Join-Path $gamePath $name
    if (Test-Path -LiteralPath $target -PathType Leaf) {
        $backup = "$target.$stamp.backup"
        Copy-Item -LiteralPath $target -Destination $backup
        $backups += $backup
    }
}
foreach ($previousAsi in @($oldMod,$legacyAsi)) {
    if (Test-Path -LiteralPath $previousAsi -PathType Leaf) {
        $disabledPath = "$previousAsi.disabled"
        if (Test-Path -LiteralPath $disabledPath) { $disabledPath = "$previousAsi.$stamp.disabled" }
        Move-Item -LiteralPath $previousAsi -Destination $disabledPath
        $disabled += $disabledPath
    }
}
Copy-Item -LiteralPath $artifact -Destination (Join-Path $gamePath 'LiteralCrab.asi') -Force
if (!(Test-Path -LiteralPath (Join-Path $gamePath 'LiteralCrab.ini'))) {
    $legacyIni = Join-Path $gamePath 'CrabeLoaderDI1.ini'
    $configuration = if (Test-Path -LiteralPath $legacyIni -PathType Leaf) { $legacyIni } else { $ini }
    Copy-Item -LiteralPath $configuration -Destination (Join-Path $gamePath 'LiteralCrab.ini')
}
$receipt = [ordered]@{
    installedAt = (Get-Date).ToString('o')
    pluginSha256 = (Get-FileHash -LiteralPath (Join-Path $gamePath 'LiteralCrab.asi')).Hash
    disabledDisplayMods = $disabled
    backups = $backups
}
$receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $gamePath 'LiteralCrab-install.json') -Encoding utf8
Write-Output "Installed LiteralCrab.asi in $gamePath."
Write-Output 'The existing INI was preserved, if present. Bink, the executable, and game assets were not changed.'
if ($disabled.Count) { Write-Output "Previous 4K display mod preserved at: $($disabled -join ', ')" }
