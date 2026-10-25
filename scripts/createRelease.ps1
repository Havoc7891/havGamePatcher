# SPDX-License-Identifier: MIT

#Requires -Version 5.1

param(
  [string]$CMakePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'core\nativeTools.ps1')

$stageRoot = $null
$cleanupStage = $true

function GetFileRecord([string]$Path) {
  $item = Get-Item -LiteralPath $Path
  if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw ('Expected a regular file: ' + $Path)
  }

  return @{
    Path   = $item.FullName
    Length = $item.Length
    Hash   = GetSha256 $item.FullName
  }
}

function AssertFileMatches([string]$Path, [hashtable]$Expected) {
  $actual = GetFileRecord $Path
  if ($actual.Length -ne $Expected.Length -or $actual.Hash -cne $Expected.Hash) {
    throw ('File changed or failed verification: ' + $Path)
  }
}

function GetReleaseVersion([string]$HeaderPath, [string]$ReadmePath) {
  $header = [IO.File]::ReadAllText($HeaderPath)
  $display = [regex]::Matches($header, '(?m)^#define HAVGAMEPATCHER_DISPLAY_VERSION "(\d+\.\d+\.\d+)"\s*$')
  $numbers = [regex]::Matches($header, '(?m)^#define HAVGAMEPATCHER_VERSION (\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*$')
  if ($display.Count -ne 1 -or $numbers.Count -ne 1) {
    throw 'The shared version header must define one display version and one four-part version.'
  }

  $parts = @(1..4 | ForEach-Object { [int]$numbers[0].Groups[$_].Value })
  if (@($parts | Where-Object { $_ -gt 65535 }).Count -ne 0) {
    throw 'Windows version components must be between 0 and 65535.'
  }

  $displayVersion = $display[0].Groups[1].Value
  if ($displayVersion -cne ($parts[0..2] -join '.')) {
    throw 'The display version does not match the first three Windows version components.'
  }

  $readme = [IO.File]::ReadAllText($ReadmePath)
  $readmeVersion = [regex]::Matches($readme, '(?m)^VERSION (\d+\.\d+\.\d+)\s*$')
  if ($readmeVersion.Count -ne 1 -or $readmeVersion[0].Groups[1].Value -cne $displayVersion) {
    throw 'Update the VERSION line in README.txt to match the shared version header.'
  }

  return @{ Display = $displayVersion; Windows = ($parts -join '.') }
}

