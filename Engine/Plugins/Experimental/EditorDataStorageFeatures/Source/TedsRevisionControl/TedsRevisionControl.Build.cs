// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TedsRevisionControl : ModuleRules
{
	public TedsRevisionControl(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		if (Target.bBuildEditor)
		{
			PublicIncludePaths.AddRange(new string[] {});
			PrivateIncludePaths.AddRange(new string[] {});

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"SourceControl",
					"TypedElementFramework",
				});

			DynamicallyLoadedModuleNames.AddRange(new string[] {});
		}
	}
}