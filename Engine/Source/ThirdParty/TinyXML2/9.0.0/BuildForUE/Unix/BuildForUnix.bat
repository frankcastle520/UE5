@rem Copyright Epic Games, Inc. All Rights Reserved.
@echo off

set ENGINE_ROOT=%~dp0..\..\..\..\..\..
::set CMAKE_ADDITIONAL_ARGUMENTS=-DCMAKE_CXX_COMPILER_WORKS=TRUE
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=x86_64-unknown-linux-gnu      -TargetLib=TinyXML2 -TargetLibVersion=9.0.0 -TargetConfigs=Release -LibOutputPath=lib -CMakeGenerator=Makefile -MakeTarget=tinyxml2 -CMakeAdditionalArguments="%CMAKE_ADDITIONAL_ARGUMENTS%" -SkipCreateChangelist || exit /b
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=aarch64-unknown-linux-gnueabi -TargetLib=TinyXML2 -TargetLibVersion=9.0.0 -TargetConfigs=Release -LibOutputPath=lib -CMakeGenerator=Makefile -MakeTarget=tinyxml2 -CMakeAdditionalArguments="%CMAKE_ADDITIONAL_ARGUMENTS%" -SkipCreateChangelist || exit /b

mkdir %~dp0..\..\include
copy /y %~dp0..\..\tinyxml2.h %~dp0..\..\include\
