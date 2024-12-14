// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TedsSettings : ModuleRules
{
	public TedsSettings(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		if (Target.bBuildEditor)
		{
			PublicIncludePaths.AddRange(new string[] { });
			PrivateIncludePaths.AddRange(new string[] { });

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"UnrealEd",
				});

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"EditorSubsystem",
					"TypedElementFramework",
				});
		}
	}
}
