// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TedsStyling : ModuleRules
{
	public TedsStyling(ReadOnlyTargetRules Target) : base(Target)
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
					"SlateCore",
					"Slate"
				});

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"TypedElementFramework",
				});
		}
	}
}
