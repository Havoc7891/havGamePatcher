# SPDX-License-Identifier: MIT

#Requires -Version 5.1

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

& (Join-Path $projectRoot 'tests\packaging\preflightTests.ps1')
& (Join-Path $projectRoot 'tests\packaging\publicationTests.ps1')

Write-Output 'Packaging preflight and publication rollback checks passed.'
