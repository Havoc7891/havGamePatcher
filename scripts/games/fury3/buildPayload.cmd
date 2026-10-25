@echo off
rem SPDX-License-Identifier: MIT

setlocal

if not "%~1"=="English" (
    echo Usage: buildPayload.cmd English
    exit /b 1
)

set "payloadSource=%~dp0..\..\..\src\games\fury3\core"
set "payloadInclude=%~dp0..\..\..\include\games\fury3\core"
set "payloadOutput=%~dp0..\..\..\out\patches\fury3\english"
if not exist "%payloadOutput%\preparedProfile.json" (
    echo Run generateRecipe.py --edition English --prepare first.
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
cl /nologo /c /std:c++17 /W4 /WX /O1 /Oi /GS- /GR- /EHs-c- /Zl /Gs999999 /arch:IA32 /I"%payloadInclude%" /FofullscreenPayload.obj "%payloadSource%\fullscreenPayload.cpp"
if errorlevel 1 goto failed

link /nologo /DLL /NOENTRY /NODEFAULTLIB /MACHINE:X86 /INCREMENTAL:NO /OPT:REF /OPT:NOICF /DYNAMICBASE:NO /FIXED:NO /BASE:0x900000 /SAFESEH:NO /MERGE:.rdata=.text /SECTION:.text,ER /DEF:"%payloadSource%\fullscreenPayload.def" /OUT:fullscreenPayload.dll /MAP:fullscreenPayload.map fullscreenPayload.obj
if errorlevel 1 goto failed

popd
exit /b 0

:failed
popd
exit /b 1
