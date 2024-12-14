// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class WorkspaceEditor : ModuleRules
	{
		public WorkspaceEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core", 
					"UnrealEd", 
				});
			
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"CoreUObject",
					"Engine",
					"AssetTools",
					"ApplicationCore",
					"SlateCore",
					"Slate",
					"InputCore",
					"PropertyEditor",
					"GraphEditor",
					"ToolWidgets",
					"ToolMenus",
					"AssetDefinition",
					"SourceControl", 
					"KismetWidgets",
					"DesktopWidgets",
					"Settings",
					"EditorWidgets",
					"Json",
					"JsonUtilities",
					"AssetRegistry",
					"ContentBrowser",
					"SceneOutliner",
					"EditorInteractiveToolsFramework",
					"InteractiveToolsFramework",
					"EditorFramework"
				}
			);
		}
	}
}