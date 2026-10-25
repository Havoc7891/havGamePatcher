# SPDX-License-Identifier: MIT

param(
  [ValidateSet('English', 'French', 'German')]
  [string[]]$Edition = @('English', 'French', 'German'),
  [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release'
)

# Runs only an owned x86 fixture. Game entry points and imports are never called.
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$testRoot = Join-Path $projectRoot 'out\test'
$buildRoot = Join-Path $testRoot 'hellbenderShading-msvc'
$runName = 'hellbenderShading-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$runRoot = Join-Path $testRoot $runName
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null

. (Join-Path $projectRoot 'scripts\core\nativeTools.ps1')
. (Join-Path $PSScriptRoot 'core\testInputs.ps1')

$processOptions = @{ WorkingDirectory = $projectRoot; LogDirectory = $runRoot; Environment = @{} }

function WriteLayout($Profile, [string]$Path) {
  $shading = $Profile.shading
  $numbers = @(0x48475354, 1, (ReadProfileNumber $Profile.preferredBase),
    (ReadProfileNumber $shading.hookRva), (ReadProfileNumber $shading.emitRva), (ReadProfileNumber $shading.skipRva),
    (ReadProfileNumber $shading.polygonStack.source), (ReadProfileNumber $shading.polygonStack.count), (ReadProfileNumber $shading.polygonStack.flags),
    (ReadProfileNumber $shading.sourceVertexSize), (ReadProfileNumber $shading.sourceFogOffsets[0]),
    (ReadProfileNumber $shading.needleCount), (ReadProfileNumber $shading.needleFlags),
    (ReadProfileNumber $shading.radarBackgroundMinCount), (ReadProfileNumber $shading.radarBackgroundMaxCount), (ReadProfileNumber $shading.radarBackgroundFlags))

  foreach ($chain in @($shading.needleReturnChain, $shading.radarBackgroundReturnChain)) {
    $numbers += $chain.Count
    foreach ($site in $chain) {
      $numbers += (ReadProfileNumber $site.stackOffset), (ReadProfileNumber $site.returnRva)
    }
  }

  [IO.File]::WriteAllLines($Path, [string[]]($numbers | ForEach-Object { '0x{0:x}' -f $_ }), [Text.Encoding]::ASCII)
}

$report = [ordered]@{
  format        = 'HellbenderShadingPredicateValidation'
  version       = 1
  passed        = $false
  configuration = $Configuration
  toolchain     = 'MSVC x86'
  gameLaunched  = $false
  gameAttached  = $false
  imageBases    = @('0x20000000', '0x30000000')
  checks        = 0
  failed        = 0
  editions      = @()
  limitations   = @('Native continuation sinks observe the shading decision without running game renderer or GPU code.')
}

$records = @()
$stage = 'input-validation'
$exitCode = 1

try {
  $records = @(ReadHellbenderInputs -ProjectRoot $projectRoot -Edition $Edition)

  foreach ($record in $records) {
    $layoutPath = Join-Path $runRoot ($record.slug + '.layout.txt')
    WriteLayout $record.profileData $layoutPath
    $record | Add-Member -NotePropertyName layout -NotePropertyValue $layoutPath
  }

  $stage = 'configure'
  $cmakePath = FindCMake
  $configureCode = (InvokeNativeProcess @processOptions -Program $cmakePath -Arguments @('-S', (Join-Path $projectRoot 'tests\games\hellbender'), '-B', $buildRoot,
      '-G', 'Visual Studio 17 2022', '-A', 'Win32') -LogName 'configure' -Timeout 600000).exitCode
  if ($configureCode -ne 0) { throw 'Shading fixture configuration failed.' }

  $stage = 'build'
  $buildCode = (InvokeNativeProcess @processOptions -Program $cmakePath -Arguments @('--build', $buildRoot, '--config', $Configuration, '--parallel', '--target', 'shadingPredicateTests') -LogName 'build' -Timeout 600000).exitCode
  if ($buildCode -ne 0) { throw 'Shading fixture build failed.' }

  $program = Join-Path $buildRoot ($Configuration + '\shadingPredicateTests.exe')
  $report.fixtureSha256 = GetSha256 $program

  foreach ($record in $records) {
    $stage = 'fixture-' + $record.slug
    $resultPath = Join-Path $runRoot ($record.slug + '.json')

    $nativeExit = (InvokeNativeProcess @processOptions -Program $program -Arguments @($record.image, $record.layout, $resultPath) -LogName $record.slug -Timeout 30000).exitCode
    if ($nativeExit -ne 0) { throw 'An owned native shading fixture failed.' }

    $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
    if ($result.passed -ne $true -or $result.failed -ne 0 -or $result.checks -le 0) {
      throw 'An owned native shading fixture returned an incomplete result.'
    }

    $report.editions += [ordered]@{
      edition = $record.name; sha256 = $record.imageHash; profileSha256 = $record.profileHash
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
  Write-Host ('Shading validation failed during ' + $stage + '. See the project-local logs.')
}
finally {
  $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'validation.json') -Encoding UTF8
  Write-Host ('Report: out/test/' + $runName + '/validation.json')
}

exit $exitCode
