[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build.ps1') -Configuration Test
& (Join-Path $PSScriptRoot 'build/tests/regression.exe')
if ($LASTEXITCODE -ne 0) { throw 'Regression checks failed.' }
