param(
    [string]$IdfPath = $env:IDF_PATH,
    [ValidateSet('build', 'menuconfig', 'size', 'reconfigure')]
    [string]$Action = 'build'
)
$ErrorActionPreference = 'Stop'
if (-not $IdfPath -or -not (Test-Path -LiteralPath (Join-Path $IdfPath 'export.ps1'))) {
    throw 'Pass -IdfPath pointing to an ESP-IDF 5.5.x installation, or set IDF_PATH.'
}
. (Join-Path $IdfPath 'export.ps1')
if ((idf.py --version) -notmatch 'ESP-IDF v5\.5(?:\.|$|-)') {
    throw 'This project requires ESP-IDF 5.5.x.'
}
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    idf.py $Action
    if ($LASTEXITCODE -ne 0) { throw "idf.py $Action failed ($LASTEXITCODE)" }
} finally { Pop-Location }
