// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AnimNextEditor : ModuleRules
	{
		public AnimNextEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"ApplicationCore",
					"Engine",
					"AssetTools",
					"UnrealEd",
					"AnimNext",
					"AnimNextUncookedOnly",
					"UnrealEd",
					"SlateCore",
					"AnimationCore",
					"Slate",
					"InputCore",
					"PropertyEditor",
					"RigVM",
					"RigVMEditor",
					"RigVMDeveloper",
					"ControlRig",
					"ControlRigDeveloper",
					"ControlRigEditor",
					"GraphEditor",
					"ToolWidgets",
					"ToolMenus",
					"AssetDefinition",
					"SourceControl", 
					"KismetWidgets",
					"StructUtilsEditor",
					"BlueprintGraph",	// For K2 Schema
					"DesktopWidgets",
					"ContentBrowserFileDataSource",
					"SubobjectEditor",
					"Settings",
					"EditorWidgets",
					"WorkspaceEditor",
					"ContentBrowser",
					"UniversalObjectLocator",
					"UniversalObjectLocatorEditor",
					"Kismet",
					"AdvancedWidgets", 
					"SceneOutliner",
				}
			);
		}
	}
}