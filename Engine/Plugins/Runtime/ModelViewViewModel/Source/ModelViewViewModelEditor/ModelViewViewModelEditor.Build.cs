// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using System.Collections.Generic;
using UnrealBuildTool;

public class ModelViewViewModelEditor : ModuleRules 
{
	public ModelViewViewModelEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Engine",
				"BlueprintGraph",
				"Core",
				"CoreUObject",
				"EditorSubsystem",
				"FieldNotification",
				"KismetCompiler",
				"ModelViewViewModel",
				"ModelViewViewModelBlueprint",
				"PropertyPath",
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AdvancedWidgets",
				"AssetTools",
				"EditorWidgets",
				"GraphEditor",
				"InputCore",
				"Kismet",
				"MessageLog",
				"Projects",
				"PropertyEditor",
				"StatusBar",
				"SlateCore",
				"Slate",
				"ToolMenus",
				"ToolWidgets",
				"UnrealEd",
				"UMG",
				"UMGEditor",
			});
	}
}
