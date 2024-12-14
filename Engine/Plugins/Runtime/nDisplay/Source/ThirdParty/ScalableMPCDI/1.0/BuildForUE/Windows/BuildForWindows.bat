@rem Copyright Epic Games, Inc. All Rights Reserved.
@echo off

:: Build configuration
set PLATFORM_TOOLSET=v143
set BUILD_CONFIGURATION=Release
set MPCDI_SOURCES_ZIP=mpcdi.zip
set MPCDI_SOURCES_DIR=mpcdi

:: UE ThirdParty root
set ENGINE_THIRDPARTY_DIR=%~dp0..\..\..\..\..\..\..\..\..\Source\ThirdParty

:: MiniZip
set ZLIB_PATH=%ENGINE_THIRDPARTY_DIR%\zlib\1.3
set ZLIB_INC_PATH=%ZLIB_PATH%\include
set MINIZIP_INC_PATH=%ZLIB_INC_PATH%\minizip

:: libPNG
set LIBPNG_PATH=%ENGINE_THIRDPARTY_DIR%\libPNG\libPNG-1.5.2
set LIBPNG_INC_PATH=%LIBPNG_PATH%

:: TinyXML2
SET TINYXML2_PATH=%ENGINE_THIRDPARTY_DIR%\TinyXML2\9.0.0
set TINYXML2_INC_PATH=%TINYXML2_PATH%\include

:: Include paths
set ADDITIONAL_INC_PATHS="%ZLIB_INC_PATH%;%LIBPNG_INC_PATH%;%TINYXML2_INC_PATH%;%MINIZIP_INC_PATH%;"


:: Find MSBuild
for /f "delims=" %%V in ('"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath') do SET _vsinstall=%%V
if errorlevel 1 goto VStudioMissing
SET _msbuild=%_vsinstall%\MSBuild\Current\Bin\
if not exist "%_msbuild%msbuild.exe" goto MSBuildMissing


pushd ..

rem Unzip MPCDI sources
if not exist %MPCDI_SOURCES_DIR% (
	mkdir %MPCDI_SOURCES_DIR%
	tar -xf %MPCDI_SOURCES_ZIP% )

:: Build MPCDI library
echo Building MPCDI for (64-bit, %BUILD_CONFIGURATION%, toolset=%PLATFORM_TOOLSET%)...
"%_msbuild%msbuild.exe" %MPCDI_SOURCES_DIR%\mpcdi_UE.vcxproj /t:build /p:ConfigurationType=StaticLibrary;DefineConstants=MPCDI_EXPORTS;IncludePath=%ADDITIONAL_INC_PATHS%;Configuration=%BUILD_CONFIGURATION%;Platform=x64;PlatformToolset=%PLATFORM_TOOLSET%"

:: Deploy MPCDI library
mkdir ..\..\lib\Win64\x64\%BUILD_CONFIGURATION%
copy /Y %MPCDI_SOURCES_DIR%\Build\x64\%BUILD_CONFIGURATION%\mpcdi_UE.lib ..\..\lib\Win64\x64\%BUILD_CONFIGURATION%\mpcdi.lib

:: Deploy MPCDI headers
mkdir ..\..\include
xcopy %MPCDI_SOURCES_DIR%\*.h ..\..\include /sy

:: Remove sources
rmdir /S /Q %MPCDI_SOURCES_DIR%

popd

exit /b 0

:VStudioMissing
echo Visual Studio not found. Please check your Visual Studio install and try again.
goto Exit

:MSBuildMissing
echo MSBuild not found. Please check your Visual Studio install and try again.
goto Exit

:Exit
exit /b 1