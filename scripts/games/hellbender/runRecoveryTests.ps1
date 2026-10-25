# SPDX-License-Identifier: MIT

param(
  [ValidateSet('English', 'French', 'German')]
  [string[]]$Edition = @('English', 'French', 'German'),
  [string]$EnglishInput,
  [string]$FrenchInput,
  [string]$GermanInput,
  [string]$PythonExecutable = 'py.exe',
  [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release'
)

# Runs exact native spans inside owned x86 fixtures. No game entry point is called.
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$testRoot = Join-Path $projectRoot 'out\test'
$buildRoot = Join-Path $testRoot 'hellbenderRecovery-msvc1444'
$runName = 'hellbenderRecovery-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$runRoot = Join-Path $testRoot $runName
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null

. (Join-Path $projectRoot 'scripts\core\nativeTools.ps1')
. (Join-Path $PSScriptRoot 'core\testInputs.ps1')

$processOptions = @{ WorkingDirectory = $projectRoot; LogDirectory = $runRoot; Environment = @{ PATHEXT = '.COM;.EXE;.BAT;.CMD' } }
$inputs = @{ English = $EnglishInput; French = $FrenchInput; German = $GermanInput }
$pythonArguments = @()
if ([IO.Path]::GetFileNameWithoutExtension($PythonExecutable) -ieq 'py') { $pythonArguments = @('-3') }
$fixtures = @('fullscreenSetupTests', 'rendererRecoveryTests', 'frameRecoveryTests', 'modeCompletionTests')
$records = @()
$inputHashes = @{}
$report = [ordered]@{
  format        = 'HellbenderRecoveryValidation'
  version       = 1
  passed        = $false
  configuration = $Configuration
  toolchain     = 'MSVC x86'
  gameLaunched  = $false
  gameAttached  = $false
  checks        = 0
  editions      = @()
  nativeInputs  = @()
  sources       = @()
  limitations   = @(
    'Real retail instruction spans run with owned COM objects and controlled Windows callbacks'
    'The original fullscreen setup body and native input action are explicit fixture boundaries'
    'Separate initial modes verify dispatch, not a continuous driver mode cycle'
    'These fixtures do not establish live rendering, driver behavior or monitor timing'
  )
}
$stage = 'input-validation'
$exitCode = 1

try {
  if ($Edition.Count -eq 0) { throw 'Select at least one edition.' }
  $Edition = @($Edition | Select-Object -Unique)
  $records = @(ReadHellbenderInputs -ProjectRoot $projectRoot -Edition $Edition -IncludePayload)
  $sourcePaths = @(
    'scripts/games/hellbender/runRecoveryTests.ps1', 'scripts/core/nativeTools.ps1',
    'scripts/games/hellbender/core/testInputs.ps1', 'tests/games/hellbender/CMakeLists.txt',
    'tests/support/generateNativeFixtures.py', 'tests/support/nativeFixtureManifest.json',
    'scripts/core/peImage.py', 'scripts/core/recipeData.py',
    'scripts/games/hellbender/generateRecipe.py',
    'src/games/hellbender/core/layoutPayload.cpp', 'src/games/hellbender/core/layoutPayload.def'
  )
  $sourcePaths += @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'include/games/hellbender/core') -File | ForEach-Object {
      $_.FullName.Substring($projectRoot.Length + 1).Replace('\', '/')
    })
  $sourcePaths += @($fixtures | ForEach-Object { 'tests/games/hellbender/' + $_ + '.cpp' })
  $sourcePaths += @(Get-ChildItem -LiteralPath (Join-Path $projectRoot 'tests/games/hellbender/support') -File | ForEach-Object {
      $_.FullName.Substring($projectRoot.Length + 1).Replace('\', '/')
    })
  $report.sources = @($sourcePaths | Sort-Object -Unique | ForEach-Object {
      [ordered]@{ path = $_; sha256 = (GetSha256 (Join-Path $projectRoot $_)) }
    })

  foreach ($record in $records) {
    $stage = $record.slug + '-native-input'
    $inputFile = $inputs[$record.name]
    if ([string]::IsNullOrWhiteSpace($inputFile)) { throw 'A selected retail input was not supplied.' }
    $inputHashes[$record.name] = GetSha256 $inputFile
    $nativeRoot = Join-Path $testRoot ('hellbenderNative\' + $record.slug)
    $generation = InvokeNativeProcess @processOptions -Program $PythonExecutable -Arguments ($pythonArguments + @(
        (Join-Path $projectRoot 'tests\support\generateNativeFixtures.py'),
        '--edition', $record.name, '--input', $inputFile, '--output', $nativeRoot
      )) -LogName $stage -Timeout 120000
    if ($generation.exitCode -ne 0) { throw 'Native fixture generation failed.' }
    $receipt = Join-Path $nativeRoot 'fixtureInputs.json'
    $report.nativeInputs += [ordered]@{
      edition = $record.name; retailSha256 = $inputHashes[$record.name]
      receiptSha256 = (GetSha256 $receipt)
    }
    foreach ($file in @(Get-ChildItem -LiteralPath $nativeRoot -File)) {
      $report.sources += [ordered]@{
        path   = $file.FullName.Substring($projectRoot.Length + 1).Replace('\', '/')
        sha256 = (GetSha256 $file.FullName)
      }
    }
    $addressHeader = 'out/patches/hellbender/' + $record.slug + '/gameAddresses.hpp'
    $report.sources += [ordered]@{ path = $addressHeader; sha256 = (GetSha256 (Join-Path $projectRoot $addressHeader)) }
  }

  $stage = 'configure'
  $cmakePath = FindCMake
  $configure = InvokeNativeProcess @processOptions -Program $cmakePath -Arguments @(
    '-S', (Join-Path $projectRoot 'tests\games\hellbender'), '-B', $buildRoot,
    '-G', 'Visual Studio 17 2022', '-A', 'Win32', '-T', 'version=14.44',
    '-DHAV_BUILD_RECOVERY_TESTS=ON', ('-DHAV_RECOVERY_EDITIONS=' + ($Edition -join ';'))
  ) -LogName $stage -Timeout 600000
  if ($configure.exitCode -ne 0) { throw 'Recovery fixture configuration failed.' }

  $stage = 'build'
  $targets = @($records | ForEach-Object {
      $name = $_.name
      $fixtures | ForEach-Object { $_ + $name }
    })
  $build = InvokeNativeProcess @processOptions -Program $cmakePath -Arguments (@(
      '--build', $buildRoot, '--config', $Configuration, '--parallel', '--target'
    ) + $targets) -LogName $stage -Timeout 600000
  if ($build.exitCode -ne 0) { throw 'Recovery fixture build failed.' }

  foreach ($record in $records) {
    foreach ($fixture in $fixtures) {
      $stage = $record.slug + '-' + $fixture
      $program = Join-Path $buildRoot ($Configuration + '\' + $fixture + $record.name + '.exe')
      $native = InvokeNativeProcess @processOptions -Program $program -Arguments @() -LogName $stage -Timeout 60000
      if ($native.exitCode -ne 0) { throw 'An owned recovery fixture failed.' }
      $result = $native.stdout | ConvertFrom-Json
      if ($result.passed -ne $true -or $result.checks -le 0 -or $result.mappedBases -ne 2) {
        throw 'An owned recovery fixture did not complete both mappings.'
      }
      $report.editions += [ordered]@{
        edition = $record.name; fixture = $fixture; fixtureSha256 = (GetSha256 $program)
        imageSha256 = $record.imageHash; profileSha256 = $record.profileHash
        checks = $result.checks; passed = $true
      }
      $report.checks += $result.checks
      Write-Host ($record.name + ' ' + $fixture + ': ' + $result.checks + ' checks passed')
    }
  }

  $stage = 'input-preservation'
  AssertHellbenderInputsUnchanged -Records $records
  foreach ($record in $records) {
    if ((GetSha256 $inputs[$record.name]) -cne $inputHashes[$record.name]) { throw 'A retail input changed.' }
  }
  foreach ($source in $report.sources) {
    if ((GetSha256 (Join-Path $projectRoot $source.path)) -cne $source.sha256) { throw 'A test input changed.' }
  }
  $report.inputsPreserved = $true
  $report.passed = $true
  $exitCode = 0
}
catch {
  $report.failedStage = $stage
  Write-Host ('Recovery validation failed during ' + $stage + '. See the project-local logs.')
}
finally {
  $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'validation.json') -Encoding UTF8
  Write-Host ('Report: out/test/' + $runName + '/validation.json')
}

exit $exitCode
