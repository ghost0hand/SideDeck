[CmdletBinding()]
param([switch]$VerifyTag)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$') { throw 'Invalid VERSION.' }
$tag = "v$version"
$commit = (& git -C $projectRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Commit the source before packaging a release.' }
if ((& git -C $projectRoot status --porcelain --untracked-files=normal)) { throw 'Commit source changes before packaging.' }
if ($VerifyTag) {
    $tagCommit = & git -C $projectRoot rev-parse "$tag`^{commit}"
    if ($LASTEXITCODE -ne 0 -or $tagCommit.Trim() -ne $commit) { throw "$tag must point to HEAD and match VERSION." }
    if ($env:GITHUB_REF_NAME -and $env:GITHUB_REF_NAME -ne $tag) { throw 'Pushed tag does not match VERSION.' }
}
& (Join-Path $projectRoot 'test.ps1')
& (Join-Path $projectRoot 'build.ps1') -Configuration Release
$exe = Join-Path $projectRoot 'build/release/SideDeck.exe'
if ((Get-Item -LiteralPath $exe).VersionInfo.ProductVersion -ne $version) { throw 'Executable version does not match VERSION.' }
$output = Join-Path $projectRoot "releases/$tag"
$stage = Join-Path $projectRoot "build/package/$tag"
New-Item -ItemType Directory -Path $stage,$output -Force | Out-Null
# An allowlist deliberately prevents local guides/config/assets entering the release.
$included = @('SideDeck.exe', 'README.md', 'CHANGELOG.md', 'VERSION', 'release.json')
if (Test-Path -LiteralPath $stage) {
    $unexpected = Get-ChildItem -LiteralPath $stage -Force | Where-Object { $_.Name -notin $included -or $_.PSIsContainer }
    if ($unexpected) { throw 'Package staging directory has unexpected files; use a fresh build directory.' }
}
Copy-Item -LiteralPath $exe -Destination (Join-Path $stage 'SideDeck.exe') -Force
foreach ($file in @('README.md', 'CHANGELOG.md', 'VERSION')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $file) -Destination (Join-Path $stage $file) -Force
}
$manifest = [ordered]@{ product = 'SideDeck'; version = $version; tag = $tag; commit = $commit; executable = 'SideDeck.exe'; sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant() }
[IO.File]::WriteAllText((Join-Path $stage 'release.json'), ($manifest | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
$zip = Join-Path $output "SideDeck-$tag-win-x64.zip"
Add-Type -AssemblyName System.IO.Compression
$stream = [IO.File]::Open($zip, [IO.FileMode]::Create, [IO.FileAccess]::ReadWrite)
$archive = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in $included) { [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, (Join-Path $stage $file), $file) | Out-Null }
} finally { $archive.Dispose(); $stream.Dispose() }
$checksum = "$( (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant() )  $(Split-Path -Leaf $zip)`n"
[IO.File]::WriteAllText((Join-Path $output 'SHA256SUMS.txt'), $checksum, [Text.UTF8Encoding]::new($false))
$changelog = Get-Content -LiteralPath (Join-Path $projectRoot 'CHANGELOG.md') -Raw
$section = [regex]::Match($changelog, '(?ms)^## ' + [regex]::Escape($version) + '(?:\s|$).*?(?=^## |\z)').Value.Trim()
if (!$section) { throw "CHANGELOG.md is missing $version." }
[IO.File]::WriteAllText((Join-Path $output 'release-notes.md'), $section, [Text.UTF8Encoding]::new($false))
Write-Host "Release ready: $zip ($commit)"
