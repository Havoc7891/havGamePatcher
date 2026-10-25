# SPDX-License-Identifier: MIT

#Requires -Version 5.1

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$sourceScript = Join-Path $projectRoot 'scripts\createRelease.ps1'
. (Join-Path $projectRoot 'scripts\core\nativeTools.ps1')
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($sourceScript, [ref]$tokens, [ref]$parseErrors)

if ($parseErrors.Count -ne 0) {
  throw ($parseErrors | Out-String)
}

foreach ($name in @('GetFileRecord', 'AssertFileMatches')) {
  $definition = @($ast.EndBlock.Statements | Where-Object {
      $_ -is [Management.Automation.Language.FunctionDefinitionAst] -and $_.Name -ceq $name
    })
  if ($definition.Count -ne 1) {
    throw ('Could not locate production function ' + $name)
  }

  Invoke-Expression $definition[0].Extent.Text
}

$main = @($ast.EndBlock.Statements | Where-Object { $_ -is [Management.Automation.Language.TryStatementAst] })
if ($main.Count -ne 1) {
  throw 'Could not identify the production script body.'
}

$statements = @($main[0].Body.Statements)
$publicationIndex = -1
for ($index = 0; $index -lt $statements.Count; ++$index) {
  if ($statements[$index] -is [Management.Automation.Language.AssignmentStatementAst] -and
    $statements[$index].Left.Extent.Text -ceq '$outputs') {
    $publicationIndex = $index
  }
}

if ($publicationIndex -lt 0 -or
  $statements[$publicationIndex + 1] -isnot [Management.Automation.Language.ForEachStatementAst] -or
  $statements[$publicationIndex + 2] -isnot [Management.Automation.Language.TryStatementAst]) {
  throw 'Could not identify the exact production publication block.'
}

$publication = ($statements[$publicationIndex..($publicationIndex + 2)] | ForEach-Object { $_.Extent.Text }) -join "`n"
$runRoot = Join-Path $projectRoot ('out\test\packagingPublication-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$results = @()

foreach ($case in @('replaceExistingArchive', 'createNewArchive')) {
  $fixture = Join-Path $runRoot $case
  $stageRoot = Join-Path $fixture 'stage'
  $releaseRoot = Join-Path $fixture 'releases'
  New-Item -ItemType Directory -Path $stageRoot, $releaseRoot -Force | Out-Null
  $releasePath = Join-Path $releaseRoot 'fixture.zip'
  if ($case -eq 'replaceExistingArchive') {
    [IO.File]::WriteAllText($releasePath, 'prior archive')
    $beforeArchive = GetFileRecord $releasePath
  }

  [IO.File]::WriteAllText(($releasePath + '.sha256'), 'prior checksum')
  $beforeChecksum = GetFileRecord ($releasePath + '.sha256')
  $temporaryArchive = Join-Path $stageRoot 'fixture.zip'
  $temporaryChecksum = $temporaryArchive + '.sha256'
  [IO.File]::WriteAllText($temporaryArchive, 'new archive')
  $archiveRecord = GetFileRecord $temporaryArchive
  $checksumText = $archiveRecord.Hash + "  fixture.zip`n"
  [IO.File]::WriteAllText($temporaryChecksum, $checksumText)
  $cleanupStage = $true
  $lock = [IO.File]::Open(($releasePath + '.sha256'), [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
  $failed = $false
  $outputs = $null

  try {
    try {
      Invoke-Expression $publication
    }
    catch {
      $failed = $true
      [IO.File]::WriteAllText((Join-Path $fixture 'expectedFailure.txt'), $_.Exception.Message)
    }
  }
  finally {
    $lock.Dispose()
  }

  AssertFileMatches ($releasePath + '.sha256') $beforeChecksum
  if ($case -eq 'replaceExistingArchive') {
    AssertFileMatches $releasePath $beforeArchive
  }
  elseif (Test-Path -LiteralPath $releasePath) {
    throw 'Rollback left a newly published archive behind.'
  }

  if (-not $failed -or -not $cleanupStage -or $null -eq $outputs -or
    -not $outputs[0].Published -or $outputs[1].Published) {
    throw 'The injected publication failure did not roll back successfully.'
  }

  $results += @{
    case                          = $case
    expectedFailureOccurred       = $failed
    archivePublishedBeforeFailure = $true
    checksumPublished             = $false
    priorOutputsRestored          = $true
  }
}

$report = [ordered]@{
  scriptSha256                     = (Get-FileHash -LiteralPath $sourceScript -Algorithm SHA256).Hash.ToLowerInvariant()
  powerShellVersion                = $PSVersionTable.PSVersion.ToString()
  productionPublicationBlockTested = $true
  cases                            = $results
  passed                           = $true
}

$reportPath = Join-Path $runRoot 'validation.json'
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
Write-Output $reportPath
Write-Output 'Locked-checksum failures restored the previous output state in both publication cases.'
