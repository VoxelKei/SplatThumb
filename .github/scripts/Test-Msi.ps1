# Installs, checks and uninstalls the MSI on the (throwaway) CI machine. After every
# uninstall, the registry keys and folders the MSI touches must be exactly as before.
# Also covers installing twice, uninstalling twice, upgrading (keeping the user's settings
# and saved views), refusing a downgrade, another application's handler and right-click
# items being kept, rollback of a failed install or uninstall, and, with -PreviousMsiPath,
# upgrading from the previous release. .ply and .spgm (Spatialograph Maker projects) are
# both registered; only .ply gets the right-click item.
param(
    [Parameter(Mandatory)] [string] $MsiPath,
    [Parameter(Mandatory)] [string] $DllPath,
    [Parameter(Mandatory)] [string] $LogDir,
    [string] $PreviousMsiPath = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

function Assert($Condition, [string] $Message) {
    if (-not $Condition) { throw "FAILED: $Message" }
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).ProviderPath
. (Join-Path $repo 'tools\dev\DevRegistration.Common.ps1')

$UpgradeCode = '{512DFF52-226B-42AE-AB47-C4C0A71D1B52}'
$hkcu = [Microsoft.Win32.Registry]::CurrentUser
$programsDir = Join-Path $env:LOCALAPPDATA 'Programs'
$installDir = Join-Path $programsDir 'SplatThumb'
$installedDll = Join-Path $installDir 'splatthumb_shellext.dll'
$installedExe = Join-Path $installDir 'SplatThumb.exe'
$shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'SplatThumb.lnk'
# What SplatThumb.exe saves for the user, which uninstalling (but not upgrading) removes.
$dataDir = Join-Path $env:LOCALAPPDATA 'SplatThumb'
$settingsKey = 'Software\SplatThumb\Settings'
$associationsKey = 'Software\Classes\SystemFileAssociations'
$verbKey = "$associationsKey\.ply\shell\SplatThumb.SetView"
$spgmKeys = @('Software\Classes\.spgm', 'Software\Classes\.spgm\ShellEx',
    "Software\Classes\.spgm\ShellEx\$ThumbnailHandlerShellEx", 'Software\SplatThumb\Previous.spgm',
    "$associationsKey\.spgm", "$associationsKey\.spgm\shell",
    "$associationsKey\.spgm\shell\SplatThumb.SetView")
$work = Join-Path ([System.IO.Path]::GetTempPath()) "splatthumb-msi-ci-$PID"
New-Item -ItemType Directory -Force -Path $work, $LogDir | Out-Null
$MsiPath = (Resolve-Path $MsiPath).ProviderPath

# The version the MSI was built with, and a newer one for the upgrade test.
$cmake = Get-Content -Raw (Join-Path $repo 'CMakeLists.txt')
Assert ($cmake -match 'project\(SplatThumb VERSION (\d+)\.(\d+)\.(\d+)') 'version not found in CMakeLists.txt'
$Version = "$($Matches[1]).$($Matches[2]).$($Matches[3])"
$NewerVersion = "$($Matches[1]).$($Matches[2]).$([int] $Matches[3] + 1)"
$script:logCount = 0

# Everything the MSI may touch, as one string, so "exactly as before" is a string compare.
function Get-State {
    $keys = $ManagedKeys + @('Software\SplatThumb', $settingsKey, 'Software\SplatThumb\Previous',
        $associationsKey, "$associationsKey\.ply", "$associationsKey\.ply\shell",
        "$associationsKey\.ply\shell\Other", "$associationsKey\.ply\shell\Other\command",
        $verbKey, "$verbKey\command") + $spgmKeys
    $registry = @(foreach ($k in $keys) { Get-KeySnapshot $k })
    $folders = [pscustomobject]@{
        Programs = Test-Path -LiteralPath $programsDir
        Install = Test-Path -LiteralPath $installDir
        Files = @(if (Test-Path -LiteralPath $installDir) {
                Get-ChildItem -LiteralPath $installDir -Recurse -Force | ForEach-Object { $_.FullName } })
        Shortcut = Test-Path -LiteralPath $shortcut
        Data = Test-Path -LiteralPath $dataDir
        DataFiles = @(if (Test-Path -LiteralPath $dataDir) {
                Get-ChildItem -LiteralPath $dataDir -Recurse -Force | ForEach-Object { $_.FullName } })
    }
    [pscustomobject]@{ Registry = $registry; Folders = $folders } | ConvertTo-Json -Depth 6
}

function Invoke-Msiexec([string] $Name, [string[]] $Arguments) {
    $script:logCount++
    $log = Join-Path $LogDir ('{0:D2}-{1}.log' -f $script:logCount, $Name)
    $p = Start-Process msiexec.exe -Wait -PassThru -ArgumentList ($Arguments + @('/qn', '/norestart', '/l*v', "`"$log`""))
    Write-Host "msiexec $Name -> $($p.ExitCode)"
    $p.ExitCode
}

function Install-Msi([string] $Path, [int] $Expect = 0, [string[]] $Properties = @()) {
    $code = Invoke-Msiexec 'install' (@('/i', "`"$Path`"") + $Properties)
    Assert ($code -eq $Expect) "install of $Path returned $code, expected $Expect"
}

function Uninstall-Msi([string] $Path, [int] $Expect = 0, [string[]] $Properties = @()) {
    $code = Invoke-Msiexec 'uninstall' (@('/x', "`"$Path`"") + $Properties)
    Assert ($code -eq $Expect) "uninstall of $Path returned $code, expected $Expect"
}

function Get-InstalledVersions {
    $installer = New-Object -ComObject WindowsInstaller.Installer
    $type = $installer.GetType()
    $list = $type.InvokeMember('RelatedProducts', 'GetProperty', $null, $installer, @($UpgradeCode))
    $count = $list.GetType().InvokeMember('Count', 'GetProperty', $null, $list, $null)
    @(for ($i = 0; $i -lt $count; $i++) {
            $code = $list.GetType().InvokeMember('Item', 'GetProperty', $null, $list, @($i))
            $type.InvokeMember('ProductInfo', 'GetProperty', $null, $installer, @($code, 'VersionString'))
        })
}

function Assert-Installed([string] $Version) {
    $versions = @(Get-InstalledVersions)
    Assert ($versions.Count -eq 1) "expected one installed SplatThumb, found $($versions.Count) ($($versions -join ', '))"
    Assert ($versions[0] -eq $Version) "expected version $Version installed, found $($versions[0])"
    Assert (Test-Path -LiteralPath $installedDll) 'DLL not installed'
    Assert (Test-Path -LiteralPath (Join-Path $installDir 'LICENSE.txt')) 'LICENSE.txt not installed'

    $key = $hkcu.OpenSubKey("Software\Classes\.ply\ShellEx\$ThumbnailHandlerShellEx")
    Assert ($null -ne $key -and $key.GetValue('') -eq $SplatThumbClsid) 'thumbnail handler not registered'
    $key.Dispose()
    $key = $hkcu.OpenSubKey("Software\Classes\.spgm\ShellEx\$ThumbnailHandlerShellEx")
    Assert ($null -ne $key -and $key.GetValue('') -eq $SplatThumbClsid) '.spgm thumbnail handler not registered'
    $key.Dispose()
    Assert ($null -eq $hkcu.OpenSubKey("$associationsKey\.spgm")) '.spgm got a right-click item'
    $key = $hkcu.OpenSubKey("Software\Classes\CLSID\$SplatThumbClsid")
    Assert ($key.GetValue('') -eq 'SplatThumb Thumbnail Handler') 'CLSID name not set'
    $key.Dispose()
    $key = $hkcu.OpenSubKey("Software\Classes\CLSID\$SplatThumbClsid\InprocServer32")
    Assert ($key.GetValue('') -eq $installedDll) "InprocServer32 is $($key.GetValue('')), expected $installedDll"
    Assert ($key.GetValue('ThreadingModel') -eq 'Apartment') 'wrong threading model'
    Assert (@($key.GetValueNames()).Count -eq 2) 'unexpected values under InprocServer32'
    $key.Dispose()
    $key = $hkcu.OpenSubKey("Software\Classes\CLSID\$SplatThumbClsid")
    Assert ($null -eq $key.GetValue('DisableProcessIsolation')) 'process isolation disabled'
    $key.Dispose()

    Assert (Test-Path -LiteralPath $installedExe) 'SplatThumb.exe not installed'
    Assert (Test-Path -LiteralPath $shortcut) "Start menu shortcut not found at $shortcut"
    $key = $hkcu.OpenSubKey($verbKey)
    Assert ($null -ne $key) 'right-click item not registered'
    Assert ($key.GetValue('MUIVerb') -eq "@$installedExe,-102") "MUIVerb is $($key.GetValue('MUIVerb'))"
    Assert ($key.GetValue('Icon') -eq "$installedExe,-1") "right-click icon is $($key.GetValue('Icon'))"
    $key.Dispose()
    $key = $hkcu.OpenSubKey("$verbKey\command")
    $expected = "`"$installedExe`" --view `"%1`""
    Assert ($key.GetValue('') -eq $expected) "right-click command is $($key.GetValue('')), expected $expected"
    $key.Dispose()
}

# What using SplatThumb.exe leaves: a setting and a saved view.
function New-UserData {
    $key = $hkcu.CreateSubKey($settingsKey)
    $key.SetValue('Background', 2, [Microsoft.Win32.RegistryValueKind]::DWord)
    $key.Dispose()
    $folder = Join-Path $dataDir 'views\ab'
    New-Item -ItemType Directory -Force -Path $folder | Out-Null
    $view = "splatthumb-view 1`neye 0 0 -5`ntarget 0 0 0`nup 0 -1 0`nfov 40`nradius 1`n"
    [System.IO.File]::WriteAllText((Join-Path $folder 'ab000000000000000000000000000000.view'), $view)
}

function Test-UserDataKept {
    $key = $hkcu.OpenSubKey($settingsKey)
    Assert ($null -ne $key -and $key.GetValue('Background') -eq 2) 'settings were not kept'
    $key.Dispose()
    Assert (Test-Path -LiteralPath (Join-Path $dataDir 'views\ab\ab000000000000000000000000000000.view')) 'saved view was not kept'
}

function Assert-NotInstalled {
    $versions = @(Get-InstalledVersions)
    Assert ($versions.Count -eq 0) "SplatThumb still installed: $($versions -join ', ')"
}

# A small synthetic 3D Gaussian Splatting PLY (10 x 10 grid) and a mesh PLY.
function New-TestPly([string] $Path, [switch] $Mesh) {
    if ($Mesh) {
        $text = "ply`nformat ascii 1.0`nelement vertex 3`nproperty float x`nproperty float y`nproperty float z`n" +
        "element face 1`nproperty list uchar int vertex_indices`nend_header`n0 0 0`n1 0 0`n0 1 0`n3 0 1 2`n"
        [System.IO.File]::WriteAllText($Path, $text)
        return
    }
    $props = 'x', 'y', 'z', 'f_dc_0', 'f_dc_1', 'f_dc_2', 'opacity', 'scale_0', 'scale_1', 'scale_2',
    'rot_0', 'rot_1', 'rot_2', 'rot_3'
    $header = "ply`nformat binary_little_endian 1.0`nelement vertex 100`n" +
    (($props | ForEach-Object { "property float $_`n" }) -join '') + "end_header`n"
    $stream = [System.IO.File]::Create($Path)
    $writer = New-Object System.IO.BinaryWriter $stream
    try {
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes($header))
        for ($i = 0; $i -lt 100; $i++) {
            $x = ($i % 10) * 0.1; $y = [math]::Floor($i / 10) * 0.1
            foreach ($v in @($x, $y, 0, 1.5, (($i % 10) / 5 - 1), -1, 5, -3, -3, -3, 1, 0, 0, 0)) {
                $writer.Write([single] $v)
            }
        }
    } finally {
        $writer.Dispose()
    }
}

# A Spatialograph Maker project (.spgm). Version 3 carries a preview image (a 32 x 32 PNG
# here); older versions carry none, so they get no thumbnail.
function New-TestSpgm([string] $Path, [int] $Version = 3) {
    $png = [Convert]::FromBase64String('iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAIAAAD8GO2jAAAAKklEQVR42mPQqDhBU8QwasGoBaMWjFowasGoBaMWjFowasGoBaMWDBULAIuXoExJIezsAAAAAElFTkSuQmCC')
    $json = [System.Text.Encoding]::UTF8.GetBytes('{"version":' + $Version + ',"hasSplatData":false}')
    $stream = [System.IO.File]::Create($Path)
    $writer = New-Object System.IO.BinaryWriter $stream
    try {
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes("SGP`0"))
        $writer.Write([uint32] $Version)
        if ($Version -ge 3) {
            $writer.Write([uint32] $png.Length)
            $writer.Write($png)
        }
        $writer.Write([uint32] $json.Length)
        $writer.Write($json)
    } finally {
        $writer.Dispose()
    }
}

# New files each time, so a thumbnail cached by an earlier round cannot hide a failure.
# COM ignores per-user (HKCU) class registrations in elevated processes, and CI runs
# elevated. So the check runs in a non-elevated "basic user" process, like Explorer.
function Get-ShellThumbnail([string] $Path) {
    $shell = (Get-Process -Id $PID).Path
    $script = Join-Path $PSScriptRoot 'Get-Thumbnail.ps1'
    $principal = [Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        & $shell -NoProfile -STA -File $script -Path $Path
        return $LASTEXITCODE -eq 0
    }
    $out = "$Path.result.txt"
    $command = "`"$shell`" -NoProfile -NonInteractive -STA -File `"$script`" -Path `"$Path`" -ResultPath `"$out`""
    Start-Process runas.exe -Wait -WindowStyle Hidden -ArgumentList "/trustlevel:0x20000 `"$($command -replace '"', '\"')`""
    $deadline = (Get-Date).AddSeconds(60)
    while (-not (Test-Path -LiteralPath $out) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
    Assert (Test-Path -LiteralPath $out) "no result from the non-elevated thumbnail check for $Path"
    Start-Sleep -Milliseconds 500  # let the writer finish
    $result = (Get-Content -LiteralPath $out -Raw).Trim()
    Write-Host "Thumbnail of ${Path} (non-elevated): $result"
    $null, $width, $null = $result -split ' '
    [int] $width -gt 0
}

# New files each time, so a thumbnail cached by an earlier round cannot hide a failure.
function Test-Thumbnails {
    $id = [guid]::NewGuid().ToString('N')
    $splatPly = Join-Path $work "splat-$id.ply"
    $meshPly = Join-Path $work "mesh-$id.ply"
    New-TestPly $splatPly
    New-TestPly $meshPly -Mesh
    Assert (Get-ShellThumbnail $splatPly) 'no thumbnail for the 3DGS PLY'
    Assert (-not (Get-ShellThumbnail $meshPly)) 'the mesh PLY got a thumbnail'
    $spgm = Join-Path $work "project-$id.spgm"
    $oldSpgm = Join-Path $work "old-project-$id.spgm"
    New-TestSpgm $spgm
    New-TestSpgm $oldSpgm -Version 2
    Assert (Get-ShellThumbnail $spgm) 'no thumbnail for the .spgm with a preview image'
    Assert (-not (Get-ShellThumbnail $oldSpgm)) 'the .spgm without a preview image got a thumbnail'
}

# The shell keeps the handler loaded in dllhost.exe for a while after a thumbnail. Report
# how long, and only on this CI machine end the process if it outlives the wait, so the
# uninstall below tests the files, not the timing.
function Wait-DllReleased([int] $Seconds = 90) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    $start = Get-Date
    while ($true) {
        $users = @(Get-Process | Where-Object {
                try { $_.Modules | Where-Object { $_.FileName -eq $installedDll } } catch { $null }
            })
        if ($users.Count -eq 0) {
            Write-Host ("DLL not loaded by any process (after {0:N0} s)" -f ((Get-Date) - $start).TotalSeconds)
            return
        }
        if ((Get-Date) -gt $deadline) {
            Write-Host "DLL still loaded after $Seconds s by: $(($users | ForEach-Object { "$($_.Name) ($($_.Id))" }) -join ', '); stopping them"
            $users | Stop-Process -Force
            Start-Sleep -Seconds 2
            return
        }
        Start-Sleep -Seconds 2
    }
}

function Build-OtherVersion([string] $Version) {
    $out = Join-Path $work "SplatThumb-$Version-x64.msi"
    Push-Location $repo
    try {
        & dotnet tool run wix build -arch x64 -d "Version=$Version" `
            -d "ShellExtDll=$((Resolve-Path $DllPath).ProviderPath)" `
            -d "AppExe=$(Join-Path (Split-Path (Resolve-Path $DllPath).ProviderPath) 'SplatThumb.exe')" `
            -d "CustomActionDll=$(Join-Path (Split-Path (Resolve-Path $DllPath).ProviderPath) 'splatthumb_ca.dll')" `
            -d "LicenseFile=$repo\LICENSE" `
            -d "AppIcon=$repo\app\icon\splatthumb.ico" `
            -o $out installer\Package.wxs
        Assert ($LASTEXITCODE -eq 0) "wix build of $Version failed"
    } finally {
        Pop-Location
    }
    $out
}

function Test-RoundTrip([string] $Name) {
    Write-Host "== $Name"
    Assert-NotInstalled
    $before = Get-State

    Install-Msi $MsiPath
    Assert-Installed $Version
    Test-Thumbnails
    New-UserData

    Install-Msi $MsiPath  # again: a repair, not a second copy
    Assert-Installed $Version
    Test-UserDataKept

    Wait-DllReleased
    Uninstall-Msi $MsiPath
    Assert-NotInstalled
    $after = Get-State
    Assert ($after -eq $before) "$Name state differs after uninstall.`nBefore:`n$before`nAfter:`n$after"

    Uninstall-Msi $MsiPath -Expect 1605  # ERROR_UNKNOWN_PRODUCT: nothing to do
    Assert ((Get-State) -eq $before) "$Name second uninstall changed something"
    Write-Host "OK: $Name"
}

function Test-Upgrade {
    Write-Host '== upgrade'
    $newer = Build-OtherVersion $NewerVersion
    $before = Get-State

    Install-Msi $MsiPath
    Assert-Installed $Version
    New-UserData
    Install-Msi $newer
    Assert-Installed $NewerVersion
    Test-UserDataKept  # upgrading removes the older version, but not what the user saved
    Install-Msi $MsiPath -Expect 1603  # downgrade refused
    Assert-Installed $NewerVersion

    Uninstall-Msi $newer
    Assert-NotInstalled
    $after = Get-State
    Assert ($after -eq $before) "upgrade: state differs after uninstall.`nBefore:`n$before`nAfter:`n$after"
    Write-Host 'OK: upgrade'
}

function Test-Rollback([string] $Name) {
    Write-Host "== rollback ($Name)"
    $fail = @('SPLATTHUMB_FAIL_FOR_TEST=1')
    $before = Get-State
    Install-Msi $MsiPath -Expect 1603 -Properties $fail
    Assert-NotInstalled
    $after = Get-State
    Assert ($after -eq $before) "$Name failed install was not rolled back.`nBefore:`n$before`nAfter:`n$after"

    Install-Msi $MsiPath
    New-UserData
    $installed = Get-State
    Uninstall-Msi $MsiPath -Expect 1603 -Properties $fail
    Assert-Installed $Version
    Test-UserDataKept
    $after = Get-State
    Assert ($after -eq $installed) "$Name failed uninstall was not rolled back.`nInstalled:`n$installed`nAfter:`n$after"
    Uninstall-Msi $MsiPath
    $after = Get-State
    Assert ($after -eq $before) "$Name state differs after uninstall.`nBefore:`n$before`nAfter:`n$after"
    Write-Host "OK: rollback ($Name)"
}

# The previous release, installed and then upgraded to this build.
function Test-UpgradeFromPrevious {
    Write-Host "== upgrade from $PreviousMsiPath"
    $before = Get-State
    Install-Msi $PreviousMsiPath
    Assert (@(Get-InstalledVersions).Count -eq 1) 'previous release not installed'
    Install-Msi $MsiPath
    Assert-Installed $Version
    Test-Thumbnails
    Wait-DllReleased
    Uninstall-Msi $MsiPath
    Assert-NotInstalled
    $after = Get-State
    Assert ($after -eq $before) "upgrade from previous: state differs after uninstall.`nBefore:`n$before`nAfter:`n$after"
    Write-Host 'OK: upgrade from previous'
}

# Another application's right-click item for .ply files.
function Set-OtherVerb {
    $key = $hkcu.CreateSubKey("$associationsKey\.ply\shell\Other\command")
    $key.SetValue('', 'other.exe "%1"')
    $key.Dispose()
}

# Another application's handler, with values of several types.
function Set-OtherHandler([string] $Extension = '.ply') {
    $key = $hkcu.CreateSubKey("Software\Classes\$Extension\ShellEx\$ThumbnailHandlerShellEx")
    $key.SetValue('', '{11111111-2222-3333-4444-555555555555}')
    $key.SetValue('Number', 5, [Microsoft.Win32.RegistryValueKind]::DWord)
    $key.SetValue('Bytes', [byte[]] (1, 2, 3), [Microsoft.Win32.RegistryValueKind]::Binary)
    $key.SetValue('Lines', [string[]] ('a', 'b'), [Microsoft.Win32.RegistryValueKind]::MultiString)
    $key.Dispose()
}

try {
    $plyExisted = $null -ne $hkcu.OpenSubKey('Software\Classes\.ply')
    $spgmExisted = $null -ne $hkcu.OpenSubKey('Software\Classes\.spgm')
    if (-not $plyExisted) {
        Test-RoundTrip 'no .ply key'
        Assert ($null -eq $hkcu.OpenSubKey('Software\Classes\.ply')) '.ply key left behind'
        # An empty .ply key, as other applications often leave.
        $hkcu.CreateSubKey('Software\Classes\.ply').Dispose()
    }
    if (-not $spgmExisted) {
        Assert ($null -eq $hkcu.OpenSubKey('Software\Classes\.spgm')) '.spgm key left behind'
        $hkcu.CreateSubKey('Software\Classes\.spgm').Dispose()
    }
    Test-RoundTrip 'existing .ply key'
    Assert ($null -ne $hkcu.OpenSubKey('Software\Classes\.ply')) 'existing .ply key was removed'
    Assert ($null -ne $hkcu.OpenSubKey('Software\Classes\.spgm')) 'existing .spgm key was removed'
    Test-Upgrade
    if ($PreviousMsiPath) {
        $PreviousMsiPath = (Resolve-Path $PreviousMsiPath).ProviderPath
        Test-UpgradeFromPrevious
    }
    Test-Rollback 'existing .ply key'

    Set-OtherHandler
    Set-OtherHandler '.spgm'
    Set-OtherVerb
    Test-RoundTrip 'another handler'
    foreach ($ext in '.ply', '.spgm') {
        $key = $hkcu.OpenSubKey("Software\Classes\$ext\ShellEx\$ThumbnailHandlerShellEx")
        Assert ($key.GetValue('') -eq '{11111111-2222-3333-4444-555555555555}') "another $ext handler: not put back"
        $key.Dispose()
    }
    $key = $hkcu.OpenSubKey("$associationsKey\.ply\shell\Other\command")
    Assert ($null -ne $key -and $key.GetValue('') -eq 'other.exe "%1"') 'another right-click item: not kept'
    $key.Dispose()
    Test-Rollback 'another handler'
} finally {
    if (-not $plyExisted) { $hkcu.DeleteSubKeyTree('Software\Classes\.ply', $false) }
    else { $hkcu.DeleteSubKeyTree("Software\Classes\.ply\ShellEx\$ThumbnailHandlerShellEx", $false) }
    if (-not $spgmExisted) { $hkcu.DeleteSubKeyTree('Software\Classes\.spgm', $false) }
    else { $hkcu.DeleteSubKeyTree("Software\Classes\.spgm\ShellEx\$ThumbnailHandlerShellEx", $false) }
    $hkcu.DeleteSubKeyTree("$associationsKey\.ply\shell\Other", $false)
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host 'MSI: all checks passed.'
