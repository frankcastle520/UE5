// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TedsTableViewer : ModuleRules
{
	public TedsTableViewer(ReadOnlyTargetRules Target) : base(Target)
	{

		if (Target.bBuildEditor)
		{
			PublicIncludePaths.AddRange(new string[] {});
			PrivateIncludePaths.AddRange(new string[] {});

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"TypedElementFramework"
				});

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"InputCore",
					"Slate",
					"SlateCore",
					"WorkspaceMenuStructure",
					"ToolWidgets"
				});
		}
	}
}