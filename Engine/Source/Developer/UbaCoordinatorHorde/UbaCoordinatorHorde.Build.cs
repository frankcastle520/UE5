// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

[SupportedPlatforms(UnrealPlatformClass.Desktop)]
public class UbaCoordinatorHorde : ModuleRules
{
	public UbaCoordinatorHorde(ReadOnlyTargetRules Target) : base(Target)
	{
		UnsafeTypeCastWarningLevel = WarningLevel.Error;
		StaticAnalyzerDisabledCheckers.Clear();

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ApplicationCore",
			"Core",
			"DesktopPlatform",
			"Horde",
			"HTTP",
			"Json",
			"Sockets",
		});

		string UbaSourcePath = Path.Combine(EngineDirectory, "Source", "Programs", "UnrealBuildAccelerator");
		PublicIncludePaths.Add(Path.Combine(UbaSourcePath, "Core", "Public"));
		PublicIncludePaths.Add(Path.Combine(UbaSourcePath, "Common", "Public"));
	}
}
