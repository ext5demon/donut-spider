[CmdletBinding()]
param(
    [ValidateSet('Public', 'Dev')]
    [string]$Variant = 'Public',
    [string]$BuildDirectory,
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$version = 'PA_001'
$isDev = $Variant -eq 'Dev'

if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $repoRoot $(if ($isDev) { 'build-ps3-dev-release' } else { 'build-ps3-public' })
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot 'dist'
}

$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)

if ([string]::IsNullOrWhiteSpace($env:PS3DEV)) {
    throw 'PS3DEV is not set. Point it at your PSL1GHT/ps3dev installation.'
}

$toolDirectory = Join-Path $env:PS3DEV 'bin'
$makeSelf = Join-Path $toolDirectory 'make_self_npdrm.exe'
$sfo = Join-Path $toolDirectory 'sfo.exe'
$pkg = Join-Path $toolDirectory 'pkg.exe'
$finalize = Join-Path $toolDirectory 'package_finalize.exe'
foreach ($tool in @($makeSelf, $sfo, $pkg, $finalize)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Missing PS3 packaging tool: $tool"
    }
}

$elf = @(
    (Join-Path $BuildDirectory 'donut-spider.elf'),
    (Join-Path $BuildDirectory 'donut-spider'),
    (Join-Path $BuildDirectory 'spider-donut.elf')
) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if ($null -eq $elf) {
    throw "Could not find the Donut Spider ELF in $BuildDirectory"
}

if ($isDev) {
    $title = 'donut spider-dev'
    $appId = 'DONSD0001'
    $contentId = 'UP0001-DONSD0001_00-DONUTSPIDERDEV01'
    $fileName = "donut-spider-dev-$version.pkg"
} else {
    $title = 'Donut Spider'
    $appId = 'DONS00001'
    $contentId = 'UP0001-DONS00001_00-DONUTSPIDERPA001'
    $fileName = "Donut-Spider-$version.pkg"
}

$stageDirectory = Join-Path $BuildDirectory "package-$($Variant.ToLowerInvariant())"
$expectedPrefix = $BuildDirectory.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if (-not $stageDirectory.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe staging path: $stageDirectory"
}
if (Test-Path -LiteralPath $stageDirectory) {
    Remove-Item -LiteralPath $stageDirectory -Recurse -Force
}

$usrDirectory = Join-Path $stageDirectory 'USRDIR'
New-Item -ItemType Directory -Force -Path $usrDirectory, $OutputDirectory | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'PKG_TEMPLATE\ICON0.PNG') -Destination (Join-Path $stageDirectory 'ICON0.PNG')

$oldPath = $env:Path
$env:Path = "$toolDirectory;$oldPath"
try {
    & $makeSelf $elf (Join-Path $usrDirectory 'EBOOT.BIN') $contentId
    if ($LASTEXITCODE -ne 0) { throw "make_self_npdrm failed with exit code $LASTEXITCODE" }

    & $sfo --title $title --appid $appId -f (Join-Path $repoRoot 'packaging\sfo.xml') (Join-Path $stageDirectory 'PARAM.SFO')
    if ($LASTEXITCODE -ne 0) { throw "sfo failed with exit code $LASTEXITCODE" }

    $outputPackage = Join-Path $OutputDirectory $fileName
    & $pkg --contentid=$contentId $stageDirectory $outputPackage
    if ($LASTEXITCODE -ne 0) { throw "pkg failed with exit code $LASTEXITCODE" }

    & $pkg --list $outputPackage | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "The package could not be inspected before finalization (exit code $LASTEXITCODE)" }

    & $finalize $outputPackage
    if ($LASTEXITCODE -ne 0) { throw "package_finalize failed with exit code $LASTEXITCODE" }
} finally {
    $env:Path = $oldPath
}

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $outputPackage).Hash.ToLowerInvariant()
$checksumPath = "$outputPackage.sha256"
[IO.File]::WriteAllText($checksumPath, "$hash  $([IO.Path]::GetFileName($outputPackage))`n", (New-Object Text.UTF8Encoding($false)))

Write-Host "Built $title $version" -ForegroundColor Green
Write-Host $outputPackage
Write-Host "SHA-256: $hash"
