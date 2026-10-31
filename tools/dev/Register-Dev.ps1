<#
.SYNOPSIS
Registers a locally built SplatThumb DLL as the .ply thumbnail handler for the current
user, for development and testing.

.DESCRIPTION
Before changing anything, the current state of every key it touches is saved to a state
file. Unregister-Dev.ps1 uses that file to put the registry back exactly as it was.
Only HKEY_CURRENT_USER is changed. Use -WhatIf to see the changes without making them.

This is not the way to install SplatThumb; use the MSI for that.

.EXAMPLE
.\tools\dev\Register-Dev.ps1 -DllPath build\msvc\bin\Release\splatthumb_shellext.dll -WhatIf
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory)] [string] $DllPath,
    [string] $StatePath
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'DevRegistration.Common.ps1')

if (-not $StatePath) { $StatePath = Get-DefaultStatePath }
$dll = (Resolve-Path -LiteralPath $DllPath).ProviderPath
if ([System.IO.Path]::GetExtension($dll) -ne '.dll') { throw "Not a DLL: $dll" }

if (Test-Path -LiteralPath $StatePath) {
    throw "A development registration is already recorded in $StatePath. Run Unregister-Dev.ps1 first."
}

$before = Get-Snapshot
$clsidKey = $before | Where-Object Path -eq "Software\Classes\CLSID\$SplatThumbClsid"
if ($clsidKey.Exists) {
    throw "HKCU\Software\Classes\CLSID\$SplatThumbClsid already exists (installed SplatThumb?). Uninstall it first; this script will not overwrite it."
}

$others = Get-OtherRegistrations
Write-Host ".ply ProgID: $(if ($others.PlyProgId) { $others.PlyProgId } else { '(none)' })"
if ($others.ThumbnailHandlers.Count -eq 0) {
    Write-Host 'Existing .ply thumbnail handlers: none'
} else {
    Write-Host 'Existing .ply thumbnail handlers (left as they are; HKCU .ply takes priority while registered):'
    $others.ThumbnailHandlers | ForEach-Object { Write-Host "  $($_.Key) = $($_.Handler)" }
}

$changes = @(
    "HKCU\Software\Classes\CLSID\$SplatThumbClsid (default) = SplatThumb Thumbnail Handler",
    "HKCU\Software\Classes\CLSID\$SplatThumbClsid\InprocServer32 (default) = $dll",
    "HKCU\Software\Classes\CLSID\$SplatThumbClsid\InprocServer32 ThreadingModel = Apartment",
    "HKCU\Software\Classes\.ply\ShellEx\$ThumbnailHandlerShellEx (default) = $SplatThumbClsid"
)
Write-Host 'Changes:'
$changes | ForEach-Object { Write-Host "  $_" }
Write-Host "State file: $StatePath"

if (-not $PSCmdlet.ShouldProcess('HKEY_CURRENT_USER', 'Register the SplatThumb development DLL')) {
    return
}

# Save the state first, so a failure halfway can still be undone.
$state = [pscustomobject]@{
    Format = 1
    CreatedUtc = (Get-Date).ToUniversalTime().ToString('o')
    DllPath = $dll
    Before = $before
    OtherRegistrations = $others
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $StatePath) | Out-Null
$state | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $StatePath -Encoding UTF8

$hkcu = [Microsoft.Win32.Registry]::CurrentUser
$key = $hkcu.CreateSubKey("Software\Classes\CLSID\$SplatThumbClsid")
$key.SetValue('', 'SplatThumb Thumbnail Handler'); $key.Dispose()
$key = $hkcu.CreateSubKey("Software\Classes\CLSID\$SplatThumbClsid\InprocServer32")
$key.SetValue('', $dll); $key.SetValue('ThreadingModel', 'Apartment'); $key.Dispose()
$key = $hkcu.CreateSubKey("Software\Classes\.ply\ShellEx\$ThumbnailHandlerShellEx")
$key.SetValue('', $SplatThumbClsid); $key.Dispose()

Send-AssociationChanged
Write-Host 'Registered. Explorer may need a restart, or the thumbnail cache cleared, to show new thumbnails.'
