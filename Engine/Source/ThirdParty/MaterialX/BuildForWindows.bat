@echo off
setlocal

rem Copyright Epic Games, Inc. All Rights Reserved.

if [%1]==[] goto usage
if [%2]==[] goto usage

set LIBRARY_NAME=MaterialX
set REPOSITORY_NAME=MaterialX

set LIBRARY_VERSION=%1

rem Set as VS2015 for backwards compatibility even though VS2022 is used
rem when building.
set COMPILER_VERSION_NAME=VS2015
set ARCH_NAME=%2

set BUILD_SCRIPT_LOCATION=%~dp0

set UE_MODULE_LOCATION=%BUILD_SCRIPT_LOCATION%

set SOURCE_LOCATION=%UE_MODULE_LOCATION%\%REPOSITORY_NAME%-%LIBRARY_VERSION%

set BUILD_LOCATION=%UE_MODULE_LOCATION%\Intermediate

set INSTALL_INCLUDEDIR=include
set INSTALL_LIB_DIR=%COMPILER_VERSION_NAME%\%ARCH_NAME%\lib

set INSTALL_LOCATION=%UE_MODULE_LOCATION%\Deploy\%REPOSITORY_NAME%-%LIBRARY_VERSION%
set INSTALL_INCLUDE_LOCATION=%INSTALL_LOCATION%\%INSTALL_INCLUDEDIR%
set INSTALL_WIN_LOCATION=%INSTALL_LOCATION%\%COMPILER_VERSION_NAME%\%ARCH_NAME%

if exist %BUILD_LOCATION% (
    rmdir %BUILD_LOCATION% /S /Q)
if exist %INSTALL_INCLUDE_LOCATION% (
    rmdir %INSTALL_INCLUDE_LOCATION% /S /Q)
if exist %INSTALL_WIN_LOCATION% (
    rmdir %INSTALL_WIN_LOCATION% /S /Q)

mkdir %BUILD_LOCATION%
pushd %BUILD_LOCATION%

echo Configuring build for %LIBRARY_NAME% version %LIBRARY_VERSION%...
cmake -G "Visual Studio 17 2022" %SOURCE_LOCATION%^
    -A %ARCH_NAME%^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_LOCATION%"^
    -DMATERIALX_INSTALL_INCLUDE_PATH="%INSTALL_INCLUDEDIR%"^
    -DMATERIALX_INSTALL_LIB_PATH="%INSTALL_LIB_DIR%"^
    -DMATERIALX_BUILD_TESTS=OFF^
    -DMATERIALX_TEST_RENDER=OFF^
    -DCMAKE_DEBUG_POSTFIX=_d
if %errorlevel% neq 0 exit /B %errorlevel%

echo Building %LIBRARY_NAME% for Debug...
cmake --build . --config Debug -j8
if %errorlevel% neq 0 exit /B %errorlevel%

echo Installing %LIBRARY_NAME% for Debug...
cmake --install . --config Debug
if %errorlevel% neq 0 exit /B %errorlevel%

echo Building %LIBRARY_NAME% for Release...
cmake --build . --config Release -j8
if %errorlevel% neq 0 exit /B %errorlevel%

echo Installing %LIBRARY_NAME% for Release...
cmake --install . --config Release
if %errorlevel% neq 0 exit /B %errorlevel%

popd

echo Done.

goto :eof

:usage
echo Usage: BuildForWindows ^<version^> ^<architecture: x64 or arm64^>
exit /B 1

endlocal
