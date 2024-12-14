// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class ColorGradingEditor : ModuleRules
{
	public ColorGradingEditor(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"AdvancedWidgets",
				"SceneOutliner",
				"ObjectMixerEditor"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"ApplicationCore",
				"AppFramework",
				"Core",
				"CoreUObject",
				"CinematicCamera",
				"DetailCustomizations",
				"EditorStyle",
				"Engine",
				"InputCore",
				"LevelEditor",
				"PropertyEditor",
				"PropertyPath",
				"Slate",
				"SlateCore",
				"ToolWidgets",
				"UnrealEd",
				"WorkspaceMenuStructure",
			});
	}
}
