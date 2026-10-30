# Fails unless the DLL exports exactly the expected functions. In particular, it must not
# export DllRegisterServer: only the installer may register the handler.
param([Parameter(Mandatory)] [string] $Path)
$ErrorActionPreference = 'Stop'
$expected = @('DllCanUnloadNow', 'DllGetClassObject')

$output = & dumpbin.exe /nologo /exports $Path
if ($LASTEXITCODE -ne 0) { throw "dumpbin failed for $Path" }
$actual = @($output |
    Select-String -Pattern '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]{8}\s+(\S+)' |
    ForEach-Object { $_.Matches[0].Groups[1].Value } |
    Sort-Object)

if (Compare-Object $expected $actual) {
    throw "Unexpected exports in ${Path}: $($actual -join ', ') (expected: $($expected -join ', '))"
}
Write-Host "Exports OK: $($actual -join ', ')"
