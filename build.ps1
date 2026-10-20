[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug', 'Test', 'FocusTest')][string]$Configuration = 'Release',
    [string]$Compiler = $env:SIDEDECK_CXX
)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim()
if ($version -notmatch '^(\d+)\.(\d+)\.(\d+)(?:-[0-9A-Za-z.-]+)?$') { throw 'VERSION must be a semantic version, e.g. 0.1.0.' }
$versionParts = "$($Matches[1]),$($Matches[2]),$($Matches[3]),0"
if (!$Compiler) {
    $bundled = Join-Path $projectRoot 'toolchain/mingw64/mingw64/bin/g++.exe'
    if (Test-Path -LiteralPath $bundled) { $Compiler = $bundled }
    else {
        $command = Get-Command g++.exe -ErrorAction SilentlyContinue
        if (!$command) { throw 'MinGW-w64 g++ was not found. Set SIDEDECK_CXX to its full path.' }
        $Compiler = $command.Source
    }
}
$Compiler = (Resolve-Path -LiteralPath $Compiler).Path
$binDir = Split-Path -Parent $Compiler
$previousPath = $env:PATH
$env:PATH = "$binDir;$previousPath"
try {
$generated = Join-Path $projectRoot 'build/generated'
$relativeOutput = if ($Configuration -in @('Test', 'FocusTest')) { 'build/tests' } else { "build/$($Configuration.ToLowerInvariant())" }
$outputDir = Join-Path $projectRoot $relativeOutput
New-Item -ItemType Directory -Path $generated,$outputDir -Force | Out-Null
$versionHeader = "#pragma once`n#define SIDEDECK_VERSION `"$version`"`n#define SIDEDECK_VERSION_W L`"$version`"`n#define SIDEDECK_VERSION_PARTS $versionParts`n"
[IO.File]::WriteAllText((Join-Path $generated 'sidedeck_version.hpp'), $versionHeader, [Text.UTF8Encoding]::new($false))
$arguments = @('-std=c++17', '-D_WIN32_WINNT=0x0A00', '-DUNICODE', '-D_UNICODE', '-static', '-static-libgcc', '-static-libstdc++', '-I', $generated)
if ($Configuration -in @('Test', 'FocusTest')) {
    $testName = if ($Configuration -eq 'FocusTest') { 'focus_integration' } else { 'regression' }
    $arguments += @('-O0', '-g', '-o', (Join-Path $outputDir "$testName.exe"), (Join-Path $projectRoot "tests/$testName.cpp"))
} else {
    $resource = Join-Path $generated 'SideDeck.res.o'
    & (Join-Path $binDir 'windres.exe') '-I' $generated (Join-Path $projectRoot 'src/SideDeck.rc') '-O' 'coff' '-o' $resource
    if ($LASTEXITCODE -ne 0) { throw 'Version resource compilation failed.' }
    if ($Configuration -eq 'Release') { $arguments += @('-O2', '-mwindows', '-fno-omit-frame-pointer', '-Wall', '-Wno-unused-variable', '-Wno-unused-but-set-variable') }
    else { $arguments += @('-O0', '-g', '-DDEBUG_LOG', '-Wl,--subsystem,console') }
    $arguments += @('-o', (Join-Path $outputDir 'SideDeck.exe'), (Join-Path $projectRoot 'src/main.cpp'), $resource)
}
$arguments += @('-lgdiplus', '-lshell32', '-lshlwapi', '-luser32', '-lgdi32', '-lole32', '-loleaut32', '-luuid', '-ladvapi32', '-lcomctl32', '-lcomdlg32', '-lwinmm', '-ldbghelp')
& $Compiler @arguments
if ($LASTEXITCODE -ne 0) { throw "$Configuration compilation failed." }
if ($Configuration -eq 'Release') {
    & (Join-Path $binDir 'strip.exe') '-s' (Join-Path $outputDir 'SideDeck.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Executable strip failed.' }
}
Write-Host "Built SideDeck v$version ($Configuration): $outputDir"

} finally { $env:PATH = $previousPath }
