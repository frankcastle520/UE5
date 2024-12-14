@ECHO OFF

REM Copyright Epic Games, Inc. All Rights Reserved.

setlocal

rem LibWebSockets is provided external to the engine, either via perforce or github.
rem Request this path via the batch prompt.
set /p LIBWEBSOCKETS_SRC= Enter path for libWebSockets... 

set ENGINE_ROOT=%CD%\..\..\..\..\..
set ENGINE_THIRD_PARTY=%ENGINE_ROOT%\Source\ThirdParty
set TARGET_LIB=libWebSockets
set TARGET_LIB_VERSION=libwebsockets
set TARGET_LIB_SOURCE_PATH=%LIBWEBSOCKETS_SRC%
set TARGET_PLATFORM=Win64
set TARGET_ARCHITECTURE=Arm64
set TARGET_CONFIGS=release+debug
set LIB_OUTPUT_PATH=lib
set CMAKE_GENERATOR=VS2022
set CMAKE_ADDITIONAL_ARGUMENTS="-DLWS_OPENSSL_INCLUDE_DIRS=%ENGINE_THIRD_PARTY%\OpenSSL\1.1.1t\include\WinArm64 -DLWS_OPENSSL_LIBRARIES=%ENGINE_THIRD_PARTY%\OpenSSL\1.1.1t\lib\WinArm64\${TARGET_CONFIG}\libssl.a;%ENGINE_THIRD_PARTY%\OpenSSL\1.1.1t\lib\WinArm64\${TARGET_CONFIG}\libcrypto.a -DLWS_ZLIB_INCLUDE_DIRS=%ENGINE_ROOT%\Source\ThirdParty\zlib\1.2.13\include -DLWS_ZLIB_LIBRARIES=%CD%\..\..\..\zlib\1.2.13\lib\Release\libzlibstatic.a -DLWS_SSL_CLIENT_USE_OS_CA_CERTS:BOOL=ON -DLWS_WITHOUT_TESTAPPS:BOOL=ON -DLWS_WITH_HTTP2:BOOL=OFF -DLWS_WITH_SHARED:BOOL=OFF -DLWS_WITH_ZIP_FOPS:BOOL=OFF -DLWS_HAVE_GETENV:BOOL=OFF -DLWS_USE_BUNDLED_ZLIB:BOOL=OFF -DLWS_WITHOUT_SERVER:BOOL=ON -DLWS_PLATFORM_EXTERNAL:BOOL=ON -DLWS_TARGET_PLATFORM=%TARGET_PLATFORM%"
set MAKE_TARGET=all

%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat BuildCMakeLib -TargetLib=%TARGET_LIB% -TargetLibVersion=%TARGET_LIB_VERSION% -TargetLibSourcePath=%TARGET_LIB_SOURCE_PATH% -TargetPlatform=%TARGET_PLATFORM% -TargetArchitecture=%TARGET_ARCHITECTURE%^
 -TargetConfigs=%TARGET_CONFIGS% -LibOutputPath=%LIB_OUTPUT_PATH% -CMakeGenerator=%CMAKE_GENERATOR% -CMakeGenerator=%CMAKE_GENERATOR% -CMakeAdditionalArguments=%CMAKE_ADDITIONAL_ARGUMENTS% -MakeTarget=%MAKE_TARGET% -SkipSubmit

PAUSE
goto Exit

:Exit
endlocal
