[CmdletBinding()]
param([switch]$Offline)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$deps = Join-Path $projectRoot 'build/deps'
New-Item -ItemType Directory -Path $deps -Force | Out-Null
$downloads = @(
    @{ Name='vigem.zip'; Url='https://api.nuget.org/v3-flatcontainer/nefarius.vigem.client/1.21.256/nefarius.vigem.client.1.21.256.nupkg'; Sha='FF460031B33E0882CD1166FE2D072D0FE97578480C61ED85DCCFA6C930FDB55E' },
    @{ Name='HidHide_1.5.230_x64.exe'; Url='https://github.com/nefarius/HidHide/releases/download/v1.5.230.0/HidHide_1.5.230_x64.exe'; Sha='F4BBBCB82E6258641B887C74BC81C4C5F66E4AA811808DFC304347687B7605F6' },
    @{ Name='ViGEmBus_1.22.0_x64_x86_arm64.exe'; Url='https://github.com/nefarius/ViGEmBus/releases/download/v1.22.0/ViGEmBus_1.22.0_x64_x86_arm64.exe'; Sha='89220A7865076B342892F98865F3499FB7C4CFD673159E89D352C360FD014C6A' }
)
foreach ($download in $downloads) {
    $path = Join-Path $deps $download.Name
    if (!(Test-Path -LiteralPath $path)) {
        if ($Offline) { throw "Missing offline dependency: $path" }
        Invoke-WebRequest $download.Url -OutFile $path
    }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $download.Sha) { throw "Dependency hash mismatch: $path" }
    if ($download.Name.EndsWith('.exe')) {
        $signature = Get-AuthenticodeSignature -FilePath $path
        if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Nefarius Software Solutions') { throw "Invalid driver publisher: $path" }
    }
}
$expanded = Join-Path $deps 'vigem'
if (!(Test-Path -LiteralPath (Join-Path $expanded 'lib/netstandard2.0/Nefarius.ViGEm.Client.dll'))) { Expand-Archive (Join-Path $deps 'vigem.zip') $expanded -Force }
$assembly = [Reflection.Assembly]::LoadFile((Join-Path $expanded 'lib/netstandard2.0/Nefarius.ViGEm.Client.dll'))
$inputStream = $assembly.GetManifestResourceStream('costura64.vigemclient.dll')
if (!$inputStream) { throw 'Native x64 client resource missing.' }
$outputStream = [IO.File]::Create((Join-Path $deps 'ViGEmClient.dll'))
try { $inputStream.CopyTo($outputStream) } finally { $outputStream.Dispose(); $inputStream.Dispose() }
if ((Get-FileHash (Join-Path $deps 'ViGEmClient.dll') -Algorithm SHA256).Hash -ne '00303F99E1968D523FBD08A7C691D60D145B7335059EC593947841D124C10875') { throw 'Extracted client hash mismatch.' }
foreach ($license in @('ViGEmClient-LICENSE.txt','HidHide-LICENSE.txt')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot "third_party/$license") -Destination (Join-Path $deps $license) -Force
}
Write-Host 'Verified pinned input dependencies; no drivers installed.'
