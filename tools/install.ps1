[CmdletBinding()]
param([switch]$ArchiveLegacy)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd([IO.Path]::DirectorySeparatorChar)
$source = Join-Path $projectRoot 'build/release/SideDeck.exe'
$version = (Get-Content -LiteralPath (Join-Path $projectRoot 'VERSION') -Raw).Trim()
if (!(Test-Path -LiteralPath $source)) { throw 'Run build.ps1 first.' }
foreach ($dependency in @('build/release/SideDeck.InputProbe.exe','build/deps/ViGEmClient.dll','build/deps/HidHide_1.5.230_x64.exe','build/deps/ViGEmBus_1.22.0_x64_x86_arm64.exe')) {
    if (!(Test-Path -LiteralPath (Join-Path $projectRoot $dependency))) { throw 'Run tools/package.ps1 to prepare the complete package first.' }
}
if ((Get-Item -LiteralPath $source).VersionInfo.ProductVersion -ne $version) { throw 'Build version differs from VERSION.' }
# Only ask this workspace's own overlay windows to quit normally. No forced process termination.
$running = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
    if ($_.ProcessName -notlike 'SideDeck*') { return $false }
    try { return [IO.Path]::GetDirectoryName($_.Path) -eq $projectRoot } catch { return $false }
})
if ($running.Count) {
    if (!('SideDeckInstall.Native' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
namespace SideDeckInstall {
    public static class Native {
        public delegate bool EnumProc(IntPtr window, IntPtr parameter);
        [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
        [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
        [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr window, StringBuilder text, int count);
        [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    }
}
'@
    }
    $processIds = @($running | ForEach-Object { [uint32]$_.Id })
    $callback = [SideDeckInstall.Native+EnumProc] {
        param([IntPtr]$window, [IntPtr]$parameter)
        [uint32]$overlayProcessId = 0
        [void][SideDeckInstall.Native]::GetWindowThreadProcessId($window, [ref]$overlayProcessId)
        if ($overlayProcessId -in $processIds) {
            $className = [Text.StringBuilder]::new(256)
            [void][SideDeckInstall.Native]::GetClassName($window, $className, 256)
            if ($className.ToString() -eq 'SideDeckOverlay') {
                [void][SideDeckInstall.Native]::PostMessage($window, 0x0111, [IntPtr]1006, [IntPtr]::Zero)
            }
        }
        return $true
    }
    [void][SideDeckInstall.Native]::EnumWindows($callback, [IntPtr]::Zero)
    foreach ($process in $running) {
        if (!$process.WaitForExit(5000)) { throw 'SideDeck is still running. Exit it from the tray and try again.' }
    }
}
if ($ArchiveLegacy) {
    $archive = [IO.Path]::GetFullPath((Join-Path $projectRoot ("backups/legacy-executables/" + (Get-Date -Format 'yyyyMMdd-HHmmss'))))
    if (!$archive.StartsWith($projectRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Backup path is outside the workspace.' }
    New-Item -ItemType Directory -Path $archive -Force | Out-Null
    foreach ($file in Get-ChildItem -LiteralPath $projectRoot -Filter 'SideDeck*.exe' -File) {
        Move-Item -LiteralPath $file.FullName -Destination (Join-Path $archive $file.Name)
    }
}
Copy-Item -LiteralPath $source -Destination (Join-Path $projectRoot 'SideDeck.exe') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'build/release/SideDeck.InputProbe.exe') -Destination (Join-Path $projectRoot 'SideDeck.InputProbe.exe') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'build/deps/ViGEmClient.dll') -Destination (Join-Path $projectRoot 'ViGEmClient.dll') -Force
New-Item -ItemType Directory -Path (Join-Path $projectRoot 'drivers'),(Join-Path $projectRoot 'licenses') -Force | Out-Null
foreach($file in @('HidHide_1.5.230_x64.exe','ViGEmBus_1.22.0_x64_x86_arm64.exe')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot "build/deps/$file") -Destination (Join-Path $projectRoot "drivers/$file") -Force
}
foreach($file in @('ViGEmClient-LICENSE.txt','HidHide-LICENSE.txt')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot "third_party/$file") -Destination (Join-Path $projectRoot "licenses/$file") -Force
}
Write-Host "Installed v$version as SideDeck.exe; personal config and guides retained."
