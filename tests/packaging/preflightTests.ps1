# SPDX-License-Identifier: MIT

#Requires -Version 5.1

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$sourceScript = Join-Path $projectRoot 'scripts\createRelease.ps1'
. (Join-Path $projectRoot 'scripts\core\nativeTools.ps1')
$runRoot = Join-Path $projectRoot ('out\test\packagingPreflight-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $runRoot | Out-Null
$tokens = $null
$parseErrors = $null
$null = [Management.Automation.Language.Parser]::ParseFile($sourceScript, [ref]$tokens, [ref]$parseErrors)

if ($parseErrors.Count -ne 0) {
  throw ($parseErrors | Out-String)
}

$results = @()
foreach ($case in @('missingReadme', 'mismatchedReadme', 'missingNotices')) {
  $fixture = Join-Path $runRoot $case
  foreach ($directory in @('scripts\core', 'include', 'licenses', 'out\releases')) {
    New-Item -ItemType Directory -Path (Join-Path $fixture $directory) -Force | Out-Null
  }

  Copy-Item -LiteralPath $sourceScript -Destination (Join-Path $fixture 'scripts\createRelease.ps1')
  Copy-Item -LiteralPath (Join-Path $projectRoot 'scripts\core\nativeTools.ps1') -Destination (Join-Path $fixture 'scripts\core\nativeTools.ps1')
  Copy-Item -LiteralPath (Join-Path $projectRoot 'include\havGamePatcherInfo.hpp') -Destination (Join-Path $fixture 'include\havGamePatcherInfo.hpp')
  Copy-Item -LiteralPath (Join-Path $projectRoot 'README.txt') -Destination (Join-Path $fixture 'README.txt')
  [IO.File]::WriteAllText((Join-Path $fixture 'LICENSE'), 'fixture license')
  [IO.File]::WriteAllText((Join-Path $fixture 'thirdPartyNotices.md'), 'fixture notice')
  [IO.File]::WriteAllText((Join-Path $fixture 'licenses\dependency.txt'), 'fixture dependency license')
  $header = [IO.File]::ReadAllText((Join-Path $fixture 'include\havGamePatcherInfo.hpp'))
  $version = [regex]::Match($header, '#define HAVGAMEPATCHER_DISPLAY_VERSION "([^"]+)"').Groups[1].Value
  $archive = Join-Path $fixture ('out\releases\havGamePatcher-' + $version + '-win-x64.zip')
  $checksum = $archive + '.sha256'
  [IO.File]::WriteAllText($archive, 'existing release archive must be preserved')
  [IO.File]::WriteAllText($checksum, 'existing checksum must be preserved')
  $before = @((Get-FileHash $archive).Hash, (Get-FileHash $checksum).Hash)

  if ($case -eq 'missingReadme') {
    Remove-Item -LiteralPath (Join-Path $fixture 'README.txt')
    $expectedError = 'README.txt'
  }
  elseif ($case -eq 'mismatchedReadme') {
    [IO.File]::WriteAllText((Join-Path $fixture 'README.txt'), "VERSION 99.98.97`r`n")
    $expectedError = 'Update the VERSION line'
  }
  else {
    Remove-Item -LiteralPath (Join-Path $fixture 'thirdPartyNotices.md')
    $expectedError = 'thirdPartyNotices.md'
  }

  $process = InvokeNativeProcess -Program (Join-Path $PSHOME 'powershell.exe') `
    -Arguments @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $fixture 'scripts\createRelease.ps1')) `
    -WorkingDirectory $fixture -LogDirectory $fixture -LogName 'preflight' -Environment @{} -Timeout 30000

  $after = @((Get-FileHash $archive).Hash, (Get-FileHash $checksum).Hash)
  $preserved = $before[0] -ceq $after[0] -and $before[1] -ceq $after[1]
  $noBuildStarted = -not (Test-Path -LiteralPath (Join-Path $fixture 'out\diagnostics'))
  $expectedFailure = $process.stderr.Contains($expectedError)
  $passed = $process.exitCode -ne 0 -and $expectedFailure -and $preserved -and $noBuildStarted
  $results += [ordered]@{
    case                     = $case
    exitCode                 = $process.exitCode
    expectedFailure          = $expectedFailure
    existingReleasePreserved = $preserved
    noBuildStarted           = $noBuildStarted
    passed                   = $passed
  }

  if (-not $passed) {
    throw ('Packaging preflight case failed: ' + $case)
  }
}

$report = [ordered]@{
  scriptSha256      = (Get-FileHash -LiteralPath $sourceScript -Algorithm SHA256).Hash.ToLowerInvariant()
  powerShellVersion = $PSVersionTable.PSVersion.ToString()
  parseErrors       = $parseErrors.Count
  cases             = $results
  passed            = $true
}

$reportPath = Join-Path $runRoot 'validation.json'
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
Write-Output $reportPath
Write-Output 'All isolated preflight failures preserved the existing release and did not start a build.'
