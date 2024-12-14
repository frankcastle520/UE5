// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DaySequence : ModuleRules
{
	public DaySequence(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"MovieScene",
				"MovieSceneTracks",
				"LevelSequence",
				"UniversalObjectLocator"
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"InputCore",
				"CoreUObject",
				"Engine",
				"DeveloperSettings",
			}
			);

		SetupIrisSupport(Target);
	}
}