function InvokeBuildTool([string]$Program, [string[]]$Arguments, [string]$Name) {
  $result = InvokeNativeProcess -Program $Program -Arguments $Arguments -WorkingDirectory $projectRoot `
    -LogDirectory $logRoot -LogName $Name -Environment @{}

  if ($result.stdout.Trim()) {
    Write-Host $result.stdout.TrimEnd()
  }

  if ($result.stderr.Trim()) {
    Write-Host $result.stderr.TrimEnd()
  }

  if ($result.exitCode -ne 0) {
    throw ('CMake ' + $Name + ' failed with exit code ' + $result.exitCode + '. See ' + $logRoot)
  }

  return $result.stdout
}

function AssertReleaseExecutable([string]$Path, [string]$ExpectedVersion) {
  $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
  $fileVersion = @($info.FileMajorPart, $info.FileMinorPart, $info.FileBuildPart, $info.FilePrivatePart) -join '.'
  $productVersion = @($info.ProductMajorPart, $info.ProductMinorPart, $info.ProductBuildPart, $info.ProductPrivatePart) -join '.'
  if ($info.FileVersion -cne $ExpectedVersion -or $info.ProductVersion -cne $ExpectedVersion -or
    $fileVersion -cne $ExpectedVersion -or $productVersion -cne $ExpectedVersion) {
    throw 'The built executable version does not match the shared version header.'
  }

  if ($info.IsDebug -or $info.ProductName -cne 'havGamePatcher') {
    throw 'The built executable is not a havGamePatcher Release build.'
  }

  $stream = [IO.File]::OpenRead($Path)
  $reader = New-Object IO.BinaryReader($stream)

  try {
    if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5A4D) {
      throw 'The built executable has no valid DOS header.'
    }

    $stream.Position = 0x3C
    $peOffset = $reader.ReadUInt32()
    if ($peOffset -lt 64 -or ($peOffset + 26) -gt $stream.Length) {
      throw 'The built executable has an invalid PE header offset.'
    }

    $stream.Position = $peOffset
    if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) {
      throw 'The release package requires a Windows x64 executable.'
    }

    $stream.Position = $peOffset + 22
    $characteristics = $reader.ReadUInt16()
    if (-not ($characteristics -band 0x0002) -or ($characteristics -band 0x2000) -or
      $reader.ReadUInt16() -ne 0x020B) {
      throw 'The release package requires a PE32+ executable, not a DLL.'
    }
  }
  finally {
    $reader.Dispose()
  }
}

function AssertArchive([string]$Path, [object[]]$Files) {
  $archive = [IO.Compression.ZipFile]::OpenRead($Path)

  try {
    if ($archive.Entries.Count -ne $Files.Count) {
      throw 'The release ZIP contains an unexpected number of entries.'
    }

    $seen = @{}
    foreach ($entry in $archive.Entries) {
      $expected = @($Files | Where-Object { $_.Entry -ceq $entry.FullName })
      if ($expected.Count -ne 1 -or $seen.ContainsKey($entry.FullName)) {
        throw ('The release ZIP contains an unexpected or duplicate entry: ' + $entry.FullName)
      }

      $seen[$entry.FullName] = $true
      $stream = $entry.Open()
      $sha256 = [Security.Cryptography.SHA256]::Create()

      try {
        $hash = [BitConverter]::ToString($sha256.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
        if ($entry.Length -ne $expected[0].Source.Length -or $hash -cne $expected[0].Source.Hash) {
          throw ('The release ZIP failed content verification: ' + $entry.FullName)
        }
      }
      finally {
        $sha256.Dispose()
        $stream.Dispose()
      }
    }
  }
  finally {
    $archive.Dispose()
  }
}

try {
  if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'Run this script on Windows with Visual Studio 2022 C++ build tools and CMake 4 or newer.'
  }

  $headerPath = Join-Path $projectRoot 'include\havGamePatcherInfo.hpp'
  $headerRecord = GetFileRecord $headerPath
  $version = GetReleaseVersion $headerPath (Join-Path $projectRoot 'README.txt')

  $files = @(
    @{ Entry = 'README.txt'; Source = (GetFileRecord (Join-Path $projectRoot 'README.txt')) },
    @{ Entry = 'LICENSE.txt'; Source = (GetFileRecord (Join-Path $projectRoot 'LICENSE')) },
    @{ Entry = 'thirdPartyNotices.md'; Source = (GetFileRecord (Join-Path $projectRoot 'thirdPartyNotices.md')) }
  )

  $licenseDirectory = Get-Item -LiteralPath (Join-Path $projectRoot 'licenses')
  if (-not $licenseDirectory.PSIsContainer -or
    ($licenseDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'licenses must be a regular directory containing the release license texts.'
  }

  $licenses = @(Get-ChildItem -LiteralPath $licenseDirectory.FullName -Force | Sort-Object Name)
  if ($licenses.Count -eq 0) {
    throw 'The release requires third-party license texts in licenses.'
  }

  foreach ($license in $licenses) {
    if ($license.PSIsContainer -or $license.Extension -cne '.txt') {
      throw 'The licenses directory must contain only .txt files, with no subdirectories.'
    }

    $files += @{ Entry = ('licenses/' + $license.Name); Source = (GetFileRecord $license.FullName) }
  }

  $runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
  $logRoot = Join-Path $projectRoot ('out\diagnostics\releasePackaging\' + $runId)
  New-Item -ItemType Directory -Path $logRoot -Force | Out-Null

  $cmake = if ($CMakePath) {
    (GetFileRecord $CMakePath).Path
  }
  else {
    FindCMake -MissingMessage 'CMake 4 or newer was not found. Install CMake or supply -CMakePath.'
  }

  $cmakeVersionText = InvokeBuildTool $cmake @('--version') 'version'
  $cmakeVersion = [regex]::Match($cmakeVersionText, '(?m)^cmake version (\d+)\.(\d+)\.(\d+)')
  if (-not $cmakeVersion.Success -or [int]$cmakeVersion.Groups[1].Value -lt 4) {
    throw 'The selected CMake must be version 4 or newer.'
  }

  Write-Host 'Configuring the MSVC x64 release.'
  $null = InvokeBuildTool $cmake @('--preset', 'msvc') 'configure'

  Write-Host 'Building the MSVC x64 release.'
  $null = InvokeBuildTool $cmake @('--build', '--preset', 'msvc-release', '--parallel') 'build'

  AssertFileMatches $headerPath $headerRecord
  $executable = GetFileRecord (Join-Path $projectRoot 'out\build\msvc\Release\havGamePatcher.exe')
  AssertReleaseExecutable $executable.Path $version.Windows
  $files = @(@{ Entry = 'havGamePatcher.exe'; Source = $executable }) + $files

  $stageRoot = Join-Path $projectRoot ('out\staging\release-' + $runId)
  $payloadRoot = Join-Path $stageRoot 'payload'
  New-Item -ItemType Directory -Path $payloadRoot -Force | Out-Null

  foreach ($file in $files) {
    AssertFileMatches $file.Source.Path $file.Source
    $stagedPath = Join-Path $payloadRoot $file.Entry.Replace('/', '\')
    New-Item -ItemType Directory -Path (Split-Path -Parent $stagedPath) -Force | Out-Null
    Copy-Item -LiteralPath $file.Source.Path -Destination $stagedPath
    AssertFileMatches $stagedPath $file.Source
  }

  Add-Type -AssemblyName System.IO.Compression
  Add-Type -AssemblyName System.IO.Compression.FileSystem

  $archiveName = 'havGamePatcher-' + $version.Display + '-win-x64.zip'
  $temporaryArchive = Join-Path $stageRoot $archiveName
  $archive = [IO.Compression.ZipFile]::Open($temporaryArchive, [IO.Compression.ZipArchiveMode]::Create)

  try {
    foreach ($file in $files) {
      $stagedPath = Join-Path $payloadRoot $file.Entry.Replace('/', '\')
      $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
        $archive, $stagedPath, $file.Entry, [IO.Compression.CompressionLevel]::Optimal)
    }
  }
  finally {
    $archive.Dispose()
  }

  AssertArchive $temporaryArchive $files
  foreach ($file in $files) {
    AssertFileMatches $file.Source.Path $file.Source
    AssertFileMatches (Join-Path $payloadRoot $file.Entry.Replace('/', '\')) $file.Source
  }
  AssertFileMatches $headerPath $headerRecord

  $archiveRecord = GetFileRecord $temporaryArchive
  $checksumText = $archiveRecord.Hash + '  ' + $archiveName + "`n"
  $temporaryChecksum = $temporaryArchive + '.sha256'
  [IO.File]::WriteAllText($temporaryChecksum, $checksumText, [Text.Encoding]::ASCII)

  $releaseRoot = Join-Path $projectRoot 'out\releases'
  New-Item -ItemType Directory -Path $releaseRoot -Force | Out-Null
  $releasePath = Join-Path $releaseRoot $archiveName
  $outputs = @(
    @{ Source = $temporaryArchive; Destination = $releasePath; Backup = (Join-Path $stageRoot 'previous.zip'); Published = $false; Previous = $null },
    @{ Source = $temporaryChecksum; Destination = ($releasePath + '.sha256'); Backup = (Join-Path $stageRoot 'previous.sha256'); Published = $false; Previous = $null }
  )

  foreach ($output in $outputs) {
    if (Test-Path -LiteralPath $output.Destination) {
      $output.Previous = GetFileRecord $output.Destination
    }
  }

  try {
    foreach ($output in $outputs) {
      if ($output.Previous) {
        [IO.File]::Replace($output.Source, $output.Destination, $output.Backup)
      }
      else {
        [IO.File]::Move($output.Source, $output.Destination)
      }

      $output.Published = $true
    }

    AssertFileMatches $releasePath $archiveRecord
    if ([IO.File]::ReadAllText($releasePath + '.sha256') -cne $checksumText) {
      throw 'The release checksum file failed verification.'
    }
  }
  catch {
    $publicationError = $_

    try {
      for ($index = $outputs.Count - 1; $index -ge 0; --$index) {
        $output = $outputs[$index]
        if ($output.Published) {
          if ($output.Previous) {
            [IO.File]::Replace($output.Backup, $output.Destination, ($output.Backup + '.failed'))
            AssertFileMatches $output.Destination $output.Previous
          }
          else {
            [IO.File]::Delete($output.Destination)
          }
        }
      }
    }
    catch {
      $cleanupStage = $false
      throw ('Release publication and rollback failed. Backup files remain in ' + $stageRoot + '. ' + $_.Exception.Message)
    }

    throw $publicationError
  }

  Write-Output ('Release package: ' + $releasePath)
  Write-Output ('SHA256: ' + $archiveRecord.Hash)
  Write-Output ('Checksum file: ' + $releasePath + '.sha256')
}
catch {
  Write-Error -Message $_.Exception.Message -ErrorAction Continue
  exit 1
}
finally {
  if ($cleanupStage -and $stageRoot -and (Test-Path -LiteralPath $stageRoot)) {
    Remove-Item -LiteralPath $stageRoot -Recurse -Force
  }
}
