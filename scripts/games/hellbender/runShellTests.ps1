# SPDX-License-Identifier: MIT

param(
  [ValidateSet('English', 'French', 'German')]
  [string[]]$Edition = @('English', 'French', 'German'),
  [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
  [switch]$ProbeShell
)

# Runs owned x86 fixtures with an optional one-time COM availability probe. No game entry point or desktop fullscreen marker is called.
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$testRoot = Join-Path $projectRoot 'out\test'
$buildRoot = Join-Path $testRoot 'hellbenderShell-msvc'
$runName = 'hellbenderShell-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$runRoot = Join-Path $testRoot $runName
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null

. (Join-Path $projectRoot 'scripts\core\nativeTools.ps1')
. (Join-Path $PSScriptRoot 'core\testInputs.ps1')

$processOptions = @{ WorkingDirectory = $projectRoot; LogDirectory = $runRoot; Environment = @{} }

$report = [ordered]@{
  format               = 'HellbenderShellFullscreenValidation'
  version              = 1
  passed               = $false
  configuration        = $Configuration
  toolchain            = 'MSVC x86'
  gameLaunched         = $false
  gameAttached         = $false
  contexts             = @('Windows DLL loader', 'Inert embedded image at 0x20000000')
  checks               = 0
  failed               = 0
  editions             = @()
  realShellMarkerCalls = 0
  shellProbeRequested  = [bool]$ProbeShell
  limitations          = @('Marker calls use an owned fake COM interface. The real shell probe only initializes ITaskbarList2 and calls HrInit.',
    'Window eligibility is simulated on hidden owned windows. In-game taskbar behavior still requires gameplay testing.')
}

$records = @()
$stage = 'input-validation'
$exitCode = 1

try {
  $records = @(ReadHellbenderInputs -ProjectRoot $projectRoot -Edition $Edition -IncludePayload)

  $stage = 'configure'
  $cmakePath = FindCMake
  $configureCode = (InvokeNativeProcess @processOptions -Program $cmakePath -Arguments @('-S', (Join-Path $projectRoot 'tests\games\hellbender'), '-B', $buildRoot,
      '-G', 'Visual Studio 17 2022', '-A', 'Win32', '-DHAV_BUILD_SHELL_TESTS=ON',
      ('-DHAV_SHELL_EDITIONS=' + (($records | ForEach-Object { $_.name }) -join ';'))) -LogName 'configure' -Timeout 600000).exitCode
  if ($configureCode -ne 0) { throw 'Shell fixture configuration failed.' }

  $stage = 'build'
  $targets = @($records | ForEach-Object { 'shellFullscreenTests' + $_.name })
  $buildCode = (InvokeNativeProcess @processOptions -Program $cmakePath -Arguments (@('--build', $buildRoot, '--config', $Configuration, '--parallel', '--target') + $targets) -LogName 'build' -Timeout 600000).exitCode
  if ($buildCode -ne 0) { throw 'Shell fixture build failed.' }

  if ($ProbeShell) {
    $stage = 'shell-availability'
    $program = Join-Path $buildRoot ($Configuration + '\shellFullscreenTests' + $records[0].name + '.exe')
    $probe = InvokeNativeProcess @processOptions -Program $program -Arguments @('--probe-shell') -LogName $stage -Timeout 30000
    if ($probe.exitCode -ne 0) { throw 'The optional Shell COM availability probe failed.' }
    $report.shellProbePassed = $true
  }

  foreach ($record in $records) {
    $stage = 'fixture-' + $record.slug
    $program = Join-Path $buildRoot ($Configuration + '\shellFullscreenTests' + $record.name + '.exe')
    $resultPath = Join-Path $runRoot ($record.slug + '.json')

    $nativeExit = (InvokeNativeProcess @processOptions -Program $program -Arguments @($record.payload, $record.image, [string]$record.origin, $resultPath) -LogName $record.slug -Timeout 45000).exitCode
    if ($nativeExit -ne 0) { throw 'An owned native shell fixture failed.' }

    $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
    if ($result.passed -ne $true -or $result.failed -ne 0 -or $result.checks -le 0 -or $result.realShellMarkerCalls -ne 0) {
      throw 'An owned native shell fixture returned an incomplete result.'
    }

    $report.editions += [ordered]@{
      edition = $record.name; sha256 = $record.imageHash; profileSha256 = $record.profileHash
      payloadSha256 = $record.payloadHash; fixtureSha256 = (GetSha256 $program)
      checks = $result.checks; failed = $result.failed; passed = $result.passed
    }
    $report.checks += $result.checks
    $report.failed += $result.failed
    Write-Host ($record.name + ': ' + $result.checks + ' checks passed')
  }

  $stage = 'input-preservation'
  AssertHellbenderInputsUnchanged -Records $records
  $report.inputsPreserved = $true

  $report.passed = $true
  $exitCode = 0
}
catch {
  # Suppress arbitrary filesystem errors so reports never disclose external paths.
  $report.failedStage = $stage
  Write-Host ('Shell validation failed during ' + $stage + '. See the project-local logs.')
}
finally {
  $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'validation.json') -Encoding UTF8
  Write-Host ('Report: out/test/' + $runName + '/validation.json')
}

exit $exitCode
