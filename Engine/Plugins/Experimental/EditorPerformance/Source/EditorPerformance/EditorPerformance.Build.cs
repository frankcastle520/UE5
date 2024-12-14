// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class EditorPerformance : ModuleRules
{
	public EditorPerformance(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"Slate",
				"ApplicationCore",
				"SlateCore",
				"InputCore",
				"EditorFramework",
				"UnrealEd",
				"ToolMenus",
				"OutputLog",
				"EditorSubsystem",
				"WorkspaceMenuStructure",
				"MessageLog",
				"ToolWidgets",
				"DerivedDataCache",
				"Virtualization",
				"StudioTelemetry"
			});

		PrivateIncludePathModuleNames.AddRange
		(
			new string[]
			{
				"WorkspaceMenuStructure",
			}
		);

		UnsafeTypeCastWarningLevel = WarningLevel.Error;
	}
}
