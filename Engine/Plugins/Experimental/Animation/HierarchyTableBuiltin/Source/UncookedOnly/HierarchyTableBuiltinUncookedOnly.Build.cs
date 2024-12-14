// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using UnrealBuildTool;

public class HierarchyTableBuiltinUncookedOnly : ModuleRules
{
	public HierarchyTableBuiltinUncookedOnly(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"AnimationCore",
				"AnimGraph",
				"AnimGraphRuntime",
				"AnimationBlueprintLibrary",
				"ToolMenus"
			});

		PrivateDependencyModuleNames.AddAll(
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"HierarchyTableRuntime",
			"HierarchyTableEditor",
			"HierarchyTableBuiltinRuntime"
		);

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
						"BlueprintGraph",
						"EditorFramework",
						"Kismet",
						"UnrealEd",
				}
			);
		}
	}
}
