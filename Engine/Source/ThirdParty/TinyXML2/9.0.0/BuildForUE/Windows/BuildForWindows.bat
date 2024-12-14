@rem Copyright Epic Games, Inc. All Rights Reserved.
@echo off

set ENGINE_ROOT=%~dp0..\..\..\..\..\..
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64                           -TargetLib=TinyXML2 -TargetLibVersion=9.0.0 -TargetConfigs=Release -LibOutputPath=lib -CMakeGenerator=VS2022 -MakeTarget=tinyxml2 -SkipCreateChangelist || exit /b
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetArchitecture=ARM64 -TargetLib=TinyXML2 -TargetLibVersion=9.0.0 -TargetConfigs=Release -LibOutputPath=lib -CMakeGenerator=VS2022 -MakeTarget=tinyxml2 -SkipCreateChangelist || exit /b

mkdir %~dp0..\..\include
copy /y %~dp0..\..\tinyxml2.h %~dp0..\..\include\
