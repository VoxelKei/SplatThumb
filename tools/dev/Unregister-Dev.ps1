<#
.SYNOPSIS
Undoes Register-Dev.ps1: puts every key it touched back to the state saved before
registration, then checks that the registry matches that state.

.DESCRIPTION
Keys that did not exist before are removed; keys that did are given back their old
values. Nothing is done without the state file. Use -WhatIf to see the changes first.

.EXAMPLE
.\tools\dev\Unregister-Dev.ps1 -WhatIf
#>
[CmdletBinding(SupportsShouldProcess)]
param([string] $StatePath)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'DevRegistration.Common.ps1')

if (-not $StatePath) { $StatePath = Get-DefaultStatePath }
if (-not (Test-Path -LiteralPath $StatePath)) {
    throw "No state file at $StatePath, so there is nothing to undo. Nothing was changed."
}
$state = Get-Content -LiteralPath $StatePath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($state.Format -ne 1) { throw "Unknown state file format: $($state.Format)" }
$before = @($state.Before)
if ($before.Count -ne $ManagedKeys.Count) { throw 'State file does not match this script version.' }

Write-Host "Restoring the state saved at $($state.CreatedUtc):"
for ($i = $before.Count - 1; $i -ge 0; $i--) {
    $b = $before[$i]
    if (-not $b.Exists) {
        Write-Host "  HKCU\$($b.Path): remove (did not exist before)"
    } elseif ($ValueKeys -contains $b.Path) {
        Write-Host "  HKCU\$($b.Path): restore its $(@($b.Values).Count) earlier value(s)"
    }
}
if (-not $PSCmdlet.ShouldProcess('HKEY_CURRENT_USER', 'Restore the registry to its state before Register-Dev.ps1')) {
    return
}

$hkcu = [Microsoft.Win32.Registry]::CurrentUser
for ($i = $before.Count - 1; $i -ge 0; $i--) {
    $b = $before[$i]
    $isValueKey = $ValueKeys -contains $b.Path
    if (-not $b.Exists) {
        if ($isValueKey) {
            $hkcu.DeleteSubKeyTree($b.Path, $false)
            continue
        }
        # A parent key we created. Remove it only if nothing else was put in it since.
        $key = $hkcu.OpenSubKey($b.Path)
        if ($null -eq $key) { continue }
        $empty = $key.SubKeyCount -eq 0 -and $key.ValueCount -eq 0
        $key.Dispose()
        if ($empty) {
            $hkcu.DeleteSubKey($b.Path, $false)
        } else {
            Write-Host "  Kept HKCU\$($b.Path): something else has been added to it since registration."
        }
        continue
    }
    if (-not $isValueKey) {
        continue # an existing parent key; its values were never changed
    }
    # A key whose values we set: put back exactly the old values. Subkeys are handled by
    # their own entries in the list.
    $key = $hkcu.CreateSubKey($b.Path)
    try {
        foreach ($name in $key.GetValueNames()) { $key.DeleteValue($name) }
        foreach ($v in @($b.Values)) {
            $kind = [Microsoft.Win32.RegistryValueKind] $v.Kind
            $key.SetValue([string] $v.Name, (ConvertFrom-StoredData $kind ([string] $v.Data)), $kind)
        }
    } finally {
        $key.Dispose()
    }
}

Send-AssociationChanged

$diff = @(Compare-Snapshot $before (Get-Snapshot))
if ($diff.Count -gt 0) {
    Write-Host 'The registry does NOT match the saved state:'
    $diff | ForEach-Object { Write-Host "  $_" }
    throw 'Restore incomplete. The state file was kept; please report this.'
}

$base = Join-Path (Split-Path -Parent $StatePath) ([System.IO.Path]::GetFileNameWithoutExtension($StatePath))
$done = "$base.restored-$((Get-Date).ToString('yyyyMMdd-HHmmss-fff')).json"
for ($n = 2; Test-Path -LiteralPath $done; $n++) {
    $done = "$base.restored-$((Get-Date).ToString('yyyyMMdd-HHmmss-fff'))-$n.json"
}
Move-Item -LiteralPath $StatePath -Destination $done
Write-Host 'Restored: the registry matches the state before registration.'
Write-Host "The state file was kept as $done"
