# SPDX-License-Identifier: MIT

function GetSha256([string]$Path) {
  return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function QuoteNativeArgument([string]$Value) {
  return '"' + (($Value -replace '(\\*)"', '$1$1\"') -replace '(\\+)$', '$1$1') + '"'
}

function FindCMake([string]$MissingMessage = 'CMake 4 or newer is required. Install CMake or add it to PATH.') {
  foreach ($directory in @($env:ProgramW6432, $env:ProgramFiles)) {
    if ($directory) {
      $candidate = Join-Path $directory 'CMake\bin\cmake.exe'
      if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        return $candidate
      }
    }
  }

  $command = Get-Command cmake.exe -CommandType Application -ErrorAction SilentlyContinue
  if ($command) {
    return $command.Source
  }

  throw $MissingMessage
}

function InvokeNativeProcess(
  [string]$Program,
  [string[]]$Arguments,
  [string]$WorkingDirectory,
  [string]$LogDirectory,
  [string]$LogName,
  [hashtable]$Environment = @{},
  [int]$Timeout = 0
) {
  $start = New-Object Diagnostics.ProcessStartInfo
  $start.FileName = $Program
  $start.Arguments = (($Arguments | ForEach-Object { QuoteNativeArgument $_ }) -join ' ')
  $start.WorkingDirectory = $WorkingDirectory
  $start.UseShellExecute = $false
  $start.CreateNoWindow = $true
  $start.RedirectStandardOutput = $true
  $start.RedirectStandardError = $true

  foreach ($name in $Environment.Keys) {
    $start.EnvironmentVariables[$name] = $Environment[$name]
  }

  $process = New-Object Diagnostics.Process
  $process.StartInfo = $start

  try {
    if (-not $process.Start()) {
      throw 'Cannot start the requested build or fixture process.'
    }

    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $timedOut = $false

    if ($Timeout -gt 0) {
      $timedOut = -not $process.WaitForExit($Timeout)
      if ($timedOut) {
        $process.Kill()
        $process.WaitForExit()
      }
    }
    else {
      $process.WaitForExit()
    }

    $output = $stdout.Result
    $errors = $stderr.Result
    [IO.File]::WriteAllText((Join-Path $LogDirectory ($LogName + '.stdout.txt')), $output)
    [IO.File]::WriteAllText((Join-Path $LogDirectory ($LogName + '.stderr.txt')), $errors)

    if ($timedOut) {
      throw 'A build or fixture process exceeded its deadline.'
    }

    return [ordered]@{ exitCode = $process.ExitCode; stdout = $output; stderr = $errors }
  }
  finally {
    $process.Dispose()
  }
}
