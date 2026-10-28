param(
    [string]$QtRoot = '',
    [string]$BuildDir = '',
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [switch]$Fresh,
    [switch]$Test
)

$ErrorActionPreference = 'Stop'
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$workRoot = Join-Path $projectRoot 'work'
[System.IO.Directory]::CreateDirectory($workRoot) | Out-Null
$buildLog = Join-Path $workRoot 'build.log'
[System.IO.File]::WriteAllText($buildLog, '', (New-Object System.Text.UTF8Encoding($false)))

function Invoke-NativeLogged {
    param([string]$FilePath, [string[]]$Arguments)
    $previousPreference = $ErrorActionPreference
    $writer = New-Object System.IO.StreamWriter($buildLog, $true, (New-Object System.Text.UTF8Encoding($false)))
    $printed = 0
    try {
        $ErrorActionPreference = 'Continue'
        & $FilePath @Arguments 2>&1 | ForEach-Object {
            $message = $_.ToString()
            $writer.WriteLine($message)
            if ($printed -lt 70 -and $message -match '^\[\d+/\d+\]|^-- (Configuring|Generating|Build files)|error [A-Z]\d+|CMake Error|^FAILED:|warning [A-Z]\d+|tests passed|tests failed') {
                Write-Host $message
                $printed++
            }
        }
        $commandExitCode = $LASTEXITCODE
    }
    finally {
        $writer.Dispose()
        $ErrorActionPreference = $previousPreference
    }
    if ($commandExitCode -ne 0) {
        Get-Content -LiteralPath $buildLog -Tail 35 | ForEach-Object { Write-Host $_ }
        throw "Command failed ($commandExitCode). Full log: $buildLog"
    }
}
if (-not $QtRoot) { $QtRoot = Join-Path $projectRoot '.tools\Qt\6.8.3\msvc2022_64' }
if (-not $BuildDir) { $BuildDir = Join-Path $projectRoot 'build' }
if (-not (Test-Path -LiteralPath (Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    throw 'Qt SDK not found. Run scripts\setup-qt.ps1, or pass -QtRoot with a Qt MSVC x64 SDK.'
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio C++ Build Tools are required.' }
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'No Visual Studio installation with C++ tools was found.' }
$devCommand = Join-Path $visualStudio 'Common7\Tools\VsDevCmd.bat'
$processInfo = New-Object System.Diagnostics.ProcessStartInfo
$processInfo.FileName = $env:ComSpec
$processInfo.Arguments = '/d /v:off /s /c ""' + $devCommand + '" -arch=x64 -host_arch=x64 >nul && set"'
$processInfo.UseShellExecute = $false
$processInfo.CreateNoWindow = $true
$processInfo.RedirectStandardOutput = $true
$processInfo.RedirectStandardError = $true
$environmentProcess = [System.Diagnostics.Process]::Start($processInfo)
$devEnvironment = $environmentProcess.StandardOutput.ReadToEnd() -split "`r?`n"
$environmentError = $environmentProcess.StandardError.ReadToEnd()
$environmentProcess.WaitForExit()
if ($environmentProcess.ExitCode -ne 0) { throw "Could not initialize the MSVC x64 environment: $environmentError" }
$environmentProcess.Dispose()
foreach ($line in $devEnvironment) {
    if ($line -match '^([^=]+)=(.*)$') {
        [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
# English compiler diagnostics avoid dependency-prefix encoding problems on localized Windows.
$env:VSLANG = '1033'

$bundledTools = Join-Path $visualStudio 'Common7\IDE\CommonExtensions\Microsoft\CMake'
$cmake = Join-Path $bundledTools 'CMake\bin\cmake.exe'
$ninja = Join-Path $bundledTools 'Ninja\ninja.exe'
if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake.exe -ErrorAction Stop).Source }
if (-not (Test-Path -LiteralPath $ninja)) { $ninja = (Get-Command ninja.exe -ErrorAction Stop).Source }

Write-Host "Configuring OmniLauncher ($Configuration)..."
$configureArguments = @('-S', $projectRoot, '-B', $BuildDir, '-G', 'Ninja', "-DCMAKE_BUILD_TYPE=$Configuration", "-DCMAKE_PREFIX_PATH=$QtRoot", "-DCMAKE_MAKE_PROGRAM=$ninja", '-DCMAKE_CXX_COMPILER=cl.exe')
if ($Test) { $configureArguments += '-DOMNI_BUILD_TESTS=ON' } else { $configureArguments += '-DOMNI_BUILD_TESTS=OFF' }
if ($Fresh) { $configureArguments += '--fresh' }
Invoke-NativeLogged -FilePath $cmake -Arguments $configureArguments

# Ninja compares /showIncludes using raw bytes. Some localized MSVC installations
# have no English resources and CMake can decode the prefix with the wrong codepage.
# Probe the actual compiler output and preserve its exact bytes in the generated rule.
$probeHeader = Join-Path $workRoot 'omni_deps_probe.h'
$probeSource = Join-Path $workRoot 'omni_deps_probe.cpp'
$probeObject = Join-Path $workRoot 'omni_deps_probe.obj'
[System.IO.File]::WriteAllText($probeHeader, "#pragma once`n", [System.Text.Encoding]::ASCII)
[System.IO.File]::WriteAllText($probeSource, "#include `"omni_deps_probe.h`"`n", [System.Text.Encoding]::ASCII)
$compiler = (Get-Command cl.exe -ErrorAction Stop).Source
$probeInfo = New-Object System.Diagnostics.ProcessStartInfo
$probeInfo.FileName = $compiler
$probeInfo.Arguments = '/nologo /showIncludes /c "' + $probeSource + '" /Fo"' + $probeObject + '"'
$probeInfo.UseShellExecute = $false
# Inherit the same console as Ninja. CREATE_NO_WINDOW changes the compiler's
# fallback codepage and would produce a different localized prefix.
$probeInfo.CreateNoWindow = $false
$probeInfo.RedirectStandardOutput = $true
$probeInfo.RedirectStandardError = $true
$probeProcess = [System.Diagnostics.Process]::Start($probeInfo)
$probeBytes = New-Object System.IO.MemoryStream
$probeProcess.StandardOutput.BaseStream.CopyTo($probeBytes)
$probeError = $probeProcess.StandardError.ReadToEnd()
$probeProcess.WaitForExit()
if ($probeProcess.ExitCode -ne 0) { throw "MSVC dependency probe failed: $probeError" }
$byteEncoding = [System.Text.Encoding]::GetEncoding(28591)
$rawOutput = $byteEncoding.GetString($probeBytes.ToArray())
$probeProcess.Dispose()
$probeBytes.Dispose()
$headerPosition = $rawOutput.IndexOf($probeHeader, [System.StringComparison]::OrdinalIgnoreCase)
if ($headerPosition -lt 0) { throw 'Could not detect the compiler dependency prefix.' }
$lineStart = $rawOutput.LastIndexOf("`n", $headerPosition) + 1
$rawPrefix = $rawOutput.Substring($lineStart, $headerPosition - $lineStart)
$rulesFile = Join-Path $BuildDir 'CMakeFiles\rules.ninja'
$rules = $byteEncoding.GetString([System.IO.File]::ReadAllBytes($rulesFile))
$rules = [System.Text.RegularExpressions.Regex]::Replace($rules, '(?m)^msvc_deps_prefix = [^\r\n]*', ('msvc_deps_prefix = ' + $rawPrefix))
[System.IO.File]::WriteAllBytes($rulesFile, $byteEncoding.GetBytes($rules))

Invoke-NativeLogged -FilePath $cmake -Arguments @('--build', $BuildDir, '--parallel', '4')
if ($Test) {
    $env:PATH = "$(Join-Path $QtRoot 'bin');$env:PATH"
    $ctest = Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
    Invoke-NativeLogged -FilePath $ctest -Arguments @('--test-dir', $BuildDir, '--output-on-failure')
}
Write-Host "Built: $(Join-Path $BuildDir 'OmniLauncher.exe')"
Write-Host "Full build log: $buildLog"
