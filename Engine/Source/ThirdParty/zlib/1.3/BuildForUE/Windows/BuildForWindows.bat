@rem Copyright Epic Games, Inc. All Rights Reserved.
@echo off

set ENGINE_ROOT=%~dp0..\..\..\..\..\..
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64                           -TargetLib=zlib -TargetLibVersion=1.3 -TargetConfigs=Release -LibOutputPath=lib -CMakeGenerator=VS2022 -CMakeAdditionalArguments="-DMINIZIP=ON" -MakeTarget=zlibstatic -SkipCreateChangelist || exit /b
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetArchitecture=ARM64 -TargetLib=zlib -TargetLibVersion=1.3 -TargetConfigs=Release -LibOutputPath=lib -CMakeGenerator=VS2022 -CMakeAdditionalArguments="-DMINIZIP=ON" -MakeTarget=zlibstatic -SkipCreateChangelist || exit /b
