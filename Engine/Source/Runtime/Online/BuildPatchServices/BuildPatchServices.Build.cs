// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using UnrealBuildTool;

public class BuildPatchServices : ModuleRules
{
	public BuildPatchServices(ReadOnlyTargetRules Target) : base(Target)
	{
		StaticAnalyzerDisabledCheckers.Add("core.uninitialized.ArraySubscript");

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject"
			}
		);

		PrivateDependencyModuleNames.AddRange(
		new string[] {
				"Analytics",
				"AnalyticsET",
				"HTTP",
				"Json",
				"VirtualFileCache",
			}
		);
	}
}
