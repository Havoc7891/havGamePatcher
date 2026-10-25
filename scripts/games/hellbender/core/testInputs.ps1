# SPDX-License-Identifier: MIT

function ReadProfileNumber($Value) {
  if ($null -eq $Value) {
    throw 'A required profile number is missing.'
  }

  $text = [string]$Value
  if ($text -match '^0x[0-9a-fA-F]+$') {
    return [Convert]::ToUInt32($text.Substring(2), 16)
  }

  if ($text -notmatch '^\d+$') {
    throw 'A profile number is invalid.'
  }

  return [Convert]::ToUInt32($text, 10)
}

function ReadHellbenderInputs([string]$ProjectRoot, [string[]]$Edition, [switch]$IncludePayload) {
  $records = @()

  foreach ($name in @($Edition | Select-Object -Unique)) {
    $slug = $name.ToLowerInvariant()
    $profilePath = Join-Path $ProjectRoot ('patches\hellbender\' + $slug + '\versionProfile.json')
    $lockPath = Join-Path $ProjectRoot ('patches\hellbender\' + $slug + '\recipeLock.json')
    $outputRoot = Join-Path $ProjectRoot ('out\patches\hellbender\' + $slug)
    $imagePath = Join-Path $outputRoot 'HELLBEND.fullscreen.EXE'

    $profile = Get-Content -LiteralPath $profilePath -Raw | ConvertFrom-Json
    $lock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
    $profileHash = GetSha256 $profilePath
    $imageHash = GetSha256 $imagePath
    if ($lock.edition -cne $name -or $profile.language -cne $name -or
      $profileHash -cne $lock.profileSha256 -or $imageHash -cne $lock.patchedSha256 -or
      (Get-Item -LiteralPath $imagePath).Length -ne $lock.patchedSize) {
      throw 'Maintained output or profile does not match its recipe lock.'
    }

    $files = @(
      @{ path = $imagePath; sha256 = $imageHash },
      @{ path = $profilePath; sha256 = $profileHash },
      @{ path = $lockPath; sha256 = (GetSha256 $lockPath) }
    )
    $record = [ordered]@{
      name = $name; slug = $slug; image = $imagePath; profileData = $profile
      imageHash = $imageHash; profileHash = $profileHash
    }

    if ($IncludePayload) {
      $payloadPath = Join-Path $outputRoot 'layoutPayload.normalized.dll'
      $addressPath = Join-Path $outputRoot 'gameAddresses.hpp'
      $recipePath = Join-Path $outputRoot 'recipeReport.json'

      $recipe = Get-Content -LiteralPath $recipePath -Raw | ConvertFrom-Json
      $payloadHash = GetSha256 $payloadPath
      if ($payloadHash -cne $lock.payloadSha256 -or $recipe.edition -cne $name -or
        $recipe.patchedSha256 -cne $imageHash -or $recipe.profileSha256 -cne $profileHash -or
        $recipe.payloadSha256 -cne $payloadHash -or
        $recipe.transformations.payload.payloadSha256 -cne $payloadHash) {
        throw 'Maintained payload or recipe does not match its recipe lock.'
      }

      $record.payload = $payloadPath
      $record.payloadHash = $payloadHash
      $record.origin = ReadProfileNumber $recipe.transformations.payload.payloadEmbeddingRvaOrigin
      $files += @(
        @{ path = $payloadPath; sha256 = $payloadHash },
        @{ path = $addressPath; sha256 = (GetSha256 $addressPath) },
        @{ path = $recipePath; sha256 = (GetSha256 $recipePath) }
      )
    }

    $record.files = $files
    $records += [pscustomobject]$record
  }

  if ($records.Count -eq 0) {
    throw 'Select at least one edition.'
  }

  return $records
}

function AssertHellbenderInputsUnchanged([object[]]$Records) {
  foreach ($record in $Records) {
    foreach ($file in $record.files) {
      if ((GetSha256 $file.path) -cne $file.sha256) {
        throw 'A maintained input changed during the test run.'
      }
    }
  }
}
