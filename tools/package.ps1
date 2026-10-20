[CmdletBinding()]
param([string]$Compiler, [switch]$Offline, [switch]$SkipBuild)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'prepare-input-deps.ps1') -Offline:$Offline
if (!$SkipBuild) { & (Join-Path $projectRoot 'build.ps1') }
if (!$Compiler) {
    foreach ($candidate in @((Join-Path $projectRoot 'toolchain/innosetup/ISCC.exe'), 'C:/Program Files (x86)/Inno Setup 6/ISCC.exe', 'C:/Program Files/Inno Setup 7/ISCC.exe')) {
        if(Test-Path -LiteralPath $candidate) {$Compiler=$candidate;break}
    }
}
if (!$Compiler) {throw 'Install Inno Setup 6/7 or pass -Compiler path/to/ISCC.exe.'}
$version=(Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim()
& $Compiler "/DAppVersion=$version" (Join-Path $projectRoot 'installer/SideDeck.iss')
if($LASTEXITCODE -ne 0){throw 'Installer compilation failed.'}
$installer=Join-Path $projectRoot "releases/v$version/SideDeck-v$version-Setup.exe"
$hash=(Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path (Split-Path $installer) 'INSTALLER-SHA256.txt'), "$hash  $(Split-Path -Leaf $installer)`n", [Text.UTF8Encoding]::new($false))
Write-Host "Installer ready: $installer"
