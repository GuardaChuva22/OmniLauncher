param(
    [string]$QtRoot = '',
    [switch]$SkipBuild
)

# Builds a self-contained Windows package: dist\OmniLauncher\ and dist\OmniLauncher-<version>-win64.zip.
# Nothing needs to be installed on the target PC (Qt and the Visual C++ runtime sit next to the exe).
$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $QtRoot) { $QtRoot = Join-Path $projectRoot '.tools\Qt\6.8.3\msvc2022_64' }
$windeployqt = Join-Path $QtRoot 'bin\windeployqt.exe'
if (-not (Test-Path -LiteralPath $windeployqt)) { throw 'windeployqt was not found. Run scripts\setup-qt.ps1 first.' }

if (-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1') -QtRoot $QtRoot -Configuration Release -CMakeArgs '-DOMNI_AUTO_UPDATE=ON' }
$executable = Join-Path $projectRoot 'build\OmniLauncher.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw 'Build first with scripts\build.ps1.' }

$version = [regex]::Match((Get-Content -Raw (Join-Path $projectRoot 'CMakeLists.txt')), 'project\(OmniLauncher VERSION ([0-9.]+)').Groups[1].Value
$dist = Join-Path $projectRoot 'dist'
$stage = Join-Path $dist 'OmniLauncher'
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item -LiteralPath $executable -Destination $stage

Write-Host 'Collecting Qt libraries, plugins and QML modules...'
$previous = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $windeployqt --release --qmldir (Join-Path $projectRoot 'qml') --no-translations --no-opengl-sw --no-compiler-runtime (Join-Path $stage 'OmniLauncher.exe') 2>&1 | Out-Null
$deployExit = $LASTEXITCODE
$ErrorActionPreference = $previous
if ($deployExit -ne 0) { throw "windeployqt failed ($deployExit)." }

# The app forces the Basic Controls style (main.cpp); the other bundled styles are dead weight.
foreach ($style in 'Material', 'Universal', 'Fusion', 'Imagine', 'FluentWinUI3', 'Windows') {
    Remove-Item -LiteralPath (Join-Path $stage "qml\QtQuick\Controls\$style") -Recurse -Force -ErrorAction SilentlyContinue
    Get-ChildItem -Path $stage -Filter "Qt6QuickControls2$style*.dll" | Remove-Item -Force
}

# App-local Visual C++ runtime, so friends don't need the redistributable installer.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$crt = Get-ChildItem -Path (Join-Path $visualStudio 'VC\Redist\MSVC') -Directory |
    Sort-Object Name -Descending |
    ForEach-Object { Get-ChildItem -Path (Join-Path $_.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue } |
    Select-Object -First 1
if (-not $crt) { throw 'Visual C++ runtime redistributable folder not found.' }
Copy-Item -Path (Join-Path $crt.FullName '*.dll') -Destination $stage

# Bundled font license (SIL OFL requires it to travel with the font).
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets\fonts\OFL.txt') -Destination (Join-Path $stage 'Manrope-OFL.txt')

$zip = Join-Path $dist "OmniLauncher-$version-win64.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path $stage -DestinationPath $zip -CompressionLevel Optimal
$size = [math]::Round((Get-Item -LiteralPath $zip).Length / 1MB, 1)
Write-Host "Package: $zip ($size MB)"
