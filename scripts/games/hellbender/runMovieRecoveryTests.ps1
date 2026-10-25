# SPDX-License-Identifier: MIT

param(
  [ValidateSet('English', 'French', 'German')][string[]]$Edition = @('English'),
  [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release'
)

# Owned COM fixtures and inert mapped image. No game or display-mode changes.
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$testRoot = Join-Path $projectRoot 'out\test'
$buildRoot = Join-Path $testRoot 'hellbenderMovieRecovery-msvc1444'
$runName = 'hellbenderMovieRecovery-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$runRoot = Join-Path $testRoot $runName
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null

. (Join-Path $projectRoot 'scripts\core\nativeTools.ps1')
. (Join-Path $PSScriptRoot 'core\testInputs.ps1')
$processOptions = @{ WorkingDirectory = $projectRoot; LogDirectory = $runRoot; Environment = @{ PATHEXT = '.COM;.EXE;.BAT;.CMD' } }
$report = [ordered]@{
  format             = 'HellbenderMovieRecoveryValidation'
  version            = 1
  passed             = $false
  configuration      = $Configuration
  toolchain          = 'MSVC 14.44 x86'
  gameLaunched       = $false
  gameAttached       = $false
  realDisplayChanges = $false
  checks             = 0
  cases              = 0
  editions           = @()
  limitations        = @('Owned COM interfaces and inert images validate recovery, pixels and ownership. Live Windows driver behavior still requires gameplay testing.')
}
$stage = 'source-identity'
$exitCode = 1

try {
  $records = @(ReadHellbenderInputs -ProjectRoot $projectRoot -Edition $Edition -IncludePayload)
  $report.sources = @(
    'scripts/games/hellbender/runMovieRecoveryTests.ps1', 'scripts/core/nativeTools.ps1',
    'tests/games/hellbender/CMakeLists.txt', 'tests/games/hellbender/movieRecoveryTests.cpp', 'tests/support/mappedImage.hpp',
    'scripts/games/hellbender/core/testInputs.ps1', 'include/games/hellbender/core/movieRecovery.hpp',
    'include/games/hellbender/core/flipProbe.hpp', 'src/games/hellbender/core/layoutPayload.cpp',
    'src/games/hellbender/core/layoutPayload.def',
    'include/games/hellbender/core/displayReadiness.hpp',
    'include/games/hellbender/core/layoutPayload.hpp',
    'include/games/hellbender/core/textureLifecycle.hpp',
    'include/games/hellbender/core/textureRecovery.hpp',
    'include/games/hellbender/core/textureLoadRecovery.hpp',
    'include/games/hellbender/core/textureUploadOwnership.hpp',
    'include/games/hellbender/core/textureTransfer.hpp'
  ) | ForEach-Object { [ordered]@{ path = $_; sha256 = (GetSha256 (Join-Path $projectRoot $_)) } }

  $report.addresses = @($records | ForEach-Object {
      $path = 'out/patches/hellbender/' + $_.slug + '/gameAddresses.hpp'
      [ordered]@{ path = $path; sha256 = (GetSha256 (Join-Path $projectRoot $path)) }
    })
  $stage = 'configure'
  $cmake = FindCMake
  $configured = InvokeNativeProcess @processOptions -Program $cmake -Arguments @(
    '-S', (Join-Path $projectRoot 'tests\games\hellbender'), '-B', $buildRoot,
    '-G', 'Visual Studio 17 2022', '-A', 'Win32', '-T', 'version=14.44',
    '-DHAV_BUILD_MOVIE_RECOVERY_TESTS=ON', ('-DHAV_MOVIE_EDITIONS=' + ($Edition -join ';'))
  ) -LogName 'configure' -Timeout 600000
  if ($configured.exitCode -ne 0) { throw 'Movie recovery fixture configuration failed.' }

  $stage = 'build'
  $targets = @($records | ForEach-Object { 'movieRecoveryTests' + $_.name })
  $built = InvokeNativeProcess @processOptions -Program $cmake -Arguments (@(
      '--build', $buildRoot, '--config', $Configuration, '--parallel', '--target'
    ) + $targets) -LogName 'build' -Timeout 600000
  if ($built.exitCode -ne 0) { throw 'Movie recovery fixture compilation failed.' }

  foreach ($record in $records) {
    $stage = $record.slug + '-fixture'
    $program = Join-Path $buildRoot ($Configuration + '\movieRecoveryTests' + $record.name + '.exe')
    $native = InvokeNativeProcess @processOptions -Program $program -Arguments @($record.payload, $record.image, [string]$record.origin) -LogName $stage -Timeout 45000
    if ($native.exitCode -ne 0) { throw 'Movie recovery fixture failed.' }
    $result = $native.stdout | ConvertFrom-Json
    if ($result.passed -ne $true -or $result.checks -le 0 -or $result.cases -le 0 -or $result.mappedBases -ne 2) {
      throw 'Movie recovery fixture returned an incomplete result.'
    }
    $report.checks += $result.checks
    $report.cases += $result.cases
    $report.editions += [ordered]@{
      edition = $record.name; sha256 = $record.imageHash; profileSha256 = $record.profileHash
      payloadSha256 = $record.payloadHash; fixtureSha256 = (GetSha256 $program)
      checks = $result.checks; cases = $result.cases; passed = $true
    }
    Write-Host ($record.name + ': ' + $result.cases + ' movie recovery cases passed')
  }
  $stage = 'source-preservation'
  AssertHellbenderInputsUnchanged -Records $records
  foreach ($source in @($report.sources) + @($report.addresses)) {
    if ((GetSha256 (Join-Path $projectRoot $source.path)) -cne $source.sha256) {
      throw 'A fixture source changed during validation.'
    }
  }
  $report.inputsPreserved = $true
  $report.passed = $true
  $exitCode = 0
  Write-Host ('Movie recovery: ' + $report.cases + ' cases passed')
}
catch {
  $report.failedStage = $stage
  Write-Host ('Movie recovery validation failed during ' + $stage + '. See the project-local logs.')
}
finally {
  $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'validation.json') -Encoding UTF8
  Write-Host ('Report: out/test/' + $runName + '/validation.json')
}

exit $exitCode
