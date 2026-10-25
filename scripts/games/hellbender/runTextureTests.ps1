# SPDX-License-Identifier: MIT

param(
  [ValidateSet('English', 'French', 'German')]
  [string[]]$Edition = @('English', 'French', 'German'),
  [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release'
)

# Runs owned x86 texture and inert-image hook fixtures. No game entry point is called.
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$testRoot = Join-Path $projectRoot 'out\test'
$buildRoot = Join-Path $testRoot 'hellbenderTexture-msvc1444'
$runName = 'hellbenderTexture-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$runRoot = Join-Path $testRoot $runName
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null

. (Join-Path $projectRoot 'scripts\core\nativeTools.ps1')
. (Join-Path $PSScriptRoot 'core\testInputs.ps1')

$processOptions = @{ WorkingDirectory = $projectRoot; LogDirectory = $runRoot; Environment = @{ PATHEXT = '.COM;.EXE;.BAT;.CMD' } }

$report = [ordered]@{
  format         = 'HellbenderTextureValidation'
  version        = 1
  passed         = $false
  configuration  = $Configuration
  toolchain      = 'MSVC x86'
  gameLaunched   = $false
  gameAttached   = $false
  contexts       = @('Actual core headers with owned COM fixtures', 'Inert embedded images at 0x20000000 and 0x30000000')
  checks         = 0
  failed         = 0
  editions       = @()
  sharedFixtures = @()
  limitations    = @('Native game functions and COM objects are simulated. These checks do not establish live gameplay behavior.')
}

$records = @()
$stage = 'input-validation'
$exitCode = 1

try {
  if ($Edition.Count -eq 0) { throw 'Select at least one edition.' }

  $records = @(ReadHellbenderInputs -ProjectRoot $projectRoot -Edition (@('English') + $Edition) -IncludePayload)

  $report.sources = @(
    'scripts/games/hellbender/runTextureTests.ps1', 'scripts/core/nativeTools.ps1',
    'scripts/games/hellbender/core/testInputs.ps1', 'tests/games/hellbender/CMakeLists.txt',
    'tests/support/mappedImage.hpp', 'tests/games/hellbender/textureTransferTests.cpp',
    'tests/games/hellbender/textureRecoveryTests.cpp', 'tests/games/hellbender/textureHookTests.cpp',
    'include/games/hellbender/core/textureTransfer.hpp', 'include/games/hellbender/core/textureRecovery.hpp',
    'include/games/hellbender/core/textureLoadRecovery.hpp', 'include/games/hellbender/core/textureUploadOwnership.hpp',
    'include/games/hellbender/core/textureLifecycle.hpp', 'include/games/hellbender/core/layoutPayload.hpp',
    'src/games/hellbender/core/layoutPayload.cpp', 'src/games/hellbender/core/layoutPayload.def'
  ) | ForEach-Object { [ordered]@{ path = $_; sha256 = (GetSha256 (Join-Path $projectRoot $_)) } }

  $stage = 'configure'
  $cmakePath = FindCMake
  $configureCode = (InvokeNativeProcess @processOptions -Program $cmakePath -Arguments @('-S', (Join-Path $projectRoot 'tests\games\hellbender'), '-B', $buildRoot,
      '-G', 'Visual Studio 17 2022', '-A', 'Win32', '-T', 'version=14.44', '-DHAV_BUILD_TEXTURE_TESTS=ON',
      ('-DHAV_TEXTURE_EDITIONS=' + (($records | ForEach-Object { $_.name }) -join ';'))) -LogName 'configure' -Timeout 600000).exitCode
  if ($configureCode -ne 0) { throw 'Texture fixture configuration failed.' }

  $stage = 'build'
  $targets = @('textureTransferTests', 'textureRecoveryTests') + @($records | Where-Object { $_.name -in $Edition } | ForEach-Object { 'textureHookTests' + $_.name })
  $buildCode = (InvokeNativeProcess @processOptions -Program $cmakePath -Arguments (@('--build', $buildRoot, '--config', $Configuration, '--parallel', '--target') + $targets) -LogName 'build' -Timeout 600000).exitCode
  if ($buildCode -ne 0) { throw 'Texture fixture build failed.' }

  foreach ($fixture in @('textureTransferTests', 'textureRecoveryTests')) {
    $stage = 'shared-' + $fixture
    $program = Join-Path $buildRoot ($Configuration + '\' + $fixture + '.exe')
    $native = InvokeNativeProcess @processOptions -Program $program -Arguments @() -LogName $stage -Timeout 45000
    if ($native.exitCode -ne 0) { throw 'An owned shared texture fixture failed.' }

    $result = $native.stdout | ConvertFrom-Json
    if ($result.passed -ne $true -or $result.checks -le 0) {
      throw 'An owned shared texture fixture did not complete successfully.'
    }

    $report.sharedFixtures += [ordered]@{
      name = $fixture; addressEdition = 'English'; sha256 = (GetSha256 $program); checks = $result.checks; passed = $true
    }
    $report.checks += $result.checks
    Write-Host ($fixture + ': ' + $result.checks + ' checks passed')
  }

  foreach ($record in @($records | Where-Object { $_.name -in $Edition })) {
    $stage = $record.slug + '-textureHookTests'
    $program = Join-Path $buildRoot ($Configuration + '\textureHookTests' + $record.name + '.exe')
    $native = InvokeNativeProcess @processOptions -Program $program -Arguments @($record.payload, $record.image, [string]$record.origin) -LogName $stage -Timeout 45000
    if ($native.exitCode -ne 0) { throw 'An owned native texture hook fixture failed.' }

    $result = $native.stdout | ConvertFrom-Json
    if ($result.passed -ne $true -or $result.checks -le 0 -or $result.mappedBases -ne 2) {
      throw 'An owned native texture hook fixture did not complete both mappings.'
    }

    $report.editions += [ordered]@{
      edition = $record.name; sha256 = $record.imageHash; profileSha256 = $record.profileHash
      payloadSha256 = $record.payloadHash; fixtureSha256 = (GetSha256 $program)
      checks = $result.checks; passed = $true
    }
    $report.checks += $result.checks
    Write-Host ($record.name + ': ' + $result.checks + ' hook checks passed')
  }

  $stage = 'input-preservation'
  AssertHellbenderInputsUnchanged -Records $records
  foreach ($source in $report.sources) {
    if ((GetSha256 (Join-Path $projectRoot $source.path)) -cne $source.sha256) {
      throw 'A fixture or core source changed during the test run.'
    }
  }

  $report.inputsPreserved = $true
  $report.passed = $true
  $exitCode = 0
}
catch {
  # Suppress arbitrary filesystem errors so reports never disclose external paths.
  $report.failedStage = $stage
  Write-Host ('Texture validation failed during ' + $stage + '. See the project-local logs.')
}
finally {
  $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'validation.json') -Encoding UTF8
  Write-Host ('Report: out/test/' + $runName + '/validation.json')
}

exit $exitCode
