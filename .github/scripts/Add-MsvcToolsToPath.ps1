# Puts the MSVC tools folder (dumpbin.exe, ASan runtime DLLs) on PATH for later steps.
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio with MSVC was not found.' }
$version = (Get-Content (Join-Path $vs 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt')).Trim()
$bin = Join-Path $vs "VC\Tools\MSVC\$version\bin\Hostx64\x64"
if (-not (Test-Path (Join-Path $bin 'dumpbin.exe'))) { throw "dumpbin.exe not found in $bin" }
Add-Content -Path $env:GITHUB_PATH -Value $bin
Write-Host "Added $bin"
