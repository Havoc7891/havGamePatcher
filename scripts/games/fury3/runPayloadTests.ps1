# SPDX-License-Identifier: MIT

param(
  [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
  [switch]$IncludeMovie
)

# Uses owned windows and mapped patch code. No game process or joystick device is opened.
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$testRoot = Join-Path $projectRoot 'out\test'
$buildRoot = Join-Path $testRoot 'fury3-msvc'
$runName = 'fury3-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$runRoot = Join-Path $testRoot $runName
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null

. (Join-Path $projectRoot 'scripts\core\nativeTools.ps1')

$processOptions = @{ WorkingDirectory = $projectRoot; LogDirectory = $runRoot; Environment = @{} }
$report = [ordered]@{
  format         = 'Fury3PayloadValidation'
  version        = 1
  passed         = $false
  configuration  = $Configuration
  toolchain      = 'MSVC x86'
  gameLaunched   = $false
  gameAttached   = $false
  movieRequested = [bool]$IncludeMovie
  checks         = 0
  fixtures       = @()
  limitations    = @(
    'Game data and original routines are simulated. These tests do not establish complete gameplay behavior.',
    'The optional movie fixture uses native Windows MCI with generated silent video in hidden owned windows.'
  )
}
$stage = 'input-validation'
$exitCode = 1

try {
  $outputRoot = Join-Path $projectRoot 'out\patches\fury3\english'
  $profilePath = Join-Path $projectRoot 'patches\fury3\english\versionProfile.json'
  $lockPath = Join-Path $projectRoot 'patches\fury3\english\recipeLock.json'
  $recipePath = Join-Path $outputRoot 'recipeReport.json'
  $image = Join-Path $outputRoot 'FURY3.fullscreen.EXE'
  $payload = Join-Path $outputRoot 'fullscreenPayload.dll'
  $profile = Get-Content -LiteralPath $profilePath -Raw | ConvertFrom-Json
  $lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
  $recipe = Get-Content -LiteralPath $recipePath -Raw | ConvertFrom-Json
  $imageHash = GetSha256 $image
  $payloadHash = GetSha256 $payload
  $profileHash = GetSha256 $profilePath

  if ($lock.edition -cne 'English' -or $profile.language -cne 'English' -or
    $imageHash -cne $lock.patchedSha256 -or $payloadHash -cne $lock.payloadSha256 -or
    $profileHash -cne $lock.profileSha256 -or (Get-Item -LiteralPath $image).Length -ne $lock.patchedSize -or
    $recipe.patchedSha256 -cne $imageHash -or $recipe.payloadSha256 -cne $payloadHash -or
    $recipe.profileSha256 -cne $profileHash -or
    $recipe.transformations.configRva -ne $recipe.transformations.exports.config) {
    throw 'Maintained Fury3 output, payload or profile does not match its recipe lock.'
  }

  $inputs = @($profilePath, $lockPath, $recipePath, $image, $payload) |
  ForEach-Object { @{ path = $_; sha256 = (GetSha256 $_) } }
  $report.imageSha256 = $imageHash
  $report.payloadSha256 = $payloadHash
  $report.profileSha256 = $profileHash

  $stage = 'configure'
  $cmakePath = FindCMake
  $movieOption = if ($IncludeMovie) { 'ON' } else { 'OFF' }
  $configure = InvokeNativeProcess @processOptions -Program $cmakePath -Arguments @(
    '-S', (Join-Path $projectRoot 'tests\games\fury3'), '-B', $buildRoot,
    '-G', 'Visual Studio 17 2022', '-A', 'Win32', ('-DHAV_BUILD_MOVIE_TESTS=' + $movieOption)
  ) -LogName 'configure' -Timeout 600000
  if ($configure.exitCode -ne 0) { throw 'Fury3 fixture configuration failed.' }

  $stage = 'build'
  $targets = @('inputHookTests', 'fullscreenTests')
  if ($IncludeMovie) { $targets += 'movieTests' }
  $build = InvokeNativeProcess @processOptions -Program $cmakePath -Arguments (
    @('--build', $buildRoot, '--config', $Configuration, '--parallel', '--target') + $targets
  ) -LogName 'build' -Timeout 600000
  if ($build.exitCode -ne 0) { throw 'Fury3 fixture build failed.' }

  foreach ($fixture in $targets) {
    $stage = $fixture
    $program = Join-Path $buildRoot ($Configuration + '\' + $fixture + '.exe')
    $resultPath = Join-Path $runRoot ($fixture + '.json')
    $arguments = switch ($fixture) {
      'inputHookTests' { @($image, [string]$recipe.transformations.configRva, [string]$recipe.transformations.exports.state, $resultPath) }
      'fullscreenTests' { @($payload, $resultPath) }
      'movieTests' { @($payload, (Join-Path $buildRoot 'owned checkerboard.avi'), $resultPath) }
    }
    $native = InvokeNativeProcess @processOptions -Program $program -Arguments $arguments -LogName $fixture -Timeout 45000
    if ($native.exitCode -ne 0) { throw 'An owned Fury3 fixture failed.' }

    $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
    if ($result.passed -ne $true -or $result.checksTotal -le 0 -or $result.checksPassed -ne $result.checksTotal) {
      throw 'An owned Fury3 fixture did not complete successfully.'
    }

    $report.fixtures += [ordered]@{
      name = $fixture; sha256 = (GetSha256 $program); checks = $result.checksTotal; passed = $true
    }
    $report.checks += $result.checksTotal
    Write-Host ($fixture + ': ' + $result.checksTotal + ' checks passed')
  }

  $stage = 'input-preservation'
  foreach ($record in $inputs) {
    if ((GetSha256 $record.path) -cne $record.sha256) {
      throw 'A maintained input changed during the test run.'
    }
  }

  $report.inputsPreserved = $true
  $report.passed = $true
  $exitCode = 0
}
catch {
  $report.failedStage = $stage
  Write-Host ('Fury3 validation failed during ' + $stage + '. See the project-local logs.')
}
finally {
  $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'validation.json') -Encoding UTF8
  Write-Host ('Report: out/test/' + $runName + '/validation.json')
}

exit $exitCode
