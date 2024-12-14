// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AudioInsights : ModuleRules
{
	public AudioInsights(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange
		(
			new string[]
			{
				"Core",
				"TraceInsights",
				"TraceServices",
				"ToolWidgets",
			}
		);	
		
		PrivateDependencyModuleNames.AddRange
		(
			new string[]
			{
				"AudioMixerCore",
				"CoreUObject",
				"InputCore",
				"OutputLog",
				"SessionServices",
				"Slate",
				"SlateCore",
				"TraceAnalysis",
				"TraceLog",
			}
		);

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Engine",
				"UnrealEd",
			});
		}
	}
}
