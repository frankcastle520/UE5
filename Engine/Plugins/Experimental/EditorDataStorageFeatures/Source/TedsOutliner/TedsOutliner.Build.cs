// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TedsOutliner : ModuleRules
{
	public TedsOutliner(ReadOnlyTargetRules Target) : base(Target)
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
					"EditorFramework",
					"Engine",
					"SceneOutliner",
					"Slate",
					"SlateCore",
					"TypedElementFramework",
				});
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"WorkspaceMenuStructure",
					"UnrealEd", // FEditorUndoClient used by SSceneOutliner
					"ToolMenus",
					"ApplicationCore",
					"TedsTableViewer",
					"ToolWidgets"
				});

			DynamicallyLoadedModuleNames.AddRange(new string[] {});
		}
	}
}
