// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NDIMedia : ModuleRules
{
	public NDIMedia(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"MediaIOCore",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"CoreUObject",
				"DeveloperSettings",
				"Engine",
				"NDISDK",
				"Projects",
				"Slate",
				"SlateCore",
			});

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(new string[] {
				"UnrealEd",
			});
		}
	}
}
