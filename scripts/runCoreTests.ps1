# SPDX-License-Identifier: MIT

param(
  [Parameter(Mandatory = $true)][string]$GermanRetail,
  [Parameter(Mandatory = $true)][string]$JoystickOnly,
  [Parameter(Mandatory = $true)][string]$FrozenFullscreen,
  [Parameter(Mandatory = $true)][string]$FrenchRetail,
  [Parameter(Mandatory = $true)][string]$FrenchJoystickOnly,
  [Parameter(Mandatory = $true)][string]$FrenchFullscreen,
  [Parameter(Mandatory = $true)][string]$EnglishRetail,
  [Parameter(Mandatory = $true)][string]$EnglishJoystickOnly,
  [Parameter(Mandatory = $true)][string]$EnglishFullscreen,
  [Parameter(Mandatory = $true)][string]$Fury3Retail,
  [Parameter(Mandatory = $true)][string]$Fury3JoystickOnly,
  [Parameter(Mandatory = $true)][string]$Fury3Fullscreen,
  [string[]]$OtherRetail = @(),
  [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
  [ValidateSet('msvc', 'mingw')][string]$Toolchain = 'msvc',
  [string]$MingwDirectory
)

$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$testSource = Join-Path $projectRoot 'tests'
$testRoot = Join-Path $projectRoot 'out\test'
$buildRoot = Join-Path $testRoot ('core-' + $Toolchain)
$runRoot = Join-Path $testRoot ('core-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$fixtureRoot = Join-Path $runRoot 'fixtures'
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null

. (Join-Path $PSScriptRoot 'core\nativeTools.ps1')

$processOptions = @{ WorkingDirectory = $projectRoot; LogDirectory = $runRoot; Environment = @{} }

function Get-InputRecord([string]$Path, [string]$Name = '') {
  try {
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $item = Get-Item -LiteralPath $resolved
    if ($item.PSIsContainer) { throw 'A reference input is a directory.' }

    $hash = GetSha256 $resolved
  }
  catch {
    throw 'Cannot read reference input metadata.'
  }

  return [ordered]@{
    name   = $(if ($Name) { $Name } else { $item.Name })
    size   = $item.Length
    sha256 = $hash
  }
}

$report = [ordered]@{
  format               = 'ProductionCoreIntegrationRun'
  status               = 'failed'
  toolchain            = $Toolchain
  gameProcessesStarted = $false
  fixtureDirectory     = 'out/test/' + (Split-Path -Leaf $runRoot) + '/fixtures'
  inputs               = @()
  inputPreservation    = @()
}

$exitCode = 1
$inputPaths = @()

try {
  $inputPaths = @($GermanRetail, $JoystickOnly, $FrozenFullscreen,
    $FrenchRetail, $FrenchJoystickOnly, $FrenchFullscreen,
    $EnglishRetail, $EnglishJoystickOnly, $EnglishFullscreen,
    $Fury3Retail, $Fury3JoystickOnly, $Fury3Fullscreen) + $OtherRetail

  $inputPaths = @($inputPaths | ForEach-Object {
      try { (Resolve-Path -LiteralPath $_).Path } catch { throw 'Cannot resolve a reference input.' }
    })
  $report.inputs = @($inputPaths | ForEach-Object { Get-InputRecord $_ })

  $recipeHashes = @{}
  foreach ($recipe in @('hellbender/english', 'hellbender/french', 'hellbender/german', 'fury3/english')) {
    $edition = ($recipe -split '/')[1]
    $lockPath = Join-Path $projectRoot ('patches\' + $recipe.Replace('/', '\') + '\recipeLock.json')
    $lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
    if ($lock.edition.ToLowerInvariant() -ne $edition -or $lock.patchedSha256 -cnotmatch '^[0-9a-f]{64}$') {
      throw 'Invalid reference recipe identity.'
    }

    $recipeHashes[$recipe] = $lock.patchedSha256
  }

  $expected = @(
    '0e31a60398565723179c19a914c81b62f25b82d921c2fd57cf945f8c90c606d5',
    '722f321902fc76308a593809f28f29983589b3939e7c4c454471ee77f0f5e309',
    $recipeHashes['hellbender/german'],
    '497950d07723a887f801cec5ff4403c269b749345b0ddf6b314d6104993d8b92',
    'b3e04b8a97e8b50e643a96fc693393e84701f6d5f4814f6365295c4911a93e12',
    $recipeHashes['hellbender/french'],
    '0044f711594744450040cefe7fee14a1e0f2c1136434fe54a680e99ea9424a5b',
    '63bfd5026ab1fa216ff8efde2af4e79d782ecdeaaad2d31d2d47b9a763dbe09a',
    $recipeHashes['hellbender/english'],
    '288ce23095c8e9092bdb665b561f13f58c5b7f4b7c3cdb03f7059ab4149e8438',
    '3548df0193842904220d90a9cb07732dce8f06e8fdfd9111da50bd4382d0bf5c',
    $recipeHashes['fury3/english']
  )
  for ($index = 0; $index -lt $expected.Count; ++$index) {
    if ($report.inputs[$index].sha256 -ne $expected[$index]) {
      throw ('Reference input hash differs at position ' + ($index + 1) + '.')
    }
  }

  $report.sources = @(
    'tests\coreIntegrationTests.cpp', 'tests\CMakeLists.txt', 'scripts\runCoreTests.ps1',
    'scripts\core\nativeTools.ps1',
    'src\core\CMakeLists.txt', 'src\core\havGamePatcherCore.cpp',
    'src\core\gameRegistry.cpp', 'include\core\havGamePatcherCore.hpp',
    'include\core\gameRegistry.hpp', 'include\core\patchDefinition.hpp',
    'src\games\hellbender\patches.cpp', 'include\games\hellbender\patches.hpp',
    'src\games\fury3\patches.cpp', 'include\games\fury3\patches.hpp',
    'include\games\hellbender\english\fullscreenPatchData.hpp',
    'include\games\hellbender\french\fullscreenPatchData.hpp',
    'include\games\hellbender\german\fullscreenPatchData.hpp',
    'include\games\fury3\english\fullscreenPatchData.hpp'
  ) | ForEach-Object { Get-InputRecord (Join-Path $projectRoot $_) $_ }

  $cmakePath = FindCMake
  if ($Toolchain -eq 'msvc') {
    $configureArguments = @('-S', $testSource, '-B', $buildRoot, '-G', 'Visual Studio 17 2022', '-A', 'x64')
  }
  else {
    if (-not $MingwDirectory) { throw 'Pass -MingwDirectory with the native MinGW bin directory.' }

    $MingwDirectory = (Resolve-Path -LiteralPath $MingwDirectory).Path
    $compiler = Join-Path $MingwDirectory 'c++.exe'
    $make = Join-Path $MingwDirectory 'mingw32-make.exe'
    if (-not (Test-Path -LiteralPath $compiler -PathType Leaf) -or -not (Test-Path -LiteralPath $make -PathType Leaf)) {
      throw 'The MinGW directory must contain c++.exe and mingw32-make.exe.'
    }

    $processOptions.Environment['PATH'] = $MingwDirectory + ';' + $env:PATH
    $configureArguments = @('-S', $testSource, '-B', $buildRoot, '-G', 'MinGW Makefiles',
      ('-DCMAKE_CXX_COMPILER=' + $compiler), ('-DCMAKE_MAKE_PROGRAM=' + $make), ('-DCMAKE_BUILD_TYPE=' + $Configuration))
  }

  $report.configure = InvokeNativeProcess @processOptions -Program $cmakePath -Arguments $configureArguments -LogName 'configure'
  if ($report.configure.exitCode -ne 0) { throw 'Core test configure failed. See configure logs.' }

  $report.build = InvokeNativeProcess @processOptions -Program $cmakePath -Arguments @('--build', $buildRoot, '--config', $Configuration, '--parallel') -LogName 'build'
  if ($report.build.exitCode -ne 0) { throw 'Core test build failed. See build logs.' }

  $program = if ($Toolchain -eq 'msvc') {
    Join-Path $buildRoot ($Configuration + '\coreIntegrationTests.exe')
  }
  else {
    Join-Path $buildRoot 'coreIntegrationTests.exe'
  }

  $nativeArguments = @($inputPaths[0..11]) + @($fixtureRoot)
  for ($index = 12; $index -lt $inputPaths.Count; ++$index) {
    $nativeArguments += $inputPaths[$index]
  }

  $report.executable = Get-InputRecord $program
  $execution = InvokeNativeProcess @processOptions -Program $program -Arguments $nativeArguments -LogName 'tests'
  $report.testExitCode = $execution.exitCode
  if ($execution.stdout.Trim()) {
    $report.tests = $execution.stdout | ConvertFrom-Json
  }

  if ($execution.exitCode -ne 0) { throw 'Core integration tests failed. See tests logs.' }
  if (-not $report.tests -or $report.tests.failures -ne 0) { throw 'Test result is missing or reports failures.' }

  $report.status = 'passed'
  $exitCode = 0
}
catch {
  $report.error = $_.Exception.Message
}
finally {
  for ($index = 0; $index -lt $report.inputs.Count; ++$index) {
    $before = $report.inputs[$index]
    try {
      $after = Get-InputRecord $inputPaths[$index]
      $unchanged = ($after.sha256 -eq $before.sha256 -and $after.size -eq $before.size)
      $report.inputPreservation += [ordered]@{ name = $before.name; unchanged = $unchanged; afterSha256 = $after.sha256 }
      if (-not $unchanged) { $report.status = 'failed'; $exitCode = 1 }
    }
    catch {
      $report.inputPreservation += [ordered]@{ name = $before.name; unchanged = $false; error = $_.Exception.Message }
      $report.status = 'failed'
      $exitCode = 1
    }
  }

  $reportPath = Join-Path $runRoot 'result.json'
  $report | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $reportPath -Encoding UTF8
  Write-Output $reportPath
  Write-Output ('Status: ' + $report.status)
}

exit $exitCode
