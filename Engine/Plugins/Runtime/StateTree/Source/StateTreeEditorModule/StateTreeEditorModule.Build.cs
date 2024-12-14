// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class StateTreeEditorModule : ModuleRules
	{
		public StateTreeEditorModule(ReadOnlyTargetRules Target) : base(Target)
		{
			UnsafeTypeCastWarningLevel = WarningLevel.Warning;

			PublicIncludePaths.AddRange(
			new string[] {
			}
			);

			PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"AssetTools",
				"EditorFramework",
				"UnrealEd",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"PropertyEditor",
				"StateTreeModule",
				"SourceControl",
				"Projects",
				"BlueprintGraph",
				"PropertyAccessEditor",
				"StructUtilsEditor",
				"GameplayTags",
				"EditorSubsystem"
			}
			);

			PrivateDependencyModuleNames.AddRange(
			new string[] {
				"AssetDefinition",
				"RenderCore",
				"GraphEditor",
				"KismetWidgets",
				"PropertyPath",
				"SourceCodeAccess",
				"ToolMenus",
				"ToolWidgets",
				"ApplicationCore",
				"DeveloperSettings",
				"RewindDebuggerInterface",
				"DetailCustomizations",
				"AppFramework",
				"Kismet",
				"KismetCompiler",
				"EditorInteractiveToolsFramework",
				"InteractiveToolsFramework",
			}
			);

			PrivateIncludePathModuleNames.AddRange(new string[] {
				"MessageLog",
			});

			PublicDefinitions.Add("WITH_STATETREE_TRACE=1");
			PublicDefinitions.Add("WITH_STATETREE_TRACE_DEBUGGER=1");
		}
	}
}
