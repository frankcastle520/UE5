// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CQTestTests : ModuleRules
{
	public CQTestTests(ReadOnlyTargetRules Target)
		: base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[] {
					"Core",
					"CoreUObject",
					"Engine",
					"Slate",
					"CQTest"
				 }
			);

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"AssetTools",
					"EngineSettings",
					"LevelEditor",
					"UnrealEd"
			});
		}
	}
}
