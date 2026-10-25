@echo off
rem SPDX-License-Identifier: MIT

setlocal
cd /d "%~dp0.."

set "build_preset=%~1"
if not defined build_preset set "build_preset=mingw-release"
set "configure_preset="

if "%build_preset%"=="mingw-debug" set "configure_preset=mingw-debug"
if "%build_preset%"=="mingw-release" set "configure_preset=mingw-release"
if "%build_preset%"=="msvc-debug" set "configure_preset=msvc"
if "%build_preset%"=="msvc-release" set "configure_preset=msvc"

if not defined configure_preset (
  echo Usage: compile.bat [mingw-debug^|mingw-release^|msvc-debug^|msvc-release]
  exit /b 1
)

cmake --preset "%configure_preset%"
if errorlevel 1 exit /b %errorlevel%

cmake --build --preset "%build_preset%" --parallel 14
exit /b %errorlevel%
