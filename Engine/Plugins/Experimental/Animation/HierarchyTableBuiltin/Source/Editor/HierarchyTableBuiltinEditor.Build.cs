// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using UnrealBuildTool;

public class HierarchyTableBuiltinEditor : ModuleRules
{
	public HierarchyTableBuiltinEditor(ReadOnlyTargetRules Target) : base(Target)
	{
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
			"HierarchyTableBuiltinRuntime",
			"TypedElementFramework" // TODO: Remove
		);
	}
}
