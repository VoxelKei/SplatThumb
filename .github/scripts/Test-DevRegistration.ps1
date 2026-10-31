# Exercises tools/dev/Register-Dev.ps1 and Unregister-Dev.ps1 on the (throwaway) CI
# machine: the registry must be exactly as before after unregistering, both when another
# handler was registered before and when nothing was.
param([Parameter(Mandatory)] [string] $DllPath)
$ErrorActionPreference = 'Stop'
$tools = Join-Path $PSScriptRoot '..\..\tools\dev'
. (Join-Path $tools 'DevRegistration.Common.ps1')

$stateName = "splatthumb-ci-$PID"
$state = Join-Path ([System.IO.Path]::GetTempPath()) "$stateName.json"
$hkcu = [Microsoft.Win32.Registry]::CurrentUser
$handlerKey = "Software\Classes\.ply\ShellEx\$ThumbnailHandlerShellEx"

function Assert($Condition, [string] $Message) {
    if (-not $Condition) { throw "FAILED: $Message" }
}

function Test-RoundTrip([string] $Name) {
    $before = Get-Snapshot

    & (Join-Path $tools 'Register-Dev.ps1') -DllPath $DllPath -StatePath $state -WhatIf
    Assert (@(Compare-Snapshot $before (Get-Snapshot)).Count -eq 0) "$Name -WhatIf changed the registry"
    Assert (-not (Test-Path $state)) "$Name -WhatIf wrote a state file"

    & (Join-Path $tools 'Register-Dev.ps1') -DllPath $DllPath -StatePath $state
    $key = $hkcu.OpenSubKey($handlerKey)
    Assert ($key.GetValue('') -eq $SplatThumbClsid) "$Name handler not registered"
    $key.Dispose()
    $key = $hkcu.OpenSubKey("Software\Classes\CLSID\$SplatThumbClsid\InprocServer32")
    Assert ($key.GetValue('') -eq (Resolve-Path $DllPath).ProviderPath) "$Name wrong DLL path"
    Assert ($key.GetValue('ThreadingModel') -eq 'Apartment') "$Name wrong threading model"
    $key.Dispose()

    $refused = $false
    try { & (Join-Path $tools 'Register-Dev.ps1') -DllPath $DllPath -StatePath $state } catch { $refused = $true }
    Assert $refused "$Name second registration was not refused"

    & (Join-Path $tools 'Unregister-Dev.ps1') -StatePath $state -WhatIf
    Assert (Test-Path $state) "$Name -WhatIf consumed the state file"

    & (Join-Path $tools 'Unregister-Dev.ps1') -StatePath $state
    $diff = @(Compare-Snapshot $before (Get-Snapshot))
    Assert ($diff.Count -eq 0) "$Name registry differs after unregister: $($diff -join '; ')"
    Assert (-not (Test-Path $state)) "$Name state file was not retired"
    $restored = @(Get-ChildItem -LiteralPath (Split-Path -Parent $state) -Filter "$stateName.restored-*.json")
    Assert ($restored.Count -eq 1) "$Name expected one retired state file, found $($restored.Count)"
    $restored | Remove-Item
    Write-Host "OK: $Name"
}

# 1. Nothing registered for .ply in HKCU (the usual case).
$plyExisted = $null -ne $hkcu.OpenSubKey('Software\Classes\.ply')
if (-not $plyExisted) {
    Test-RoundTrip 'clean'
    Assert ($null -eq $hkcu.OpenSubKey('Software\Classes\.ply')) 'clean: .ply key left behind'
}

# 2. Another application's handler, with extra values of several types.
$key = $hkcu.CreateSubKey($handlerKey)
$key.SetValue('', '{11111111-2222-3333-4444-555555555555}')
$key.SetValue('Number', 5, [Microsoft.Win32.RegistryValueKind]::DWord)
$key.SetValue('Bytes', [byte[]] (1, 2, 3), [Microsoft.Win32.RegistryValueKind]::Binary)
$key.SetValue('Lines', [string[]] ('a', 'b'), [Microsoft.Win32.RegistryValueKind]::MultiString)
$key.Dispose()
try {
    Test-RoundTrip 'existing handler'
    $key = $hkcu.OpenSubKey($handlerKey)
    Assert ($key.GetValue('') -eq '{11111111-2222-3333-4444-555555555555}') 'existing handler: not restored'
    Assert (($key.GetValue('Bytes') -join ',') -eq '1,2,3') 'existing handler: binary value changed'
    $key.Dispose()
} finally {
    if (-not $plyExisted) { $hkcu.DeleteSubKeyTree('Software\Classes\.ply', $false) }
    else { $hkcu.DeleteSubKeyTree($handlerKey, $false) }
}

# 3. Unregister without a state file changes nothing.
$failed = $false
try { & (Join-Path $tools 'Unregister-Dev.ps1') -StatePath $state } catch { $failed = $true }
Assert $failed 'unregister without a state file did not stop'
Write-Host 'Development registration scripts: all checks passed.'
