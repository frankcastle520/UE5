@ECHO OFF

REM Copyright Epic Games, Inc. All Rights Reserved.

setlocal

set PATH_TO_CMAKE_FILE=%CD%\..\

REM Temporary build directories (used as working directories when running CMake)
set VS2015_X64_PATH="%PATH_TO_CMAKE_FILE%\..\lib\Win64\VS2015\Build"
set VS2022_ARM64_PATH="%PATH_TO_CMAKE_FILE%\..\lib\Win64\VS2015\ARM64\Build"
set VS2022_X64_CLANG_PATH="%PATH_TO_CMAKE_FILE%\..\lib\Win64\Clang\Build"

set DEVENV_EXE_PATH="%VS170COMNTOOLS%\..\IDE\devenv.exe"

REM Build for VS2022 (x64 Clang/LLVM)
echo Generating ICU solution for VS2022 (x64 Clang/LLVM)...
if exist %VS2022_X64_CLANG_PATH% (rmdir %VS2022_X64_CLANG_PATH% /s/q)
mkdir %VS2022_X64_CLANG_PATH%
cd %VS2022_X64_CLANG_PATH%
cmake -G "Visual Studio 17 2022" -A x64 %PATH_TO_CMAKE_FILE% -T ClangCL
echo Building ICU solution for VS2022 (x64 Clang, Debug)...
%DEVENV_EXE_PATH% icu.sln /Build Debug
echo Building ICU solution for VS2022 (x64 Clang, Release)...
%DEVENV_EXE_PATH% icu.sln /Build Release
echo Building ICU solution for VS2022 (x64 Clang, RelWithDebInfo)...
%DEVENV_EXE_PATH% icu.sln /Build RelWithDebInfo
cd %PATH_TO_CMAKE_FILE%
copy /B/Y "%VS2022_X64_CLANG_PATH%\icu.dir\Debug\icu.pdb" "%VS2022_X64_CLANG_PATH%\..\Debug\icu.pdb"
copy /B/Y "%VS2022_X64_CLANG_PATH%\icu.dir\RelWithDebInfo\icu.pdb" "%VS2022_X64_CLANG_PATH%\..\RelWithDebInfo\icu.pdb"
rmdir %VS2022_X64_CLANG_PATH% /s/q

REM Build for VS2015 (x64 64-bit)
echo Generating ICU solution for VS2015 (64-bit)...
if exist %VS2015_X64_PATH% (rmdir %VS2015_X64_PATH% /s/q)
mkdir %VS2015_X64_PATH%
cd %VS2015_X64_PATH%
cmake -G "Visual Studio 14 2015 Win64" %PATH_TO_CMAKE_FILE%
echo Building ICU solution for VS2015 (x64 64-bit, Debug)...
"%VS140COMNTOOLS%\..\IDE\devenv.exe" icu.sln /Build Debug
echo Building ICU solution for VS2015 (x64 64-bit, Release)...
"%VS140COMNTOOLS%\..\IDE\devenv.exe" icu.sln /Build Release
echo Building ICU solution for VS2015 (x64 64-bit, RelWithDebInfo)...
"%VS140COMNTOOLS%\..\IDE\devenv.exe" icu.sln /Build RelWithDebInfo
cd %PATH_TO_CMAKE_FILE%
copy /B/Y "%VS2015_X64_PATH%\icu.dir\Debug\icu.pdb" "%VS2015_X64_PATH%\..\Debug\icu.pdb"
copy /B/Y "%VS2015_X64_PATH%\icu.dir\RelWithDebInfo\icu.pdb" "%VS2015_X64_PATH%\..\RelWithDebInfo\icu.pdb"
rmdir %VS2015_X64_PATH% /s/q

REM Build for VS2022 (ARM64 64-bit)
echo Generating ICU solution for VS2022 (64-bit)...
if exist %VS2022_ARM64_PATH% (rmdir %VS2022_ARM64_PATH% /s/q)
mkdir %VS2022_ARM64_PATH%
cd %VS2022_ARM64_PATH%
cmake -G "Visual Studio 17 2022" %PATH_TO_CMAKE_FILE% -A ARM64
echo Building ICU solution for VS2022 (ARM64 64-bit, Debug)...
%DEVENV_EXE_PATH% icu.sln /Build Debug
echo Building ICU solution for VS2022 (ARM64 64-bit, Release)...
%DEVENV_EXE_PATH% icu.sln /Build Release
echo Building ICU solution for VS2022 (ARM64 64-bit, RelWithDebInfo)...
%DEVENV_EXE_PATH% icu.sln /Build RelWithDebInfo
cd %PATH_TO_CMAKE_FILE%
copy /B/Y "%VS2022_ARM64_PATH%\icu.dir\Debug\icu.pdb" "%VS2022_ARM64_PATH%\..\Debug\icu.pdb"
copy /B/Y "%VS2022_ARM64_PATH%\icu.dir\RelWithDebInfo\icu.pdb" "%VS2022_ARM64_PATH%\..\RelWithDebInfo\icu.pdb"
rmdir %VS2022_ARM64_PATH% /s/q

:exit
endlocal
