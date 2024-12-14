// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MotionWarping : ModuleRules
{
	public MotionWarping(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",		
				"Engine",
				"NetCore"
			}
			);
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
			}
			);
		
		if (Target.bBuildEditor)
		{
			PublicDependencyModuleNames.AddRange(new string []
				{
					"UnrealEd",
					"AnimGraph",
				});
		}
	}
}
