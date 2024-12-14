// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NDIMediaEditor : ModuleRules
{
	public NDIMediaEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"MediaIOCore",
				"MediaIOEditor",
				"NDIMedia",
				"Slate",
				"SlateCore",
				"TimeManagement",
				"UnrealEd",
			});
		
		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"AssetTools",
				"TargetPlatform",
			});

		PrivateDependencyModuleNames.AddRange(
		new string[] {
				"Projects",
			});
	}
}
