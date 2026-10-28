param(
    [string]$QtRoot = '',
    [string]$BuildDir = '',
    [switch]$Build,
    [string]$Screenshot = '',
    [int]$QuitAfterMs = 0,
    [string]$DataDir = '',
    [string]$WindowSize = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $QtRoot) { $QtRoot = Join-Path $projectRoot '.tools\Qt\6.8.3\msvc2022_64' }
if (-not $BuildDir) { $BuildDir = Join-Path $projectRoot 'build' }
if ($Build) { & (Join-Path $PSScriptRoot 'build.ps1') -QtRoot $QtRoot -BuildDir $BuildDir }

$executable = Join-Path $BuildDir 'OmniLauncher.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw 'Build first with scripts\build.ps1, or use -Build.' }
if (-not (Test-Path -LiteralPath (Join-Path $QtRoot 'bin\Qt6Core.dll'))) { throw 'Qt runtime was not found.' }
$env:PATH = "$(Join-Path $QtRoot 'bin');$env:PATH"
$env:QT_PLUGIN_PATH = Join-Path $QtRoot 'plugins'
$env:QML_IMPORT_PATH = Join-Path $QtRoot 'qml'
$env:QT_FORCE_STDERR_LOGGING = '1'
$appArguments = @()
if ($Screenshot) { $appArguments += @('--screenshot', [System.IO.Path]::GetFullPath($Screenshot)) }
if ($QuitAfterMs -gt 0) { $appArguments += @('--quit-after', [string]$QuitAfterMs) }
if ($DataDir) { $appArguments += @('--data-dir', [System.IO.Path]::GetFullPath($DataDir)) }
if ($WindowSize) { $appArguments += @('--window-size', $WindowSize) }
$previousErrorPreference = $ErrorActionPreference
try {
    $ErrorActionPreference = 'Continue'
    & $executable @appArguments 2>&1 | ForEach-Object { Write-Host $_.ToString() }
    $processExitCode = $LASTEXITCODE
}
finally { $ErrorActionPreference = $previousErrorPreference }
if ($processExitCode -ne 0) { throw "OmniLauncher exited with code $processExitCode." }
