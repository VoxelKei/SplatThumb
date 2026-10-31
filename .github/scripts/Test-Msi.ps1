# Installs, checks and uninstalls the MSI on the (throwaway) CI machine. After every
# uninstall, the registry keys and folders the MSI touches must be exactly as before.
# Also covers installing twice, uninstalling twice, upgrading and refusing a downgrade.
param(
    [Parameter(Mandatory)] [string] $MsiPath,
    [Parameter(Mandatory)] [string] $DllPath,
    [Parameter(Mandatory)] [string] $LogDir
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
    $keys = $ManagedKeys + @('Software\SplatThumb')
    $registry = @(foreach ($k in $keys) { Get-KeySnapshot $k })
    $folders = [pscustomobject]@{
        Programs = Test-Path -LiteralPath $programsDir
        Install = Test-Path -LiteralPath $installDir
        Files = @(if (Test-Path -LiteralPath $installDir) {
                Get-ChildItem -LiteralPath $installDir -Recurse -Force | ForEach-Object { $_.FullName } })
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

function Install-Msi([string] $Path, [int] $Expect = 0) {
    $code = Invoke-Msiexec 'install' @('/i', "`"$Path`"")
    Assert ($code -eq $Expect) "install of $Path returned $code, expected $Expect"
}

function Uninstall-Msi([string] $Path, [int] $Expect = 0) {
    $code = Invoke-Msiexec 'uninstall' @('/x', "`"$Path`"")
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

# New files each time, so a thumbnail cached by an earlier round cannot hide a failure.
function Test-Thumbnails {
    $id = [guid]::NewGuid().ToString('N')
    $splatPly = Join-Path $work "splat-$id.ply"
    $meshPly = Join-Path $work "mesh-$id.ply"
    New-TestPly $splatPly
    New-TestPly $meshPly -Mesh
    $shell = (Get-Process -Id $PID).Path
    & $shell -NoProfile -File (Join-Path $PSScriptRoot 'Get-Thumbnail.ps1') -Path $splatPly
    Assert ($LASTEXITCODE -eq 0) 'no thumbnail for the 3DGS PLY'
    & $shell -NoProfile -File (Join-Path $PSScriptRoot 'Get-Thumbnail.ps1') -Path $meshPly
    Assert ($LASTEXITCODE -ne 0) 'the mesh PLY got a thumbnail'
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
            -d "ShellExtDll=$((Resolve-Path $DllPath).ProviderPath)" -d "LicenseFile=$repo\LICENSE" `
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

    Install-Msi $MsiPath  # again: a repair, not a second copy
    Assert-Installed $Version

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
    Install-Msi $newer
    Assert-Installed $NewerVersion
    Install-Msi $MsiPath -Expect 1603  # downgrade refused
    Assert-Installed $NewerVersion

    Uninstall-Msi $newer
    Assert-NotInstalled
    $after = Get-State
    Assert ($after -eq $before) "upgrade: state differs after uninstall.`nBefore:`n$before`nAfter:`n$after"
    Write-Host 'OK: upgrade'
}

try {
    $plyExisted = $null -ne $hkcu.OpenSubKey('Software\Classes\.ply')
    if (-not $plyExisted) {
        Test-RoundTrip 'no .ply key'
        Assert ($null -eq $hkcu.OpenSubKey('Software\Classes\.ply')) '.ply key left behind'
        # An empty .ply key, as other applications often leave.
        $hkcu.CreateSubKey('Software\Classes\.ply').Dispose()
    }
    Test-RoundTrip 'existing .ply key'
    Assert ($null -ne $hkcu.OpenSubKey('Software\Classes\.ply')) 'existing .ply key was removed'
    Test-Upgrade
} finally {
    if (-not $plyExisted) { $hkcu.DeleteSubKeyTree('Software\Classes\.ply', $false) }
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host 'MSI: all checks passed.'
