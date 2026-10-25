@echo off
rem SPDX-License-Identifier: MIT

setlocal

if not "%~1"=="English" if not "%~1"=="French" if not "%~1"=="German" (
    echo Usage: buildPayload.cmd English, French or German
    exit /b 1
)

if "%~1"=="English" set "editionSlug=english"
if "%~1"=="French" set "editionSlug=french"
if "%~1"=="German" set "editionSlug=german"

set "payloadSource=%~dp0..\..\..\src\games\hellbender\core\"
set "payloadInclude=%~dp0..\..\..\include\games\hellbender\core"
set "payloadOutput=%~dp0..\..\..\out\patches\hellbender\%editionSlug%"
if not exist "%payloadOutput%\gameAddresses.hpp" (
    echo Run generateRecipe.py --edition %~1 --prepare first.
    exit /b 1
)

set "vswherePath=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswherePath%" exit /b 1

"%vswherePath%" -latest -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%payloadOutput%\visualStudioPath.txt"
if errorlevel 1 exit /b 1

set "visualStudioPath="
for /f "usebackq delims=" %%I in ("%payloadOutput%\visualStudioPath.txt") do set "visualStudioPath=%%I"
if not defined visualStudioPath exit /b 1

call "%visualStudioPath%\VC\Auxiliary\Build\vcvars32.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1

pushd "%payloadOutput%"
cl /nologo /c /std:c++17 /W4 /WX /O1 /Oi /GS- /GR- /EHs-c- /Zl /Gs999999 /arch:IA32 /FI"%payloadOutput%\gameAddresses.hpp" /I"%payloadInclude%" /FolayoutPayload.obj "%payloadSource%layoutPayload.cpp"
if errorlevel 1 goto failed

link /nologo /DLL /NOENTRY /NODEFAULTLIB /MACHINE:X86 /INCREMENTAL:NO /OPT:REF /OPT:NOICF /DYNAMICBASE:NO /FIXED:NO /BASE:0x900000 /SAFESEH:NO /MERGE:.rdata=.text /SECTION:.text,ER /DEF:"%payloadSource%layoutPayload.def" /OUT:layoutPayload.dll /MAP:layoutPayload.map layoutPayload.obj
if errorlevel 1 goto failed

popd
exit /b 0

:failed
popd
exit /b 1
