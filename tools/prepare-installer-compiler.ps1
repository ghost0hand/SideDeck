[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$target=Join-Path $projectRoot 'toolchain/innosetup'
if(Test-Path -LiteralPath (Join-Path $target 'ISCC.exe')){return}
$download=Join-Path $projectRoot 'build/deps/innosetup-7.1.0-x64.exe'
New-Item -ItemType Directory -Path (Split-Path $download) -Force | Out-Null
if(!(Test-Path -LiteralPath $download)) {
    Invoke-WebRequest 'https://github.com/jrsoftware/issrc/releases/download/is-7_1_0/innosetup-7.1.0-x64.exe' -OutFile $download
}
if((Get-FileHash $download -Algorithm SHA256).Hash -ne '0362A383ED217D4C4239B5933866DD96D3EB2102737DA92F80F6057A4B40DF2F'){throw 'Compiler hash mismatch.'}
if((Get-AuthenticodeSignature $download).Status -ne 'Valid'){throw 'Compiler signature invalid.'}
$process=Start-Process -FilePath $download -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-','/CURRENTUSER','/NOICONS',('/DIR="'+$target+'"')) -WindowStyle Hidden -PassThru -Wait
if($process.ExitCode -ne 0){throw 'Inno Setup compiler installation failed.'}
