// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TedsDebugger : ModuleRules
{
	public TedsDebugger(ReadOnlyTargetRules Target) : base(Target)
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
					"CoreUObject"
				});
			
			PrivateDependencyModuleNames.AddRange(
            	new string[]
            	{
            		"TedsOutliner",
		            "WorkspaceMenuStructure",
		            "TypedElementFramework",
		            "SceneOutliner",
		            "SlateCore",
		            "Slate",
		            "InputCore",
		            "EditorWidgets",
		            "ToolWidgets",
		            "EditorWidgets",
		            "TedsTableViewer"
            	});
			
			DynamicallyLoadedModuleNames.AddRange(new string[] {});
		}
	}
}
