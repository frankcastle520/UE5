// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AnimNextStateTreeEditor : ModuleRules
	{
		public AnimNextStateTreeEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
				    "AnimNextStateTree",
				    "MessageLog",
				    "EditorInteractiveToolsFramework",
				    "InteractiveToolsFramework",
				    "EditorFramework",
				    "AnimNext",
				    "UnrealEd",
				    "AssetDefinition",
				    "AnimNextAnimGraph",
				    "RigVM",
					"ToolMenus"
			    });

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"WorkspaceEditor",
					"StateTreeEditorModule",
					"StateTreeModule",
					"AnimNextStateTreeUncookedOnly",
					"AnimNextEditor",
					"SlateCore",
					"EditorSubsystem",
					"Engine",
					"Slate",
					"RigVMDeveloper",
					"AnimNextUncookedOnly"
				}
			);
		}
	}
}