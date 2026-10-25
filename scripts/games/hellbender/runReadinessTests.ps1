# SPDX-License-Identifier: MIT

param([ValidateSet('Release', 'Debug')][string]$Configuration = 'Release')

# Owned in-process COM fixtures and a simulated clock. No game or display-mode changes.
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$testRoot = Join-Path $projectRoot 'out\test'
$buildRoot = Join-Path $testRoot 'hellbenderReadiness-msvc1444'
$runName = 'hellbenderReadiness-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$runRoot = Join-Path $testRoot $runName
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null

. (Join-Path $projectRoot 'scripts\core\nativeTools.ps1')
$processOptions = @{ WorkingDirectory = $projectRoot; LogDirectory = $runRoot; Environment = @{ PATHEXT = '.COM;.EXE;.BAT;.CMD' } }
$report = [ordered]@{
  format             = 'HellbenderDisplayReadinessValidation'
  version            = 1
  passed             = $false
  configuration      = $Configuration
  toolchain          = 'MSVC 14.44 x86'
  gameLaunched       = $false
  gameAttached       = $false
  realDisplayChanges = $false
  checks             = 0
  cases              = @()
  limitations        = @('Owned COM interfaces and simulated time validate control flow and ownership. Live Windows mode readiness still requires gameplay testing.')
}
$stage = 'source-identity'
$exitCode = 1

try {
  $report.sources = @(
    'scripts/games/hellbender/runReadinessTests.ps1', 'scripts/core/nativeTools.ps1',
    'tests/games/hellbender/CMakeLists.txt', 'tests/games/hellbender/displayReadinessTests.cpp',
    'include/games/hellbender/core/displayReadiness.hpp',
    'include/games/hellbender/core/layoutPayload.hpp',
    'include/games/hellbender/core/textureLifecycle.hpp',
    'include/games/hellbender/core/textureRecovery.hpp',
    'include/games/hellbender/core/textureLoadRecovery.hpp',
    'include/games/hellbender/core/textureUploadOwnership.hpp',
    'include/games/hellbender/core/textureTransfer.hpp',
    'out/patches/hellbender/english/gameAddresses.hpp'
  ) | ForEach-Object { [ordered]@{ path = $_; sha256 = (GetSha256 (Join-Path $projectRoot $_)) } }

  $stage = 'configure'
  $cmake = FindCMake
  $configured = InvokeNativeProcess @processOptions -Program $cmake -Arguments @(
    '-S', (Join-Path $projectRoot 'tests\games\hellbender'), '-B', $buildRoot,
    '-G', 'Visual Studio 17 2022', '-A', 'Win32', '-T', 'version=14.44',
    '-DHAV_BUILD_READINESS_TESTS=ON'
  ) -LogName 'configure' -Timeout 600000
  if ($configured.exitCode -ne 0) { throw 'Readiness fixture configuration failed.' }

  $stage = 'build'
  $built = InvokeNativeProcess @processOptions -Program $cmake -Arguments @(
    '--build', $buildRoot, '--config', $Configuration, '--target', 'displayReadinessTests'
  ) -LogName 'build' -Timeout 600000
  if ($built.exitCode -ne 0) { throw 'Readiness fixture compilation failed.' }

  $stage = 'fixture'
  $program = Join-Path $buildRoot ($Configuration + '\displayReadinessTests.exe')
  $native = InvokeNativeProcess @processOptions -Program $program -Arguments @() -LogName 'fixture' -Timeout 45000
  if ($native.exitCode -ne 0) { throw 'Readiness fixture failed.' }
  $result = $native.stdout | ConvertFrom-Json
  if ($result.passed -ne $true -or $result.checks -le 0 -or $result.cases.Count -le 0) {
    throw 'Readiness fixture returned an incomplete result.'
  }
  $report.checks = $result.checks
  $report.cases = $result.cases
  $report.fixtureSha256 = GetSha256 $program

  $stage = 'source-preservation'
  foreach ($source in $report.sources) {
    if ((GetSha256 (Join-Path $projectRoot $source.path)) -cne $source.sha256) {
      throw 'A fixture source changed during validation.'
    }
  }
  $report.inputsPreserved = $true
  $report.passed = $true
  $exitCode = 0
  Write-Host ('Display readiness: ' + $report.cases.Count + ' cases passed')
}
catch {
  $report.failedStage = $stage
  Write-Host ('Readiness validation failed during ' + $stage + '. See the project-local logs.')
}
finally {
  $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'validation.json') -Encoding UTF8
  Write-Host ('Report: out/test/' + $runName + '/validation.json')
}

exit $exitCode
