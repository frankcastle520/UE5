@rem Copyright Epic Games, Inc. All Rights Reserved.
@echo off

set ENGINE_ROOT=%~dp0..\..\..\..\..\..
set THIRDPARTY_ROOT=%ENGINE_ROOT%\Source\ThirdParty
set CMAKE_ADDITIONAL_ARGUMENTS=-DENABLE_LIB_ONLY=ON -DENABLE_STATIC_LIB=ON -DCMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=OFF
set CMAKE_OPENSSL_ARGUMENTS=-DOPENSSL_ROOT_DIR=%THIRDPARTY_ROOT%\OpenSSL\1.1.1n\lib\Win64\VS2015\Release -DOPENSSL_INCLUDE_DIR=%THIRDPARTY_ROOT%\OpenSSL\1.1.1n\include\Win64\VS2015 -DOPENSSL_USE_STATIC_LIBS=ON
set CMAKE_ZLIB_ARGUMENTS=-DZLIB_ROOT=%THIRDPARTY_ROOT%\zlib\1.2.12\lib\Win64\Release -DZLIB_INCLUDE_DIR=%THIRDPARTY_ROOT%\zlib\1.2.12\include
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetLib=nghttp2 -TargetLibVersion=1.47.0 -TargetConfigs=Release -LibOutputPath=lib -CMakeGenerator=VS2022 -CMakeAdditionalArguments="%CMAKE_ADDITIONAL_ARGUMENTS% %CMAKE_OPENSSL_ARGUMENTS% %CMAKE_ZLIB_ARGUMENTS%" -SkipCreateChangelist || exit /b

rem Now do it all again for Arm64!

set CMAKE_OPENSSL_ARGUMENTS=-DOPENSSL_ROOT_DIR=%THIRDPARTY_ROOT%\OpenSSL\1.1.1n\lib\WinArm64\Release -DOPENSSL_INCLUDE_DIR=%THIRDPARTY_ROOT%\OpenSSL\1.1.1n\include\WinArm64 -DOPENSSL_USE_STATIC_LIBS=ON
set CMAKE_ZLIB_ARGUMENTS=-DZLIB_ROOT=%THIRDPARTY_ROOT%\zlib\1.2.12\lib\Win64\arm64\Release -DZLIB_INCLUDE_DIR=%THIRDPARTY_ROOT%\zlib\1.2.12\include
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetArchitecture=Arm64 -TargetLib=nghttp2 -TargetLibVersion=1.47.0 -TargetConfigs=Release -LibOutputPath=lib -CMakeGenerator=VS2022 -CMakeAdditionalArguments="%CMAKE_ADDITIONAL_ARGUMENTS% %CMAKE_OPENSSL_ARGUMENTS% %CMAKE_ZLIB_ARGUMENTS%" -SkipCreateChangelist || exit /b
