// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using UnrealBuildTool;

public class HierarchyTableEditor : ModuleRules
{
	public HierarchyTableEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddAll(
			"AnimGraph",
			"AssetDefinition",
			"Core",
			"CoreUObject",
			"Engine", 
			"InputCore",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"HierarchyTableRuntime",
			"TedsOutliner",
			"TypedElementFramework",
			"SceneOutliner",
			"ToolMenus",
			"Persona",
			"TedsTableViewer"
		);
	}
}
