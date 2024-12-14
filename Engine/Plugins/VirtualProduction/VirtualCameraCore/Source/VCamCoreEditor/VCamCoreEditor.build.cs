// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VCamCoreEditor : ModuleRules
{
	public VCamCoreEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CinematicCamera",
				"CoreUObject",
				"ConcertSyncClient",
				"ConcertSyncCore", 
				"Engine",
				"EditorFramework",
				"EnhancedInput",
				"InputBlueprintNodes",
				"InputCore",
				"KismetCompiler",
				"LiveLinkInterface",
				"PropertyEditor",
				"Slate",
				"SlateCore",
				"ToolWidgets",
				"UMG", 
				"UMGEditor",
				"UnrealEd",
				"VCamCore",
				"VPUtilities"
			}
		);
	}
}
