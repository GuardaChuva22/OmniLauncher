param([string]$Python = 'python')

$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$pythonEnv = Join-Path $projectRoot '.tools\python'
$localPython = Join-Path $pythonEnv 'Scripts\python.exe'
$qtOutput = Join-Path $projectRoot '.tools\Qt'
$qtConfig = Join-Path $qtOutput '6.8.3\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake'
if (Test-Path -LiteralPath $qtConfig) {
    Write-Host 'The local Qt SDK is already present.'
    exit 0
}
if (-not (Test-Path -LiteralPath $localPython)) {
    & $Python -m venv $pythonEnv
    if ($LASTEXITCODE -ne 0) { throw 'Could not create the local Python environment.' }
}
& $localPython -m pip install 'aqtinstall==3.3.0'
if ($LASTEXITCODE -ne 0) { throw 'Could not install the Qt download helper.' }
Push-Location $projectRoot
try {
    & $localPython -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 --outputdir $qtOutput --archives qtbase qtdeclarative qtsvg d3dcompiler_47 --modules qtshadertools qtimageformats
    if ($LASTEXITCODE -ne 0) { throw 'Qt SDK download or extraction failed.' }
}
finally { Pop-Location }
Write-Host 'Local Qt SDK ready. Build with scripts\build.ps1.'
