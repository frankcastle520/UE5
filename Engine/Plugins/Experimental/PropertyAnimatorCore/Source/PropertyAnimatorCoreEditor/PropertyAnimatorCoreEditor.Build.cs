// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PropertyAnimatorCoreEditor : ModuleRules
{
    public PropertyAnimatorCoreEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
	        new string[]
	        {
		        "Core"
	        }
        );

        PrivateDependencyModuleNames.AddRange(
	        new string[]
	        {
		        "ApplicationCore",
				"AssetDefinition",
		        "CoreUObject",
		        "EditorSubsystem",
		        "EditorWidgets",
		        "Engine",
		        "InputCore",
				"LevelSequence",
				"MovieScene",
				"MovieSceneTools",
		        "OperatorStackEditor",
		        "Projects",
				"PropertyAnimatorCore",
				"PropertyEditor",
				"Sequencer",
		        "SlateCore",
		        "Slate",
		        "ToolMenus",
		        "UnrealEd"
	        }
        );

		ShortName = "PropAnimCoreEd";
    }
}