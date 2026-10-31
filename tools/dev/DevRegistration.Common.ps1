# Shared by Register-Dev.ps1 and Unregister-Dev.ps1. Developer use only: the MSI is the
# supported way to install SplatThumb.
#
# Everything here works on HKEY_CURRENT_USER and needs no administrator rights. Other
# registrations (HKLM, other applications) are only read, never changed.

Set-StrictMode -Version 3.0

$script:ThumbnailHandlerShellEx = '{e357fccd-a995-4576-b01f-234630154e96}'
$script:SplatThumbClsid = '{09C68D16-D21F-4B95-97A1-29B45AEBF4CC}'

# Keys the registration may create, parents first. Unregister removes, in reverse order,
# only the ones that did not exist before.
$script:ParentKeys = @('Software\Classes\.ply', 'Software\Classes\.ply\ShellEx')
# Keys whose values the registration sets. Their values are saved and restored exactly.
$script:ValueKeys = @(
    "Software\Classes\.ply\ShellEx\$ThumbnailHandlerShellEx",
    "Software\Classes\CLSID\$SplatThumbClsid",
    "Software\Classes\CLSID\$SplatThumbClsid\InprocServer32"
)
$script:ManagedKeys = @(
    'Software\Classes\.ply',
    'Software\Classes\.ply\ShellEx',
    "Software\Classes\.ply\ShellEx\$ThumbnailHandlerShellEx",
    "Software\Classes\CLSID\$SplatThumbClsid",
    "Software\Classes\CLSID\$SplatThumbClsid\InprocServer32"
)

function Get-DefaultStatePath {
    Join-Path $env:LOCALAPPDATA 'SplatThumb-dev\registration-state.json'
}

# Existence and all values of one HKCU key, as a plain object that round-trips through JSON.
function Get-KeySnapshot([string] $SubKey) {
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($SubKey)
    if ($null -eq $key) {
        return [pscustomobject]@{ Path = $SubKey; Exists = $false; Values = @() }
    }
    try {
        $values = @(foreach ($name in ($key.GetValueNames() | Sort-Object)) {
                $kind = $key.GetValueKind($name)
                $raw = $key.GetValue($name, $null,
                    [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
                [pscustomobject]@{ Name = $name; Kind = $kind.ToString(); Data = ConvertTo-StoredData $kind $raw }
            })
        return [pscustomobject]@{ Path = $SubKey; Exists = $true; Values = $values }
    } finally {
        $key.Dispose()
    }
}

# Registry data as a string that survives JSON: binary as base64, multi-string joined by NUL.
function ConvertTo-StoredData([Microsoft.Win32.RegistryValueKind] $Kind, $Raw) {
    switch ($Kind) {
        'Binary' { return [Convert]::ToBase64String([byte[]] $Raw) }
        'MultiString' { return ([string[]] $Raw) -join [char] 0 }
        default { return [string] $Raw }
    }
}

function ConvertFrom-StoredData([Microsoft.Win32.RegistryValueKind] $Kind, [string] $Data) {
    switch ($Kind) {
        'Binary' { return , [Convert]::FromBase64String($Data) }
        'MultiString' { return , [string[]] $Data.Split([char] 0) }
        'DWord' { return [int] $Data }
        'QWord' { return [long] $Data }
        default { return $Data }
    }
}

function Get-Snapshot {
    @(foreach ($subKey in $script:ManagedKeys) { Get-KeySnapshot $subKey })
}

# Differences between two snapshots, as readable lines. Empty means identical.
function Compare-Snapshot($Before, $After) {
    $lines = @()
    for ($i = 0; $i -lt $Before.Count; $i++) {
        $b = $Before[$i]; $a = $After[$i]
        if ($b.Exists -ne $a.Exists) {
            $lines += "$($b.Path): existed before = $($b.Exists), exists now = $($a.Exists)"
            continue
        }
        if ($script:ValueKeys -notcontains $b.Path) {
            continue # parent keys: only their existence is ours to restore
        }
        $bv = ($b.Values | ForEach-Object { "$($_.Name)|$($_.Kind)|$($_.Data)" }) -join "`n"
        $av = ($a.Values | ForEach-Object { "$($_.Name)|$($_.Kind)|$($_.Data)" }) -join "`n"
        if ($bv -ne $av) {
            $lines += "$($b.Path): values differ (before: [$bv], now: [$av])"
        }
    }
    $lines
}

# Read-only look at what else could provide .ply thumbnails, for the record.
function Get-OtherRegistrations {
    $found = @()
    $places = @(
        @{ Hive = 'HKCU'; Root = [Microsoft.Win32.Registry]::CurrentUser; Path = "Software\Classes\.ply\ShellEx\$ThumbnailHandlerShellEx" },
        @{ Hive = 'HKLM'; Root = [Microsoft.Win32.Registry]::LocalMachine; Path = "Software\Classes\.ply\ShellEx\$ThumbnailHandlerShellEx" },
        @{ Hive = 'HKCU'; Root = [Microsoft.Win32.Registry]::CurrentUser; Path = "Software\Classes\SystemFileAssociations\.ply\ShellEx\$ThumbnailHandlerShellEx" },
        @{ Hive = 'HKLM'; Root = [Microsoft.Win32.Registry]::LocalMachine; Path = "Software\Classes\SystemFileAssociations\.ply\ShellEx\$ThumbnailHandlerShellEx" }
    )
    $progId = $null
    $ext = [Microsoft.Win32.Registry]::ClassesRoot.OpenSubKey('.ply')
    if ($null -ne $ext) {
        $progId = [string] $ext.GetValue('')
        $ext.Dispose()
    }
    if ($progId) {
        $places += @{ Hive = 'HKCR'; Root = [Microsoft.Win32.Registry]::ClassesRoot; Path = "$progId\ShellEx\$ThumbnailHandlerShellEx" }
    }
    foreach ($p in $places) {
        $key = $p.Root.OpenSubKey($p.Path)
        if ($null -ne $key) {
            $found += [pscustomobject]@{ Key = "$($p.Hive)\$($p.Path)"; Handler = [string] $key.GetValue('') }
            $key.Dispose()
        }
    }
    [pscustomobject]@{ PlyProgId = $progId; ThumbnailHandlers = $found }
}

# Tells Explorer that file associations changed, so it picks up the new handler.
function Send-AssociationChanged {
    if (-not ('SplatThumbDev.Shell' -as [type])) {
        Add-Type -Namespace SplatThumbDev -Name Shell -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("shell32.dll")]
public static extern void SHChangeNotify(int eventId, uint flags, System.IntPtr item1, System.IntPtr item2);
'@
    }
    # SHCNE_ASSOCCHANGED, SHCNF_IDLIST
    [SplatThumbDev.Shell]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
}
